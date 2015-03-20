/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/******************************************************************************
 *
 *  The original Work has been changed by NXP Semiconductors.
 *
 *  Copyright (C) 2014 NXP Semiconductors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
/*
 *  Manage the listen-mode routing table.
 */

#include <cutils/log.h>
#include <ScopedLocalRef.h>
#include <JNIHelp.h>
#include "config.h"
#include "JavaClassConstants.h"
#include "RoutingManager.h"

extern "C"
{
    #include "phNxpConfig.h"
    #include "nfa_ee_api.h"
    #include "nfa_ce_api.h"
}
extern bool gActivated;
extern SyncEvent gDeactivatedEvent;

const JNINativeMethod RoutingManager::sMethods [] =
{
    {"doGetDefaultRouteDestination", "()I", (void*) RoutingManager::com_android_nfc_cardemulation_doGetDefaultRouteDestination},
    {"doGetDefaultOffHostRouteDestination", "()I", (void*) RoutingManager::com_android_nfc_cardemulation_doGetDefaultOffHostRouteDestination},
    {"doGetAidMatchingMode", "()I", (void*) RoutingManager::com_android_nfc_cardemulation_doGetAidMatchingMode},
    {"doGetAidMatchingPlatform", "()I", (void*) RoutingManager::com_android_nfc_cardemulation_doGetAidMatchingPlatform}
};

static const int MAX_NUM_EE = 5;

namespace android
{
    extern  void  checkforTranscation(UINT8 connEvent, void* eventData );
    extern jmethodID  gCachedNfcManagerNotifyAidRoutingTableFull;
}
RoutingManager::RoutingManager ()
:   mNativeData(NULL),
    mHostListnEnable (true),
    mFwdFuntnEnable (true)
{
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    static const char fn [] = "RoutingManager::RoutingManager()";
    unsigned long num = 0;

    // Get the active SE
    if (GetNumValue("ACTIVE_SE", &num, sizeof(num)))
        mActiveSe = num;
    else
        mActiveSe = 0x00;

    // Get the "default" route
    if (GetNumValue("DEFAULT_ISODEP_ROUTE", &num, sizeof(num)))
        mDefaultEe = num;
    else
        mDefaultEe = 0x00;
    ALOGD("%s: default route is 0x%02X", fn, mDefaultEe);

    // Get the default "off-host" route.  This is hard-coded at the Java layer
    // but we can override it here to avoid forcing Java changes.
    if (GetNumValue("DEFAULT_OFFHOST_ROUTE", &num, sizeof(num)))
        mOffHostEe = num;
    else
        mOffHostEe = 0x02;
//        mOffHostEe = 0xf4;
    if (GetNumValue("AID_MATCHING_MODE", &num, sizeof(num)))
        mAidMatchingMode = num;
    else
        mAidMatchingMode = AID_MATCHING_EXACT_ONLY;

    if (GetNxpNumValue("AID_MATCHING_PLATFORM", &num, sizeof(num)))
        mAidMatchingPlatform = num;
    else
        mAidMatchingPlatform = AID_MATCHING_L;

    ALOGD("%s: mOffHostEe=0x%02X", fn, mOffHostEe);

    memset (&mEeInfo, 0, sizeof(mEeInfo));
    mReceivedEeInfo = false;
    mSeTechMask = 0x00;
#endif
}

void *stop_reader_event_handler_async(void *data);
void *reader_event_handler_async(void *data);
int RoutingManager::mChipId = 0;
bool recovery;
RoutingManager::~RoutingManager ()
{
    NFA_EeDeregister (nfaEeCallback);
}

bool RoutingManager::initialize (nfc_jni_native_data* native)
{
    static const char fn [] = "RoutingManager::initialize()";
    unsigned long num = 0, tech = 0;
    mNativeData = native;
    UINT8 mActualNumEe = SecureElement::MAX_NUM_EE;
    tNFA_EE_INFO mEeInfo [mActualNumEe];
  //  mIsDirty = true;

    if (GetNxpNumValue (NAME_NXP_DEFAULT_SE, (void*)&num, sizeof(num)))
    {
        ALOGD ("%d: nfcManager_GetDefaultSE", num);
        mDefaultEe = num;
    }
    else
    {
        mDefaultEe = NFA_HANDLE_INVALID;
    }
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    if ((GetNumValue(NAME_HOST_LISTEN_ENABLE, &tech, sizeof(tech))))
    {
        mHostListnEnable = tech;
        ALOGD ("%s:HOST_LISTEN_ENABLE=0x0%lu;", __FUNCTION__, tech);
    }
    if ((GetNxpNumValue(NAME_NXP_NFC_CHIP, &num, sizeof(num))))
    {
        ALOGD ("%s:NXP_NFC_CHIP=0x0%lu;", __FUNCTION__, num);
        mChipId = num;
    }
    if ((GetNumValue(NAME_NXP_FWD_FUNCTIONALITY_ENABLE, &tech, sizeof(tech))))
    {
        mFwdFuntnEnable = tech;
        ALOGE ("%s:NXP_FWD_FUNCTIONALITY_ENABLE=%d;", __FUNCTION__, mFwdFuntnEnable);
    }

    if (GetNxpNumValue (NAME_NXP_CE_ROUTE_STRICT_DISABLE, (void*)&num, sizeof(num)) == false)
        num = 0x00; // default value
    mCeRouteStrictDisable = num;
#endif
    tNFA_STATUS nfaStat;
    {
        SyncEventGuard guard (mEeRegisterEvent);
        ALOGD ("%s: try ee register", fn);
        nfaStat = NFA_EeRegister (nfaEeCallback);
        if (nfaStat != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail ee register; error=0x%X", fn, nfaStat);
            return false;
        }
        mEeRegisterEvent.wait ();
    }

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    if(tech)
    {
        // Tell the host-routing to only listen on Nfc-A/Nfc-B
        nfaStat = NFA_CeRegisterAidOnDH (NULL, 0, stackCallback);
        if (nfaStat != NFA_STATUS_OK)
            ALOGE ("Failed to register wildcard AID for DH");
        // Tell the host-routing to only listen on Nfc-A/Nfc-B
        nfaStat = NFA_CeSetIsoDepListenTech(tech & 0x03);
        if (nfaStat != NFA_STATUS_OK)
        ALOGE ("Failed to configure CE IsoDep technologies");
       // setRouting(true);
    }
    mRxDataBuffer.clear ();

#else
    if (mActiveSe != 0) {
        {
            // Wait for EE info if needed
            SyncEventGuard guard (mEeInfoEvent);
            if (!mReceivedEeInfo)
            {
                ALOGE("Waiting for EE info");
                mEeInfoEvent.wait();
            }
        }
        for (UINT8 i = 0; i < mEeInfo.num_ee; i++)
        {
             ALOGD ("%s   EE[%u] Handle: 0x%04x  techA: 0x%02x  techB: 0x%02x  techF: 0x%02x  techBprime: 0x%02x",
                fn, i, mEeInfo.ee_disc_info[i].ee_handle,
                mEeInfo.ee_disc_info[i].la_protocol,
                mEeInfo.ee_disc_info[i].lb_protocol,
                mEeInfo.ee_disc_info[i].lf_protocol,
                mEeInfo.ee_disc_info[i].lbp_protocol);
             if (mEeInfo.ee_disc_info[i].ee_handle == (mActiveSe | NFA_HANDLE_GROUP_EE))
             {
                 if (mEeInfo.ee_disc_info[i].la_protocol != 0) mSeTechMask |= NFA_TECHNOLOGY_MASK_A;

                 if (mSeTechMask != 0x00)
                 {
                     ALOGD("Configuring tech mask 0x%02x on EE 0x%04x", mSeTechMask, mEeInfo.ee_disc_info[i].ee_handle);
                     nfaStat = NFA_CeConfigureUiccListenTech(mEeInfo.ee_disc_info[i].ee_handle, mSeTechMask);
                     if (nfaStat != NFA_STATUS_OK)
                         ALOGE ("Failed to configure UICC listen technologies.");
                     // Set technology routes to UICC if it's there
                     nfaStat = NFA_EeSetDefaultTechRouting(mEeInfo.ee_disc_info[i].ee_handle, mSeTechMask, mSeTechMask,
                             mSeTechMask);
                     if (nfaStat != NFA_STATUS_OK)
                         ALOGE ("Failed to configure UICC technology routing.");
                 }
             }
        }
    }

    // Tell the host-routing to only listen on Nfc-A
    nfaStat = NFA_CeSetIsoDepListenTech(NFA_TECHNOLOGY_MASK_A);
    if (nfaStat != NFA_STATUS_OK)
        ALOGE ("Failed to configure CE IsoDep technologies");

    // Register a wild-card for AIDs routed to the host
    nfaStat = NFA_CeRegisterAidOnDH (NULL, 0, stackCallback);
    if (nfaStat != NFA_STATUS_OK)
        ALOGE("Failed to register wildcard AID for DH");
#endif

if(mChipId == 0x02 || mChipId == 0x04)
    {
        if ((nfaStat = NFA_AllEeGetInfo (&mActualNumEe, mEeInfo)) != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail get info; error=0x%X", fn, nfaStat);
            mActualNumEe = 0;
        }
        else
        {
            for(int xx = 0; xx <  mActualNumEe; xx++)
            {
                ALOGE("xx=%d, ee_handle=0x0%x, status=0x0%x", xx, mEeInfo[xx].ee_handle,mEeInfo[xx].ee_status);
                if ((mEeInfo[xx].ee_handle == 0x4C0) &&
                        (mEeInfo[xx].ee_status == 0x02))
                {
                    ee_removed_disc_ntf_handler(mEeInfo[xx].ee_handle, mEeInfo[xx].ee_status);
                    break;
                }
            }
        }
    }
    return true;
}

RoutingManager& RoutingManager::getInstance ()
{
    static RoutingManager manager;
    return manager;
}

void RoutingManager::cleanRouting()
{
    tNFA_STATUS nfaStat;
    //tNFA_HANDLE seHandle = NFA_HANDLE_INVALID;        /*commented to eliminate unused variable warning*/
    tNFA_HANDLE ee_handleList[SecureElement::MAX_NUM_EE];
    UINT8 i, count;
   // static const char fn [] = "SecureElement::cleanRouting";   /*commented to eliminate unused variable warning*/
    SecureElement::getInstance().getEeHandleList(ee_handleList, &count);
    if (count > SecureElement::MAX_NUM_EE) {
    count = SecureElement::MAX_NUM_EE;
    ALOGD("Count is more than SecureElement::MAX_NUM_EE,Forcing to SecureElement::MAX_NUM_EE");
    }
    for ( i = 0; i < count; i++)
    {
        nfaStat =  NFA_EeSetDefaultTechRouting(ee_handleList[i],0,0,0);
        if(nfaStat == NFA_STATUS_OK)
        {
            mRoutingEvent.wait ();
        }
        nfaStat =  NFA_EeSetDefaultProtoRouting(ee_handleList[i],0,0,0);
        if(nfaStat == NFA_STATUS_OK)
        {
            mRoutingEvent.wait ();
        }
    }
    //clean HOST
    nfaStat =  NFA_EeSetDefaultTechRouting(NFA_EE_HANDLE_DH,0,0,0);
    if(nfaStat == NFA_STATUS_OK)
    {
        mRoutingEvent.wait ();
    }
    nfaStat =  NFA_EeSetDefaultProtoRouting(NFA_EE_HANDLE_DH,0,0,0);
    if(nfaStat == NFA_STATUS_OK)
    {
        mRoutingEvent.wait ();
    }
//    nfaStat = NFA_EeUpdateNow();
//    if (nfaStat != NFA_STATUS_OK)
//        ALOGE("Failed to commit routing configuration");

}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
void RoutingManager::setRouting(bool isHCEEnabled)
{
    tNFA_STATUS nfaStat;
    tNFA_HANDLE ee_handle = NFA_EE_HANDLE_DH;
    UINT8 i, count;
    static const char fn [] = "SecureElement::setRouting";
    unsigned long num = 0x03, tech=0; /*Enable TechA and TechB routing for Host.*/
    unsigned long max_tech_mask = 0x03;

    if (!mIsDirty)
    {
        return;
    }
    mIsDirty = false;
    SyncEventGuard guard (mRoutingEvent);
    ALOGE("Inside %s mDefaultEe %d isHCEEnabled %d ee_handle :0x%x", fn, mDefaultEe, isHCEEnabled,ee_handle);
    /*
     * UICC_LISTEN_TECH_MASK is taken as common to both eSE and UICC
     * In order to control routing and listen phase */
    if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num))))
    {
        ALOGD ("%s:UICC_LISTEN_MASK=0x0%d;", __FUNCTION__, num);
    }
    if (mDefaultEe == 0x01) //eSE
    {
        ee_handle = 0x4C0;
        max_tech_mask = SecureElement::getInstance().getSETechnology(ee_handle);
        //num = NFA_TECHNOLOGY_MASK_A;
        ALOGD ("%s:ESE_LISTEN_MASK=0x0%d;", __FUNCTION__, num);
    }
    else if (mDefaultEe == 0x02) //UICC
    {
        ee_handle = 0x402;
        max_tech_mask = SecureElement::getInstance().getSETechnology(ee_handle);
/*
        SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
        nfaStat = NFA_CeConfigureUiccListenTech (ee_handle, (num & 0x07));
        if(nfaStat == NFA_STATUS_OK)
        {
            SecureElement::getInstance().mUiccListenEvent.wait ();
        }
        else
            ALOGE ("fail to start UICC listen");
*/
    }

    {
        if ((GetNumValue(NAME_HOST_LISTEN_ENABLE, &tech, sizeof(tech))))
        {
            ALOGD ("%s:HOST_LISTEN_ENABLE=0x0%lu;", __FUNCTION__, tech);
        }
        if(tech)
        {
            // Default routing for IsoDep protocol
            nfaStat = NFA_EeSetDefaultProtoRouting(NFA_EE_HANDLE_DH, NFA_PROTOCOL_MASK_ISO_DEP, 0, 0);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
            {
                ALOGE ("Fail to set default proto routing");
            }
        }

        nfaStat =  NFA_EeSetDefaultTechRouting(ee_handle, num & max_tech_mask, num & max_tech_mask, num & max_tech_mask);
        if(nfaStat == NFA_STATUS_OK)
        {
            mRoutingEvent.wait ();
        }

        SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
        nfaStat = NFA_CeConfigureUiccListenTech (ee_handle, (num & 0x07));
        if(nfaStat == NFA_STATUS_OK)
        {
            SecureElement::getInstance().mUiccListenEvent.wait ();
        }
        else
            ALOGE ("fail to start UICC listen");
    }

    // Commit the routing configuration
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK)
        ALOGE("Failed to commit routing configuration");
}

bool RoutingManager::setDefaultRoute(const UINT8 defaultRoute, const UINT8 protoRoute, const UINT8 techRoute)
{
    tNFA_STATUS nfaStat;
    static const char fn [] = "RoutingManager::setDefaultRoute";    /*commented to eliminate unused variable warning*/
    unsigned long uiccListenTech = 0;
    unsigned long hostListenTech = 0;
    tNFA_HANDLE defaultHandle ,ActDevHandle = NFA_HANDLE_INVALID;

    tNFA_HANDLE preferred_defaultHandle = NFA_HANDLE_INVALID;
    UINT8 isDefaultProtoSeIDPresent = 0;
    SyncEventGuard guard (mRoutingEvent);

    if (mDefaultEe == 0x01) //eSE
    {
        preferred_defaultHandle = 0x4C0;
    }
    else if (mDefaultEe == 0x02) //UICC
    {
        preferred_defaultHandle = 0x402;
    }

    // Host
    hostListenTech=uiccListenTech = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;
    defaultSeID = (((defaultRoute & 0x18) >> 3) == 0x00)  ? 0x400 :  ((((defaultRoute & 0x18)>>3 )== 0x01 ) ? 0x4C0 : 0x402);
    defaultPowerstate=defaultRoute & 0x07;
    ALOGD ("%s: enter, defaultSeID:%x defaultPowerstate:0x%x", fn, defaultSeID,defaultPowerstate);
    defaultProtoSeID = (((protoRoute & 0x18) >> 3) == 0x00)  ? 0x400 :  ((((protoRoute & 0x18)>>3 )== 0x01 ) ? 0x4C0 : 0x402);
    defaultProtoPowerstate = protoRoute & 0x07;
    ALOGD ("%s: defaultProtoSeID:%x defaultProtoPowerstate:0x%x", fn, defaultProtoSeID,defaultProtoPowerstate);
    defaultTechSeID = (((techRoute & 0x18) >> 3) == 0x00)  ? 0x400 :  ((((techRoute & 0x18)>>3 )== 0x01 ) ? 0x4C0 : 0x402);
    defaultTechAPowerstate = techRoute & 0x07;
    DefaultTechType = (techRoute & 0x20) >> 5;

    ALOGD ("%s:, defaultTechSeID:%x defaultTechAPowerstate:0x%x,defaultTechType:0x%x", fn, defaultTechSeID,defaultTechAPowerstate,DefaultTechType);
    cleanRouting();

    if ((GetNumValue(NAME_HOST_LISTEN_ENABLE, &hostListenTech, sizeof(hostListenTech))))
    {
        ALOGD ("%s:HOST_LISTEN_ENABLE=0x0%lu;", __FUNCTION__, hostListenTech);
    }

    if(hostListenTech)
    {
        nfaStat = NFA_CeRegisterAidOnDH (NULL, 0, stackCallback);
        if (nfaStat != NFA_STATUS_OK)
           ALOGE("Failed to register wildcard AID for DH");
    }
    {
        UINT8 count,seId=0;
        tNFA_HANDLE ee_handleList[SecureElement::MAX_NUM_EE];
        SecureElement::getInstance().getEeHandleList(ee_handleList, &count);
        for (int  i = 0; ((count != 0 ) && (i < count)); i++)
        {
            seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
            defaultHandle = SecureElement::getInstance().getEseHandleFromGenericId(seId);
            ALOGD ("%s: ee_handleList[%d]:%x", fn, i,ee_handleList[i]);
            //defaultHandle = ee_handleList[i];
            if (preferred_defaultHandle == defaultHandle)
            {
                //ActSEhandle = defaultHandle;
                break;
            }


         }
         for (int  i = 0; ((count != 0 ) && (i < count)); i++)
         {
             seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
             ActDevHandle = SecureElement::getInstance().getEseHandleFromGenericId(seId);
             ALOGD ("%s: ee_handleList[%d]:%x", fn, i,ee_handleList[i]);
             if (defaultProtoSeID == ActDevHandle)
             {
                 isDefaultProtoSeIDPresent =1;
                 break;
             }

         }

    }

    if(!isDefaultProtoSeIDPresent)
    {
        defaultProtoSeID = 0x400;
        defaultProtoPowerstate = 0x01;
    }
    ALOGD ("%s: isDefaultProtoSeIDPresent:%x", fn, isDefaultProtoSeIDPresent);

    if( defaultProtoSeID == defaultSeID)
    {
        unsigned int default_proto_power_mask[3] = {0,};

        for(int pCount=0 ; pCount< 3 ;pCount++)
        {
            if((defaultPowerstate >> pCount)&0x01)
            {
                default_proto_power_mask[pCount] |= NFA_PROTOCOL_MASK_ISO7816;
            }

            if((defaultProtoPowerstate >> pCount)&0x01)
            {
                default_proto_power_mask[pCount] |= NFA_PROTOCOL_MASK_ISO_DEP;
            }
        }

        if(defaultProtoSeID == 0x400 && mHostListnEnable == FALSE)
        {
            ALOGE("%s, HOST is disabled hence skipping configure proto route to host", fn);
        }
        else
        {
            nfaStat = NFA_EeSetDefaultProtoRouting(defaultProtoSeID ,
                                                  default_proto_power_mask[0],
                                                  default_proto_power_mask[1],
                                                  default_proto_power_mask[2]);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
            {
                ALOGE ("Fail to set  iso7816 routing");
            }
        }
    }
    else
    {
        ALOGD ("%s: enter, defaultPowerstate:%x", fn, defaultPowerstate);
        if(mHostListnEnable == FALSE && defaultSeID == 0x400)
        {
            ALOGE("%s, HOST is disabled hence skipping configure 7816 route to host", fn);
         }
         else
         {
             nfaStat = NFA_EeSetDefaultProtoRouting(defaultSeID ,
                                                   (defaultPowerstate & 01) ? NFA_PROTOCOL_MASK_ISO7816 :0,
                                                   (defaultPowerstate & 02) ? NFA_PROTOCOL_MASK_ISO7816 :0,
                                                   (defaultPowerstate & 04) ? NFA_PROTOCOL_MASK_ISO7816 :0
                                                   );
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
            {
                ALOGE ("Fail to set  iso7816 routing");
            }
        }

        if(mHostListnEnable == FALSE && defaultProtoSeID == 0x400)
        {
             ALOGE("%s, HOST is disabled hence skipping configure ISO-DEP route to host", fn);
        }
        else
        {
            nfaStat = NFA_EeSetDefaultProtoRouting(defaultProtoSeID,
                                                    (defaultProtoPowerstate& 01) ? NFA_PROTOCOL_MASK_ISO_DEP: 0,
                                                    (defaultProtoPowerstate & 02) ? NFA_PROTOCOL_MASK_ISO_DEP :0,
                                                    (defaultProtoPowerstate & 04) ? NFA_PROTOCOL_MASK_ISO_DEP :0
                                                  );
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
            {
                 ALOGE ("Fail to set  iso7816 routing");
            }
        }
    }

    ALOGD ("%s: defaultHandle:%x", fn, defaultHandle);
    ALOGD ("%s: preferred_defaultHandle:%x", fn, preferred_defaultHandle);

    {
        unsigned long max_tech_mask = 0x03;
        max_tech_mask = SecureElement::getInstance().getSETechnology(defaultTechSeID);
        unsigned int default_tech_power_mask[3]={0,};
        unsigned int defaultTechFPowerstate=0x07;

        ALOGD ("%s: defaultTechSeID:%x", fn, defaultTechSeID);

        if(defaultTechSeID == 0x402)
        {
            for(int pCount=0 ; pCount< 3 ;pCount++)
            {
                if((defaultTechAPowerstate >> pCount)&0x01)
                {
                    default_tech_power_mask[pCount] |= (NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B);
                }

                if((defaultTechFPowerstate >> pCount)&0x01)
                {
                    default_tech_power_mask[pCount] |= NFA_TECHNOLOGY_MASK_F;
                }
            }

            if(mHostListnEnable == TRUE && mFwdFuntnEnable == TRUE)
            {
                if((max_tech_mask != 0x01) && (max_tech_mask == 0x02))
                {
                    nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                           NFA_TECHNOLOGY_MASK_A,
                                                           0,
                                                           0);
                    if (nfaStat == NFA_STATUS_OK)
                       mRoutingEvent.wait ();
                    else
                    {
                        ALOGE ("Fail to set tech routing");
                    }
                }
                else if((max_tech_mask == 0x01) && (max_tech_mask != 0x02))
                {
                    nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                           NFA_TECHNOLOGY_MASK_B,
                                                           0,
                                                           0);
                    if (nfaStat == NFA_STATUS_OK)
                       mRoutingEvent.wait ();
                    else
                    {
                        ALOGE ("Fail to set tech routing");
                    }
                }
            }


            nfaStat =  NFA_EeSetDefaultTechRouting (defaultTechSeID,
                                                (max_tech_mask & default_tech_power_mask[0]),
                                                (max_tech_mask & default_tech_power_mask[1]),
                                                (max_tech_mask & default_tech_power_mask[2]));
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
            {
                ALOGE ("Fail to set tech routing");
            }

        }
        else
        {
            DefaultTechType &= ~NFA_TECHNOLOGY_MASK_F;
            DefaultTechType |= (NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B);
            nfaStat =  NFA_EeSetDefaultTechRouting (defaultTechSeID,
                                                   (defaultTechAPowerstate& 01) ?  (max_tech_mask & DefaultTechType): 0,
                                                   (defaultTechAPowerstate & 02) ? (max_tech_mask & DefaultTechType) :0,
                                                   (defaultTechAPowerstate & 04) ? (max_tech_mask & DefaultTechType) :0);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
            {
                ALOGE ("Fail to set  tech routing");
            }

            max_tech_mask = SecureElement::getInstance().getSETechnology(0x402);
            nfaStat =  NFA_EeSetDefaultTechRouting (0x402,
                                                   (defaultTechFPowerstate& 01) ?  (max_tech_mask & NFA_TECHNOLOGY_MASK_F): 0,
                                                   (defaultTechFPowerstate & 02) ? (max_tech_mask & NFA_TECHNOLOGY_MASK_F) :0,
                                                   (defaultTechFPowerstate & 04) ? (max_tech_mask & NFA_TECHNOLOGY_MASK_F) :0);

            if (nfaStat == NFA_STATUS_OK)
               mRoutingEvent.wait ();
            else
            {
                ALOGE ("Fail to set tech routing");
            }

            if(mHostListnEnable == TRUE && mFwdFuntnEnable == TRUE)
            {
                if((max_tech_mask != 0x01) && (max_tech_mask == 0x02))
                {
                    nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                            NFA_TECHNOLOGY_MASK_A,
                                                            0,
                                                            0);
                    if (nfaStat == NFA_STATUS_OK)
                        mRoutingEvent.wait ();
                    else
                    {
                        ALOGE ("Fail to set  tech routing");
                    }
                }
                else if((max_tech_mask == 0x01) && (max_tech_mask != 0x02))
                {
                    nfaStat =  NFA_EeSetDefaultTechRouting (0x400,
                                                            NFA_TECHNOLOGY_MASK_B,
                                                            0,
                                                            0);
                    if (nfaStat == NFA_STATUS_OK)
                        mRoutingEvent.wait ();
                    else
                    {
                        ALOGE ("Fail to set  tech routing");
                    }
                }
            }

        }

    }


    if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &uiccListenTech, sizeof(uiccListenTech))))
    {
        ALOGD ("%s:UICC_TECH_MASK=0x0%lu;", __FUNCTION__, uiccListenTech);
    }

    if((defaultHandle != NFA_HANDLE_INVALID)  &&  (0 != uiccListenTech))
    {
         {
             SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
             nfaStat = NFA_CeConfigureUiccListenTech (defaultHandle, 0x00);
             if (nfaStat == NFA_STATUS_OK)
             {
                 SecureElement::getInstance().mUiccListenEvent.wait ();
             }
             else
                 ALOGE ("fail to start UICC listen");
         }
         {
             SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
             nfaStat = NFA_CeConfigureUiccListenTech (defaultHandle, (uiccListenTech & 0x07));
             if(nfaStat == NFA_STATUS_OK)
             {
                 SecureElement::getInstance().mUiccListenEvent.wait ();
             }
             else
                 ALOGE ("fail to start UICC listen");
         }
    }
        return true;
}
#endif

//TODO:
void RoutingManager::enableRoutingToHost()
{
    tNFA_STATUS nfaStat;

    {
        SyncEventGuard guard (mRoutingEvent);

        // Route Nfc-A to host if we don't have a SE
        if (mSeTechMask == 0)
        {
            nfaStat = NFA_EeSetDefaultTechRouting (mDefaultEe, NFA_TECHNOLOGY_MASK_A, 0, 0);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
                ALOGE ("Fail to set default tech routing");
        }

        // Default routing for IsoDep protocol
        nfaStat = NFA_EeSetDefaultProtoRouting(mDefaultEe, NFA_PROTOCOL_MASK_ISO_DEP, 0, 0);
        if (nfaStat == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
            ALOGE ("Fail to set default proto routing");
    }
}

//TODO:
void RoutingManager::disableRoutingToHost()
{
    tNFA_STATUS nfaStat;

    {
        SyncEventGuard guard (mRoutingEvent);
        // Default routing for NFC-A technology if we don't have a SE
        if (mSeTechMask == 0)
        {
            nfaStat = NFA_EeSetDefaultTechRouting (mDefaultEe, 0, 0, 0);
            if (nfaStat == NFA_STATUS_OK)
                mRoutingEvent.wait ();
            else
                ALOGE ("Fail to set default tech routing");
        }

        // Default routing for IsoDep protocol
        nfaStat = NFA_EeSetDefaultProtoRouting(mDefaultEe, 0, 0, 0);
        if (nfaStat == NFA_STATUS_OK)
            mRoutingEvent.wait ();
        else
            ALOGE ("Fail to set default proto routing");
    }
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
bool RoutingManager::addAidRouting(const UINT8* aid, UINT8 aidLen, int route, int power, bool isprefix)
#else
bool RoutingManager::addAidRouting(const UINT8* aid, UINT8 aidLen, int route)
#endif
{
    static const char fn [] = "RoutingManager::addAidRouting";
    ALOGD ("%s: enter", fn);
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    tNFA_HANDLE handle;
    tNFA_HANDLE current_handle;
    ALOGD ("%s: enter, route:%x power:0x%x isprefix:%x", fn, route, power, isprefix);
    if (route == 0)
    {
        handle = NFA_EE_HANDLE_DH;
    }
    else
    {
        handle = SecureElement::getInstance().getEseHandleFromGenericId(route);
    }

    ALOGD ("%s: enter, route:%x", fn, handle);
    if (handle  == NFA_HANDLE_INVALID)
    {
        return false;
    }

    current_handle = ((handle == 0x4C0)?0x01:0x02);
    if(handle == 0x400)
        current_handle = 0x00;

    ALOGD ("%s: enter, mDefaultEe:%x", fn, current_handle);

    if (mCeRouteStrictDisable  == 0x01)
    {
        if (handle != 0x400)
            power |= (power | 0xC0);
    }
    //SecureElement::getInstance().activate(current_handle);
    if ((power&0x0F) == NFA_EE_PWR_STATE_NONE)
    {
        power |= NFA_EE_PWR_STATE_ON;
    }
    SyncEventGuard guard(SecureElement::getInstance().mAidAddRemoveEvent);
    UINT8 vs_info = 0x00;
    if(isprefix) {
        vs_info = NFA_EE_AE_NXP_PREFIX_MATCH;
    }
    tNFA_STATUS nfaStat = NFA_EeAddAidRouting(handle, aidLen, (UINT8*) aid, power, vs_info);
#else
    tNFA_STATUS nfaStat = NFA_EeAddAidRouting(route, aidLen, (UINT8*) aid, 0x01);
#endif
    if (nfaStat == NFA_STATUS_OK)
    {
        ALOGD ("%s: routed AID", fn);
        mIsDirty = true;
        SecureElement::getInstance().mAidAddRemoveEvent.wait();
        return true;
    } else
    {
        ALOGE ("%s: failed to route AID", fn);
        return false;
    }
}

void RoutingManager::clearAidRouting()
{
    static const char fn [] = "RoutingManager::clearAidRouting";
    ALOGD ("%s: enter", fn);
    SyncEventGuard guard(SecureElement::getInstance().mAidAddRemoveEvent);
    tNFA_STATUS nfaStat = NFA_EeRemoveAidRouting(NFA_REMOVE_ALL_AID_LEN, NFA_REMOVE_ALL_AID);
    if (nfaStat == NFA_STATUS_OK)
    {
        SecureElement::getInstance().mAidAddRemoveEvent.wait();
    }
    else
    {
        ALOGE ("%s: failed to clear AID", fn);
    }
    mIsDirty = true;
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
void RoutingManager::setDefaultTechRouting (int seId, int tech_switchon,int tech_switchoff)
{
    ALOGD ("ENTER setDefaultTechRouting");
    tNFA_STATUS nfaStat;
    SyncEventGuard guard (mRoutingEvent);

    nfaStat = NFA_EeSetDefaultTechRouting (seId, tech_switchon, tech_switchoff, 0);
    if(nfaStat == NFA_STATUS_OK){
        mRoutingEvent.wait ();
        ALOGD ("tech routing SUCCESS");
    }
    else{
        ALOGE ("Fail to set default tech routing");
    }
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK){
        ALOGE("Failed to commit routing configuration");
    }
}

void RoutingManager::setDefaultProtoRouting (int seId, int proto_switchon,int proto_switchoff)
{
    tNFA_STATUS nfaStat;
    ALOGD ("ENTER setDefaultProtoRouting");
    SyncEventGuard guard (mRoutingEvent);

    nfaStat = NFA_EeSetDefaultProtoRouting (seId, proto_switchon, proto_switchoff, 0);
    if(nfaStat == NFA_STATUS_OK){
        mRoutingEvent.wait ();
        ALOGD ("proto routing SUCCESS");
    }
    else{
        ALOGE ("Fail to set default proto routing");
    }
    nfaStat = NFA_EeUpdateNow();
    if (nfaStat != NFA_STATUS_OK){
        ALOGE("Failed to commit routing configuration");
    }
}
#endif

bool RoutingManager::commitRouting()
{
    static const char fn [] = "RoutingManager::commitRouting";
    tNFA_STATUS nfaStat = 0;
    ALOGD ("%s", fn);
    {
        SyncEventGuard guard (mEeUpdateEvent);
        nfaStat = NFA_EeUpdateNow();
        if (nfaStat == NFA_STATUS_OK)
        {
            mEeUpdateEvent.wait (); //wait for NFA_EE_UPDATED_EVT
        }
    }
    return (nfaStat == NFA_STATUS_OK);
}

void RoutingManager::onNfccShutdown ()
{
    static const char fn [] = "RoutingManager:onNfccShutdown";
    if (mActiveSe == 0x00) return;

    tNFA_STATUS nfaStat = NFA_STATUS_FAILED;
    UINT8 actualNumEe = MAX_NUM_EE;
    tNFA_EE_INFO eeInfo[MAX_NUM_EE];

    memset (&eeInfo, 0, sizeof(eeInfo));
    if ((nfaStat = NFA_EeGetInfo (&actualNumEe, eeInfo)) != NFA_STATUS_OK)
    {
        ALOGE ("%s: fail get info; error=0x%X", fn, nfaStat);
        return;
    }
    if (actualNumEe != 0)
    {
        for (UINT8 xx = 0; xx < actualNumEe; xx++)
        {
            if ((eeInfo[xx].num_interface != 0)
                && (eeInfo[xx].ee_interface[0] != NCI_NFCEE_INTERFACE_HCI_ACCESS)
                && (eeInfo[xx].ee_status == NFA_EE_STATUS_ACTIVE))
            {
                ALOGD ("%s: Handle: 0x%04x Change Status Active to Inactive", fn, eeInfo[xx].ee_handle);
                SyncEventGuard guard (mEeSetModeEvent);
                if ((nfaStat = NFA_EeModeSet (eeInfo[xx].ee_handle, NFA_EE_MD_DEACTIVATE)) == NFA_STATUS_OK)
                {
                    mEeSetModeEvent.wait (); //wait for NFA_EE_MODE_SET_EVT
                }
                else
                {
                    ALOGE ("Failed to set EE inactive");
                }
            }
        }
    }
    else
    {
        ALOGD ("%s: No active EEs found", fn);
    }
}

void RoutingManager::notifyActivated ()
{
    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyHostEmuActivated);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail notify");
    }
}

void RoutingManager::notifyDeactivated ()
{
    SecureElement::getInstance().notifyListenModeState (false);
    mRxDataBuffer.clear();
    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyHostEmuDeactivated);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail notify");
    }
}

void RoutingManager::notifyLmrtFull ()
{
    JNIEnv* e = NULL;
    ScopedAttach attach(mNativeData->vm, &e);
    if (e == NULL)
    {
        ALOGE ("jni env is null");
        return;
    }

    e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyAidRoutingTableFull);
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("fail notify");
    }
}

void RoutingManager::handleData (const UINT8* data, UINT32 dataLen, tNFA_STATUS status)
{
    if (dataLen <= 0)
    {
        ALOGE("no data");
        goto TheEnd;
    }

    if (status == NFA_STATUS_CONTINUE)
    {
        ALOGE ("jni env is null");
        mRxDataBuffer.insert (mRxDataBuffer.end(), &data[0], &data[dataLen]); //append data; more to come
        return; //expect another NFA_CE_DATA_EVT to come
    }
    else if (status == NFA_STATUS_OK)
    {
        mRxDataBuffer.insert (mRxDataBuffer.end(), &data[0], &data[dataLen]); //append data
        //entire data packet has been received; no more NFA_CE_DATA_EVT
    }
    else if (status == NFA_STATUS_FAILED)
    {
        ALOGE("RoutingManager::handleData: read data fail");
        goto TheEnd;
    }
    {
        JNIEnv* e = NULL;
        ScopedAttach attach(mNativeData->vm, &e);
        if (e == NULL)
        {
            ALOGE ("jni env is null");
            goto TheEnd;
        }

        ScopedLocalRef<jobject> dataJavaArray(e, e->NewByteArray(mRxDataBuffer.size()));
        if (dataJavaArray.get() == NULL)
        {
            ALOGE ("fail allocate array");
            goto TheEnd;
        }

        e->SetByteArrayRegion ((jbyteArray)dataJavaArray.get(), 0, mRxDataBuffer.size(),
                (jbyte *)(&mRxDataBuffer[0]));
        if (e->ExceptionCheck())
        {
            e->ExceptionClear();
            ALOGE ("fail fill array");
            goto TheEnd;
        }

        e->CallVoidMethod (mNativeData->manager, android::gCachedNfcManagerNotifyHostEmuData, dataJavaArray.get());
        if (e->ExceptionCheck())
        {
            e->ExceptionClear();
            ALOGE ("fail notify");
        }

    }
TheEnd:
    mRxDataBuffer.clear();
}

void RoutingManager::stackCallback (UINT8 event, tNFA_CONN_EVT_DATA* eventData)
{
    static const char fn [] = "RoutingManager::stackCallback";
    ALOGD("%s: event=0x%X", fn, event);
    RoutingManager& routingManager = RoutingManager::getInstance();

    switch (event)
    {
    case NFA_CE_REGISTERED_EVT:
        {
            tNFA_CE_REGISTERED& ce_registered = eventData->ce_registered;
            ALOGD("%s: NFA_CE_REGISTERED_EVT; status=0x%X; h=0x%X", fn, ce_registered.status, ce_registered.handle);
        }
        break;

    case NFA_CE_DEREGISTERED_EVT:
        {
            tNFA_CE_DEREGISTERED& ce_deregistered = eventData->ce_deregistered;
            ALOGD("%s: NFA_CE_DEREGISTERED_EVT; h=0x%X", fn, ce_deregistered.handle);
        }
        break;

    case NFA_CE_ACTIVATED_EVT:
        {
            android::checkforTranscation(NFA_CE_ACTIVATED_EVT, (void *)eventData);
            routingManager.notifyActivated();
        }
        break;
    case NFA_DEACTIVATED_EVT:
    case NFA_CE_DEACTIVATED_EVT:
        {
            android::checkforTranscation(NFA_CE_DEACTIVATED_EVT, (void *)eventData);
            routingManager.notifyDeactivated();
            SyncEventGuard g (gDeactivatedEvent);
            gActivated = false; //guard this variable from multi-threaded access
            gDeactivatedEvent.notifyOne ();
        }
        break;

    case NFA_CE_DATA_EVT:
        {
            tNFA_CE_DATA& ce_data = eventData->ce_data;
            ALOGD("%s: NFA_CE_DATA_EVT; stat=0x%X; h=0x%X; data len=%u", fn, ce_data.status, ce_data.handle, ce_data.len);
            getInstance().handleData(ce_data.p_data, ce_data.len, ce_data.status);
        }
        break;
    }
}
/*******************************************************************************
**
** Function:        nfaEeCallback
**
** Description:     Receive execution environment-related events from stack.
**                  event: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
void RoutingManager::nfaEeCallback (tNFA_EE_EVT event, tNFA_EE_CBACK_DATA* eventData)
{
    static const char fn [] = "RoutingManager::nfaEeCallback";

    SecureElement& se = SecureElement::getInstance();
    RoutingManager& routingManager = RoutingManager::getInstance();
    tNFA_EE_DISCOVER_REQ info = eventData->discover_req;

    switch (event)
    {
    case NFA_EE_REGISTER_EVT:
        {
            SyncEventGuard guard (routingManager.mEeRegisterEvent);
            ALOGD ("%s: NFA_EE_REGISTER_EVT; status=%u", fn, eventData->ee_register);
            routingManager.mEeRegisterEvent.notifyOne();
        }
        break;

    case NFA_EE_MODE_SET_EVT:
        {
            SyncEventGuard guard (routingManager.mEeSetModeEvent);
            ALOGD ("%s: NFA_EE_MODE_SET_EVT; status: 0x%04X  handle: 0x%04X  mActiveEeHandle: 0x%04X", fn,
                    eventData->mode_set.status, eventData->mode_set.ee_handle, se.mActiveEeHandle);
            routingManager.mEeSetModeEvent.notifyOne();
            se.notifyModeSet(eventData->mode_set.ee_handle, !(eventData->mode_set.status),eventData->mode_set.ee_status );
        }
        break;

    case NFA_EE_SET_TECH_CFG_EVT:
        {
            ALOGD ("%s: NFA_EE_SET_TECH_CFG_EVT; status=0x%X", fn, eventData->status);
            SyncEventGuard guard(routingManager.mRoutingEvent);
            routingManager.mRoutingEvent.notifyOne();
        }
        break;

    case NFA_EE_SET_PROTO_CFG_EVT:
        {
            ALOGD ("%s: NFA_EE_SET_PROTO_CFG_EVT; status=0x%X", fn, eventData->status);
            SyncEventGuard guard(routingManager.mRoutingEvent);
            routingManager.mRoutingEvent.notifyOne();
        }
        break;

    case NFA_EE_ACTION_EVT:
        {
            tNFA_EE_ACTION& action = eventData->action;
            android::checkforTranscation(NFA_EE_ACTION_EVT, (void *)eventData);
            if (action.trigger == NFC_EE_TRIG_SELECT)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=select (0x%X)", fn, action.ee_handle, action.trigger);
            else if (action.trigger == NFC_EE_TRIG_APP_INIT)
            {
                tNFC_APP_INIT& app_init = action.param.app_init;
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=app-init (0x%X); aid len=%u; data len=%u", fn,
                        action.ee_handle, action.trigger, app_init.len_aid, app_init.len_data);
                //if app-init operation is successful;
                //app_init.data[] contains two bytes, which are the status codes of the event;
                //app_init.data[] does not contain an APDU response;
                //see EMV Contactless Specification for Payment Systems; Book B; Entry Point Specification;
                //version 2.1; March 2011; section 3.3.3.5;
                if ( (app_init.len_data > 1) &&
                     (app_init.data[0] == 0x90) &&
                     (app_init.data[1] == 0x00) )
                {
                    se.notifyTransactionListenersOfAid (app_init.aid, app_init.len_aid, app_init.data, app_init.len_data, SecureElement::getInstance().getGenericEseId(action.ee_handle));
                }
            }
            else if (action.trigger == NFC_EE_TRIG_RF_PROTOCOL)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=rf protocol (0x%X)", fn, action.ee_handle, action.trigger);
            else if (action.trigger == NFC_EE_TRIG_RF_TECHNOLOGY)
                ALOGD ("%s: NFA_EE_ACTION_EVT; h=0x%X; trigger=rf tech (0x%X)", fn, action.ee_handle, action.trigger);
            else
                ALOGE ("%s: NFA_EE_ACTION_EVT; h=0x%X; unknown trigger (0x%X)", fn, action.ee_handle, action.trigger);
        }
        break;

    case NFA_EE_DISCOVER_EVT:
        {
            UINT8 num_ee = eventData->ee_discover.num_ee;
            tNFA_EE_DISCOVER ee_disc_info = eventData->ee_discover;
            ALOGD ("%s: NFA_EE_DISCOVER_EVT; status=0x%X; num ee=%u", __FUNCTION__,eventData->status, eventData->ee_discover.num_ee);

            if(mChipId == 0x02 || mChipId == 0x04)
            {
                for(int xx = 0; xx <  num_ee; xx++)
                {
                    ALOGE("xx=%d, ee_handle=0x0%x, status=0x0%x", xx, ee_disc_info.ee_info[xx].ee_handle,ee_disc_info.ee_info[xx].ee_status);
                    if ((ee_disc_info.ee_info[xx].ee_handle == 0x4C0) &&
                            (ee_disc_info.ee_info[xx].ee_status == 0x02))
                    {
                        recovery=TRUE;
                        routingManager.ee_removed_disc_ntf_handler(ee_disc_info.ee_info[xx].ee_handle, ee_disc_info.ee_info[xx].ee_status);
                        break;
                    }
                }
            }
        }
        break;

    case NFA_EE_DISCOVER_REQ_EVT:
        ALOGD ("%s: NFA_EE_DISCOVER_REQ_EVT; status=0x%X; num ee=%u", __FUNCTION__,
                eventData->discover_req.status, eventData->discover_req.num_ee);
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
        /* Handle Reader over SWP.
         * 1. Check if the event is for Reader over SWP.
         * 2. IF yes than send this info(READER_REQUESTED_EVENT) till FWK level.
         * 3. Stop the discovery.
         * 4. MAP the proprietary interface for Reader over SWP.NFC_DiscoveryMap, nfc_api.h
         * 5. start the discovery with reader req, type and DH configuration.
         *
         * 6. IF yes than send this info(STOP_READER_EVENT) till FWK level.
         * 7. MAP the DH interface for Reader over SWP. NFC_DiscoveryMap, nfc_api.h
         * 8. start the discovery with DH configuration.
         */
        for (UINT8 xx = 0; xx < info.num_ee; xx++)
        {
            //for each technology (A, B, F, B'), print the bit field that shows
            //what protocol(s) is support by that technology
            ALOGD ("%s   EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                    fn, xx, info.ee_disc_info[xx].ee_handle,
                    info.ee_disc_info[xx].pa_protocol,
                    info.ee_disc_info[xx].pb_protocol);

            if(info.ee_disc_info[xx].pa_protocol ==  0x04 || info.ee_disc_info[xx].pb_protocol == 0x04)
            {
                ALOGD ("%s NFA_RD_SWP_READER_REQUESTED  EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                        fn, xx, info.ee_disc_info[xx].ee_handle,
                        info.ee_disc_info[xx].pa_protocol,
                        info.ee_disc_info[xx].pb_protocol);

                rd_swp_req_t *data = (rd_swp_req_t*)malloc(sizeof(rd_swp_req_t));
        if (data == NULL) {
            return;
        }

                data->tech_mask = 0x00;
                if(info.ee_disc_info[xx].pa_protocol !=  0)
                    data->tech_mask |= NFA_TECHNOLOGY_MASK_A;
                if(info.ee_disc_info[xx].pb_protocol !=  0)
                    data->tech_mask |= NFA_TECHNOLOGY_MASK_B;

                data->src = info.ee_disc_info[xx].ee_handle;
                pthread_t thread;
                pthread_create (&thread, NULL,  &reader_event_handler_async, (void*)data);
                //Reader over SWP - Reader Requested.
                //se.handleEEReaderEvent(NFA_RD_SWP_READER_REQUESTED, tech, info.ee_disc_info[xx].ee_handle);
                break;
            }
            else if(info.ee_disc_info[xx].pa_protocol ==  0xFF || info.ee_disc_info[xx].pb_protocol == 0xFF)
            {
                ALOGD ("%s NFA_RD_SWP_READER_STOP  EE[%u] Handle: 0x%04x  PA: 0x%02x  PB: 0x%02x",
                        fn, xx, info.ee_disc_info[xx].ee_handle,
                        info.ee_disc_info[xx].pa_protocol,
                        info.ee_disc_info[xx].pb_protocol);
                rd_swp_req_t *data = (rd_swp_req_t*)malloc(sizeof(rd_swp_req_t));
        if (data == NULL) {
            return;
        }
                data->tech_mask = 0x00;
                data->src = info.ee_disc_info[xx].ee_handle;

                //Reader over SWP - Stop Reader Requested.
                //se.handleEEReaderEvent(NFA_RD_SWP_READER_STOP, 0x00,info.ee_disc_info[xx].ee_handle);
                pthread_t thread;
                pthread_create (&thread, NULL,  &stop_reader_event_handler_async, (void*)data);
                break;
            }
        }
        /*Set the configuration for UICC/ESE */
        se.storeUiccInfo (eventData->discover_req);
#else
        {
            ALOGD ("%s: NFA_EE_DISCOVER_REQ_EVT; status=0x%X; num ee=%u", __FUNCTION__,
                    eventData->discover_req.status, eventData->discover_req.num_ee);
            SyncEventGuard guard (routingManager.mEeInfoEvent);
            memcpy (&routingManager.mEeInfo, &eventData->discover_req, sizeof(routingManager.mEeInfo));
            routingManager.mReceivedEeInfo = true;
            routingManager.mEeInfoEvent.notifyOne();
        }
#endif
        break;

    case NFA_EE_NO_CB_ERR_EVT:
        ALOGD ("%s: NFA_EE_NO_CB_ERR_EVT  status=%u", fn, eventData->status);
        break;

    case NFA_EE_ADD_AID_EVT:
        {
            ALOGD ("%s: NFA_EE_ADD_AID_EVT  status=%u", fn, eventData->status);
            if(eventData->status == NFA_STATUS_BUFFER_FULL)
            {
                ALOGD ("%s: AID routing table is FULL!!!", fn);
                RoutingManager::getInstance().notifyLmrtFull();
            }
            SyncEventGuard guard(se.mAidAddRemoveEvent);
            se.mAidAddRemoveEvent.notifyOne();
        }
        break;

    case NFA_EE_REMOVE_AID_EVT:
        {
            ALOGD ("%s: NFA_EE_REMOVE_AID_EVT  status=%u", fn, eventData->status);
            SyncEventGuard guard(se.mAidAddRemoveEvent);
            se.mAidAddRemoveEvent.notifyOne();
        }
        break;

    case NFA_EE_NEW_EE_EVT:
        {
            ALOGD ("%s: NFA_EE_NEW_EE_EVT  h=0x%X; status=%u", fn,
                eventData->new_ee.ee_handle, eventData->new_ee.ee_status);
        }
        break;
    case NFA_EE_ROUT_ERR_EVT:
        {
            ALOGD ("%s: NFA_EE_ROUT_ERR_EVT  status=%u", fn,eventData->status);
        }
        break;
    case NFA_EE_UPDATED_EVT:
        {
            ALOGD("%s: NFA_EE_UPDATED_EVT", fn);
            SyncEventGuard guard(routingManager.mEeUpdateEvent);
            routingManager.mEeUpdateEvent.notifyOne();
        }
        break;
    default:
        ALOGE ("%s: unknown event=%u ????", fn, event);
        break;
    }
}

int RoutingManager::registerJniFunctions (JNIEnv* e)
{
    static const char fn [] = "RoutingManager::registerJniFunctions";
    ALOGD ("%s", fn);
    return jniRegisterNativeMethods (e, "com/android/nfc/cardemulation/AidRoutingManager", sMethods, NELEM(sMethods));
}

int RoutingManager::com_android_nfc_cardemulation_doGetDefaultRouteDestination (JNIEnv*)
{
    return getInstance().mDefaultEe;
}

int RoutingManager::com_android_nfc_cardemulation_doGetDefaultOffHostRouteDestination (JNIEnv*)
{
    return getInstance().mOffHostEe;
}

int RoutingManager::com_android_nfc_cardemulation_doGetAidMatchingMode (JNIEnv*)
{
    return getInstance().mAidMatchingMode;
}
int RoutingManager::com_android_nfc_cardemulation_doGetAidMatchingPlatform(JNIEnv*)
{
    return getInstance().mAidMatchingPlatform;
}
void *reader_event_handler_async(void *data)
{
    rd_swp_req_t *cbData = (rd_swp_req_t *) data;
    SecureElement::getInstance().handleEEReaderEvent(NFA_RD_SWP_READER_REQUESTED, cbData->tech_mask, cbData->src);
    free(cbData);

    return NULL;
}

void *stop_reader_event_handler_async(void *data)
{
    rd_swp_req_t *cbData = (rd_swp_req_t *) data;
    SecureElement::getInstance().handleEEReaderEvent(NFA_RD_SWP_READER_STOP, cbData->tech_mask, cbData->src);
    free(cbData);
    return NULL;
}
void *ee_removed_ntf_handler_thread(void *data)
{
    static const char fn [] = "ee_removed_ntf_handler_thread";
    SecureElement &se = SecureElement::getInstance();
    ALOGD ("%s:  ", fn);
    se.SecEle_Modeset(0x00);
    usleep(10*1000);
    se.SecEle_Modeset(0x01);
    usleep(10*1000);
    SyncEventGuard guard(se.mEEdatapacketEvent);
    recovery=FALSE;
    se.mEEdatapacketEvent.notifyOne();
    return NULL;
}
void RoutingManager::ee_removed_disc_ntf_handler(tNFA_HANDLE handle, tNFA_EE_STATUS status)
{
    static const char fn [] = "RoutingManager::ee_disc_ntf_handler";
    pthread_t thread;
    if (pthread_create (&thread, NULL,  &ee_removed_ntf_handler_thread, (void*)NULL) < 0)
    {
        ALOGD("Thread creation failed");
    }
    else
    {
        ALOGD("Thread creation success");
    }
}

bool RoutingManager::is_ee_recovery_ongoing()
{
    ALOGD("is_ee_recovery_ongoing : recovery");
    if(recovery)
        return true;
    else
        return false;
}
