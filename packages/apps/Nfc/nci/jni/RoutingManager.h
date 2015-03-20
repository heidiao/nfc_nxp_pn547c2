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
#pragma once
#include "SyncEvent.h"
#include "NfcJniUtil.h"
#include "RouteDataSet.h"
#include "SecureElement.h"
#include <vector>
extern "C"
{
    #include "nfa_api.h"
    #include "nfa_ee_api.h"
}

class RoutingManager
{
public:
    static const int ROUTE_HOST = 0;
    static const int ROUTE_ESE = 1;
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    int mHostListnEnable;
    int mFwdFuntnEnable;
#endif
    static RoutingManager& getInstance ();
    bool initialize(nfc_jni_native_data* native);
    void enableRoutingToHost();
    void disableRoutingToHost();
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    void setDefaultTechRouting (int seId, int tech_switchon,int tech_switchoff);
    void setDefaultProtoRouting (int seId, int proto_switchon,int proto_switchoff);
#endif
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    bool addAidRouting(const UINT8* aid, UINT8 aidLen, int route, int power, bool isprefix);
#else
    bool addAidRouting(const UINT8* aid, UINT8 aidLen, int route);
#endif
    void clearAidRouting();
    bool commitRouting();
    void onNfccShutdown();
    int registerJniFunctions (JNIEnv* e);
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    void setRouting(bool);
    bool setDefaultRoute(const UINT8 defaultRoute, const UINT8 protoRoute, const UINT8 techRoute);
#endif
    void ee_removed_disc_ntf_handler(tNFA_HANDLE handle, tNFA_EE_STATUS status);
    bool is_ee_recovery_ongoing();
private:
    RoutingManager();
    ~RoutingManager();
    RoutingManager(const RoutingManager&);
    RoutingManager& operator=(const RoutingManager&);

    void cleanRouting();
    void handleData (const UINT8* data, UINT32 dataLen, tNFA_STATUS status);
    void notifyActivated ();
    void notifyDeactivated ();
    void notifyLmrtFull();

    // See AidRoutingManager.java for corresponding
    // AID_MATCHING_ constants

    // Every routing table entry is matched exact (BCM20793)
    static const int AID_MATCHING_EXACT_ONLY = 0x00;
    // Every routing table entry can be matched either exact or prefix
    static const int AID_MATCHING_EXACT_OR_PREFIX = 0x01;
    // Every routing table entry is matched as a prefix
    static const int AID_MATCHING_PREFIX_ONLY = 0x02;

    // See AidRoutingManager.java for corresponding
    // AID_MATCHING_ platform constants

    //Behavior as per Android-L, supporting prefix match and full
    //match for both OnHost and OffHost apps.
    static const int AID_MATCHING_L = 0x01;
    //Behavior as per Android-KitKat by NXP, supporting prefix match for
    //OffHost and prefix and full both for OnHost apps.
    static const int AID_MATCHING_K = 0x02;



    static void nfaEeCallback (tNFA_EE_EVT event, tNFA_EE_CBACK_DATA* eventData);
    static void stackCallback (UINT8 event, tNFA_CONN_EVT_DATA* eventData);
    static int com_android_nfc_cardemulation_doGetDefaultRouteDestination (JNIEnv* e);
    static int com_android_nfc_cardemulation_doGetDefaultOffHostRouteDestination (JNIEnv* e);
    static int com_android_nfc_cardemulation_doGetAidMatchingMode (JNIEnv* e);
    static int com_android_nfc_cardemulation_doGetAidMatchingPlatform (JNIEnv* e);

    std::vector<UINT8> mRxDataBuffer;

    // Fields below are final after initialize()
    nfc_jni_native_data* mNativeData;
    int mDefaultEe;
    static int mChipId;
    bool mIsDirty;
    int mOffHostEe;
    int mActiveSe;
    int mAidMatchingMode;
    int mAidMatchingPlatform;
    bool mReceivedEeInfo;
    tNFA_EE_DISCOVER_REQ mEeInfo;
    tNFA_TECHNOLOGY_MASK mSeTechMask;
    static const JNINativeMethod sMethods [];
    SyncEvent mEeRegisterEvent;
    SyncEvent mRoutingEvent;
    SyncEvent mEeUpdateEvent;
    SyncEvent mEeInfoEvent;
    SyncEvent mEeSetModeEvent;
    int defaultSeID ;
    int defaultPowerstate;
    int defaultProtoSeID;
    int defaultProtoPowerstate;
    int defaultTechSeID;
    int defaultTechAPowerstate;
    int DefaultTechType;
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    UINT32 mCeRouteStrictDisable;
#endif
};
