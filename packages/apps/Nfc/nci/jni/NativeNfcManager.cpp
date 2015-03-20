/*
 * Copyright (C) 2012 The Android Open Source Project
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
 *  Copyright (C) 2013-2014 NXP Semiconductors
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
#include <semaphore.h>
#include <errno.h>
#include "OverrideLog.h"
#include "NfcJniUtil.h"
#include "NfcAdaptation.h"
#include "SyncEvent.h"
#include "PeerToPeer.h"
#include "SecureElement.h"
#include "RoutingManager.h"
#include "NfcTag.h"
#include "config.h"
#include "PowerSwitch.h"
#include "JavaClassConstants.h"
#include "Pn544Interop.h"
#include <ScopedLocalRef.h>
#include <ScopedUtfChars.h>
#include <sys/time.h>
#include "HciRFParams.h"
#include <pthread.h>
#include <ScopedPrimitiveArray.h>
#include "DwpChannel.h"
extern "C"
{
    #include "nfa_api.h"
    #include "nfa_p2p_api.h"
    #include "rw_api.h"
    #include "nfa_ee_api.h"
    #include "nfc_brcm_defs.h"
    #include "ce_api.h"
    #include "phNxpExtns.h"
    #include "phNxpConfig.h"
#ifdef NFC_NXP_P61
    #include "JcDnld.h"
    #include "IChannel.h"
#endif
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
#define UICC_HANDLE   0x402
#define ESE_HANDLE    0x4C0
#define RETRY_COUNT   10
#define default_count 3
#endif

extern const UINT8 nfca_version_string [];
extern const UINT8 nfa_version_string [];
extern tNFA_DM_DISC_FREQ_CFG* p_nfa_dm_rf_disc_freq_cfg; //defined in stack
namespace android
{
    extern bool gIsTagDeactivating;
    extern bool gIsSelectingRfInterface;
    extern void nativeNfcTag_doTransceiveStatus (tNFA_STATUS status, uint8_t * buf, uint32_t buflen);
    extern void nativeNfcTag_notifyRfTimeout ();
    extern void nativeNfcTag_doConnectStatus (jboolean is_connect_ok);
    extern void nativeNfcTag_doDeactivateStatus (int status);
    extern void nativeNfcTag_doWriteStatus (jboolean is_write_ok);
    extern void nativeNfcTag_doCheckNdefResult (tNFA_STATUS status, uint32_t max_size, uint32_t current_size, uint8_t flags);
    extern void nativeNfcTag_doMakeReadonlyResult (tNFA_STATUS status);
    extern void nativeNfcTag_doPresenceCheckResult (tNFA_STATUS status);
    extern void nativeNfcTag_formatStatus (bool is_ok);
    extern void nativeNfcTag_resetPresenceCheck ();
    extern void nativeNfcTag_doReadCompleted (tNFA_STATUS status);
    extern void nativeNfcTag_abortWaits ();
    extern void nativeLlcpConnectionlessSocket_abortWait ();
    extern void nativeNfcTag_registerNdefTypeHandler ();
    extern void nativeLlcpConnectionlessSocket_receiveData (uint8_t* data, uint32_t len, uint32_t remote_sap);
    extern tNFA_STATUS EmvCo_dosetPoll(jboolean enable);
    extern tNFA_STATUS SetDHListenFilter(jboolean enable);
    extern tNFA_STATUS SetVenConfigValue(jint venconfig);
    extern tNFA_STATUS SetScreenState(int state);
    //Factory Test Code --start
    extern tNFA_STATUS Nxp_SelfTest(uint8_t testcase, uint8_t* param);
    extern void SetCbStatus(tNFA_STATUS status);
    extern tNFA_STATUS GetCbStatus(void);
    static void nfaNxpSelfTestNtfTimerCb (union sigval);
    //Factory Test Code --end
    extern bool getReconnectState(void);
}


/*****************************************************************************
**
** public variables and functions
**
*****************************************************************************/
bool                        gActivated = false;
SyncEvent                   gDeactivatedEvent;

namespace android
{
    int                     gGeneralTransceiveTimeout = DEFAULT_GENERAL_TRANS_TIMEOUT;
    int                     gGeneralPowershutDown = 0;
    jmethodID               gCachedNfcManagerNotifyNdefMessageListeners;
    jmethodID               gCachedNfcManagerNotifyTransactionListeners;
    jmethodID               gCachedNfcManagerNotifyConnectivityListeners;
    jmethodID               gCachedNfcManagerNotifyEmvcoMultiCardDetectedListeners;
    jmethodID               gCachedNfcManagerNotifyLlcpLinkActivation;
    jmethodID               gCachedNfcManagerNotifyLlcpLinkDeactivated;
    jmethodID               gCachedNfcManagerNotifyLlcpFirstPacketReceived;
    jmethodID               gCachedNfcManagerNotifySeFieldActivated;
    jmethodID               gCachedNfcManagerNotifySeFieldDeactivated;
    jmethodID               gCachedNfcManagerNotifySeListenActivated;
    jmethodID               gCachedNfcManagerNotifySeListenDeactivated;
    jmethodID               gCachedNfcManagerNotifyHostEmuActivated;
    jmethodID               gCachedNfcManagerNotifyHostEmuData;
    jmethodID               gCachedNfcManagerNotifyHostEmuDeactivated;
    jmethodID               gCachedNfcManagerNotifyRfFieldActivated;
    jmethodID               gCachedNfcManagerNotifyRfFieldDeactivated;
    jmethodID               gCachedNfcManagerNotifySWPReaderRequested;
    jmethodID               gCachedNfcManagerNotifySWPReaderActivated;
    jmethodID               gCachedNfcManagerNotifySWPReaderDeActivated;
    jmethodID               gCachedNfcManagerNotifyAidRoutingTableFull;
    const char*             gNativeP2pDeviceClassName                 = "com/android/nfc/dhimpl/NativeP2pDevice";
    const char*             gNativeLlcpServiceSocketClassName         = "com/android/nfc/dhimpl/NativeLlcpServiceSocket";
    const char*             gNativeLlcpConnectionlessSocketClassName  = "com/android/nfc/dhimpl/NativeLlcpConnectionlessSocket";
    const char*             gNativeLlcpSocketClassName                = "com/android/nfc/dhimpl/NativeLlcpSocket";
    const char*             gNativeNfcTagClassName                    = "com/android/nfc/dhimpl/NativeNfcTag";
    const char*             gNativeNfcManagerClassName                = "com/android/nfc/dhimpl/NativeNfcManager";
    const char*             gNativeNfcSecureElementClassName          = "com/android/nfc/dhimpl/NativeNfcSecureElement";
    const char*             gNativeNfcAlaClassName                    = "com/android/nfc/dhimpl/NativeNfcAla";
    void                    doStartupConfig ();
    void                    startStopPolling (bool isStartPolling);
    void                    startRfDiscovery (bool isStart);
    void                    setUiccIdleTimeout (bool enable);
}


/*****************************************************************************
**
** private variables and functions
**
*****************************************************************************/
namespace android
{
static jint                 sLastError = ERROR_BUFFER_TOO_SMALL;
static jmethodID            sCachedNfcManagerNotifySeApduReceived;
static jmethodID            sCachedNfcManagerNotifySeMifareAccess;
static jmethodID            sCachedNfcManagerNotifySeEmvCardRemoval;
static jmethodID            sCachedNfcManagerNotifyTargetDeselected;
static SyncEvent            sNfaEnableEvent;  //event for NFA_Enable()
static SyncEvent            sNfaDisableEvent;  //event for NFA_Disable()
SyncEvent            sNfaEnableDisablePollingEvent;  //event for NFA_EnablePolling(), NFA_DisablePolling()
SyncEvent                   sNfaSetConfigEvent;  // event for Set_Config....
SyncEvent                   sNfaGetConfigEvent;  // event for Get_Config....

static bool                 sIsNfaEnabled = false;
static bool                 sDiscoveryEnabled = false;  //is polling or listening
static bool                 sPollingEnabled = false;  //is polling for tag?
static bool                 sIsDisabling = false;
static bool                 sRfEnabled = false; // whether RF discovery is enabled
static bool                 sHCEEnabled = true; // whether Host Card Emulation is enabled
static bool                 sForceDiscovery = false; // whether force re-start discovery
static bool                 sSeRfActive = false;  // whether RF with SE is likely active
static bool                 sReaderModeEnabled = false; // whether we're only reading tags, not allowing P2p/card emu
static bool                 sEnableLptd = false; //whether low power mode is enabled.
static bool                 sP2pActive = false; // whether p2p was last active
static bool                 sAbortConnlessWait = false;
static UINT8                sIsSecElemSelected = 0;  //has NFC service selected a sec elem
static UINT8                sIsSecElemDetected = 0;  //has NFC service deselected a sec elem
static bool                 sDiscCmdwhleNfcOff = false;
#define CONFIG_UPDATE_TECH_MASK     (1 << 1)
#define TRANSACTION_TIMER_VALUE     50
#define DEFAULT_TECH_MASK           (NFA_TECHNOLOGY_MASK_A \
                                     | NFA_TECHNOLOGY_MASK_B \
                                     | NFA_TECHNOLOGY_MASK_F \
                                     | NFA_TECHNOLOGY_MASK_ISO15693 \
                                     | NFA_TECHNOLOGY_MASK_B_PRIME \
                                     | NFA_TECHNOLOGY_MASK_A_ACTIVE \
                                     | NFA_TECHNOLOGY_MASK_F_ACTIVE \
                                     | NFA_TECHNOLOGY_MASK_KOVIO)
#define DEFAULT_DISCOVERY_DURATION       500
#define READER_MODE_DISCOVERY_DURATION   200
static int screenstate = 0;
static void nfcManager_doSetScreenState(JNIEnv* e, jobject o, jint state);
static void StoreScreenState(int state);
int getScreenState();
static void nfaConnectionCallback (UINT8 event, tNFA_CONN_EVT_DATA *eventData);
static void nfaDeviceManagementCallback (UINT8 event, tNFA_DM_CBACK_DATA *eventData);
static bool isPeerToPeer (tNFA_ACTIVATED& activated);
static bool isListenMode(tNFA_ACTIVATED& activated);
static void setListenMode();
static void enableDisableLptd (bool enable);
static tNFA_STATUS stopPolling_rfDiscoveryDisabled();
static tNFA_STATUS startPolling_rfDiscoveryDisabled(tNFA_TECHNOLOGY_MASK tech_mask);

static int nfcManager_getChipVer(JNIEnv* e, jobject o);
static int nfcManager_doJcosDownload(JNIEnv* e, jobject o);
static jint nfcManager_getSecureElementTechList(JNIEnv* e, jobject o);
static void nfcManager_setSecureElementListenTechMask(JNIEnv *e, jobject o, jint tech_mask);
static void notifyPollingEventwhileNfcOff();

void nfcManager_doSetVenConfigValue (JNIEnv *e, jobject o, jint venconfig);
void nfcManager_doSetScrnState(JNIEnv *e, jobject o, jint Enable);
#ifdef NFC_NXP_P61
void DWPChannel_init(IChannel_t *DWP);
IChannel_t Dwp;
#endif
static UINT16 sCurrentConfigLen;
static UINT8 sConfig[256];

/*Proprietary cmd sent to HAL to send reader mode flag
 * Last byte of sProprietaryCmdBuf contains ReaderMode flag */
#define PROPRIETARY_CMD_FELICA_READER_MODE 0xFE
static UINT8     sProprietaryCmdBuf[]={0xFE,0xFE,0xFE,0x00};
static void      NxpResponsePropCmd_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);
static int       sTechMask = 0; // Copy of Tech Mask used in doEnableReaderMode
static SyncEvent sRespCbEvent;
static void* T3TPollThread(void *arg);
static bool SwitchP2PToT3TRead();
typedef enum felicaReaderMode_state
{
    STATE_IDLE = 0x00,
    STATE_NFCDEP_ACTIVATED_NFCDEP_INTF,
    STATE_DEACTIVATED_TO_SLEEP,
    STATE_FRAMERF_INTF_SELECTED,
}eFelicaReaderModeState_t;
static eFelicaReaderModeState_t gFelicaReaderState=STATE_IDLE;

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
#ifdef CHECK_FOR_NFCEE_CONFIGURATION
static UINT8 sNfceeConfigured;
static UINT8 sCheckNfceeFlag;
void checkforNfceeBuffer();
void checkforNfceeConfig();
//self test start
static IntervalTimer nfaNxpSelfTestNtfTimer; // notification timer for swp self test
static SyncEvent sNfaNxpNtfEvent;
static void nfaNxpSelfTestNtfTimerCb (union sigval);
static void nfcManager_doSetEEPROM(JNIEnv* e, jobject o, jbyteArray val);
static jint nfcManager_getFwVersion(JNIEnv* e, jobject o);
static jint nfcManager_SWPSelfTest(JNIEnv* e, jobject o, jint ch);
static void nfcManager_doPrbsOff(JNIEnv* e, jobject o);
static void nfcManager_doPrbsOn(JNIEnv* e, jobject o, jint tech, jint rate);
//self test end
#endif
#endif

bool isDiscoveryStarted();

void checkforTranscation(UINT8 connEvent ,void * eventData);
void sig_handler(int signo);
void cleanup_timer();
/* Transaction Events in order */
typedef enum transcation_events
{
    NFA_TRANS_DEFAULT = 0x00,
    NFA_TRANS_ACTIVATED_EVT,
    NFA_TRANS_EE_ACTION_EVT,
    NFA_TRANS_DM_RF_FIELD_EVT,
    NFA_TRANS_DM_RF_FIELD_EVT_ON,
    NFA_TRANS_DM_RF_TRANS_START,
    NFA_TRANS_DM_RF_FIELD_EVT_OFF,
    NFA_TRANS_DM_RF_TRANS_PROGRESS,
    NFA_TRANS_DM_RF_TRANS_END,
    NFA_TRANS_CE_ACTIVATED = 0x18,
    NFA_TRANS_CE_DEACTIVATED = 0x19,
}eTranscation_events_t;

/* Structure to store screen state */
typedef enum screen_state
{
    NFA_SCREEN_STATE_DEFAULT = 0x00,
    NFA_SCREEN_STATE_OFF,
    NFA_SCREEN_STATE_LOCKED,
    NFA_SCREEN_STATE_UNLOCKED
}eScreenState_t;

/*Structure to store  discovery parameters*/
typedef struct discovery_Parameters
{
    int technologies_mask;
    bool enable_lptd;
    bool reader_mode;
    bool enable_host_routing;
    bool restart;
}discovery_Parameters_t;


/*Structure to store transcation result*/
typedef struct Transcation_Check
{
    bool trans_in_progress;
    char last_request;
    eScreenState_t last_screen_state_request;
    eTranscation_events_t current_transcation_state;
    struct nfc_jni_native_data *transaction_nat;
    discovery_Parameters_t discovery_params;
}Transcation_Check_t;

extern tNFA_INTF_TYPE   sCurrentRfInterface;
static Transcation_Check_t transaction_data;
static void nfcManager_enableDiscovery (JNIEnv* e, jobject o, jint technologies_mask,
    jboolean enable_lptd, jboolean reader_mode, jboolean enable_host_routing,
    jboolean restart);
void nfcManager_disableDiscovery (JNIEnv*, jobject);
static bool get_transcation_stat(void);
static void set_transcation_stat(bool result);
static void set_last_request(char status, struct nfc_jni_native_data *nat);
static char get_last_request(void);
void *enableThread(void *arg);
static eScreenState_t get_lastScreenStateRequest(void);
static void set_lastScreenStateRequest(eScreenState_t status);
static IntervalTimer scleanupTimerProc_transaction;
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

/*******************************************************************************
**
** Function:        getNative
**
** Description:     Get native data
**
** Returns:         Native data structure.
**
*******************************************************************************/
nfc_jni_native_data *getNative (JNIEnv* e, jobject o)
{
    static struct nfc_jni_native_data *sCachedNat = NULL;
    if (e)
    {
        sCachedNat = nfc_jni_get_nat(e, o);
    }
    return sCachedNat;
}


/*******************************************************************************
**
** Function:        handleRfDiscoveryEvent
**
** Description:     Handle RF-discovery events from the stack.
**                  discoveredDevice: Discovered device.
**
** Returns:         None
**
*******************************************************************************/
static void handleRfDiscoveryEvent (tNFC_RESULT_DEVT* discoveredDevice)
{
#if (NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    if(discoveredDevice->more == NCI_DISCOVER_NTF_MORE)
#endif
    {
        //there is more discovery notification coming
        NfcTag::getInstance ().mNumDiscNtf++;
        return;
    }

    NfcTag::getInstance ().mNumDiscNtf++;
    bool isP2p = NfcTag::getInstance ().isP2pDiscovered ();
    if (!sReaderModeEnabled && isP2p)
    {
        //select the peer that supports P2P
        NfcTag::getInstance ().selectP2p();
    }
    else
    {
        if (sReaderModeEnabled)
        {
            NfcTag::getInstance ().mNumDiscNtf = 0;
        }
        else
        {
            NfcTag::getInstance ().mNumDiscNtf--;
        }
        //select the first of multiple tags that is discovered
        NfcTag::getInstance ().selectFirstTag();
    }
}


/*******************************************************************************
**
** Function:        nfaConnectionCallback
**
** Description:     Receive connection-related events from stack.
**                  connEvent: Event code.
**                  eventData: Event data.
**
** Returns:         None
**
*******************************************************************************/
static void nfaConnectionCallback (UINT8 connEvent, tNFA_CONN_EVT_DATA* eventData)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    static UINT8 prev_more_val = 0x00;
    UINT8 cur_more_val=0x00;
    ALOGD("%s: event= %u", __FUNCTION__, connEvent);

    switch (connEvent)
    {
    case NFA_POLL_ENABLED_EVT: // whether polling successfully started
        {
            ALOGD("%s: NFA_POLL_ENABLED_EVT: status = %u", __FUNCTION__, eventData->status);

            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

    case NFA_POLL_DISABLED_EVT: // Listening/Polling stopped
        {
            ALOGD("%s: NFA_POLL_DISABLED_EVT: status = %u", __FUNCTION__, eventData->status);

            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

    case NFA_RF_DISCOVERY_STARTED_EVT: // RF Discovery started
        {
            ALOGD("%s: NFA_RF_DISCOVERY_STARTED_EVT: status = %u", __FUNCTION__, eventData->status);

            SyncEventGuard guard (sNfaEnableDisablePollingEvent);
            sNfaEnableDisablePollingEvent.notifyOne ();
        }
        break;

    case NFA_RF_DISCOVERY_STOPPED_EVT: // RF Discovery stopped event
        {
            ALOGD("%s: NFA_RF_DISCOVERY_STOPPED_EVT: status = %u", __FUNCTION__, eventData->status);
            notifyPollingEventwhileNfcOff();
            if (getReconnectState() == true)
            {
               eventData->deactivated.type = NFA_DEACTIVATE_TYPE_SLEEP;
               NfcTag::getInstance().setDeactivationState (eventData->deactivated);
               if (gIsTagDeactivating)
                {
                    NfcTag::getInstance().setActive(false);
                    nativeNfcTag_doDeactivateStatus(0);
                }
            }
            else
            {
                SyncEventGuard guard (sNfaEnableDisablePollingEvent);
                sNfaEnableDisablePollingEvent.notifyOne ();
            }
        }
        break;

    case NFA_DISC_RESULT_EVT: // NFC link/protocol discovery notificaiton
        status = eventData->disc_result.status;
        cur_more_val = eventData->disc_result.discovery_ntf.more;
        ALOGD("%s: NFA_DISC_RESULT_EVT: status = %d", __FUNCTION__, status);
        if((cur_more_val == 0x01) && (prev_more_val != 0x02))
        {
            ALOGE("NFA_DISC_RESULT_EVT failed");
            status = NFA_STATUS_FAILED;
        }
        else
        {
            ALOGD("NFA_DISC_RESULT_EVT success");
            status = NFA_STATUS_OK;
            prev_more_val = cur_more_val;
        }
        if (status != NFA_STATUS_OK)
        {
            NfcTag::getInstance ().mNumDiscNtf = 0;
            ALOGE("%s: NFA_DISC_RESULT_EVT error: status = %d", __FUNCTION__, status);
        }
        else
        {
            NfcTag::getInstance().connectionEventHandler(connEvent, eventData);
            handleRfDiscoveryEvent(&eventData->disc_result.discovery_ntf);
        }
        break;

    case NFA_SELECT_RESULT_EVT: // NFC link/protocol discovery select response
        ALOGD("%s: NFA_SELECT_RESULT_EVT: status = %d, gIsSelectingRfInterface = %d, sIsDisabling=%d", __FUNCTION__, eventData->status, gIsSelectingRfInterface, sIsDisabling);

        if (sIsDisabling)
            break;

        if (eventData->status != NFA_STATUS_OK)
        {
            if (gIsSelectingRfInterface)
            {
                nativeNfcTag_doConnectStatus(false);
            }
            NfcTag::getInstance().mTechListIndex = 0;
            ALOGE("%s: NFA_SELECT_RESULT_EVT error: status = %d", __FUNCTION__, eventData->status);
            NFA_Deactivate (FALSE);
        }
        else if(sReaderModeEnabled && (gFelicaReaderState == STATE_DEACTIVATED_TO_SLEEP))
        {
            SyncEventGuard g (sRespCbEvent);
            ALOGD("%s: Sending Sem Post for Select Event", __FUNCTION__);
            sRespCbEvent.notifyOne ();
            gFelicaReaderState = STATE_FRAMERF_INTF_SELECTED;
        }

        break;

    case NFA_DEACTIVATE_FAIL_EVT:
        ALOGD("%s: NFA_DEACTIVATE_FAIL_EVT: status = %d", __FUNCTION__, eventData->status);
        {
            SyncEventGuard g (gDeactivatedEvent);
            gDeactivatedEvent.notifyOne ();
        }
        break;

    case NFA_ACTIVATED_EVT: // NFC link/protocol activated
        checkforTranscation(NFA_ACTIVATED_EVT, (void *)eventData);
        ALOGD("%s: NFA_ACTIVATED_EVT: gIsSelectingRfInterface=%d, sIsDisabling=%d", __FUNCTION__, gIsSelectingRfInterface, sIsDisabling);
        /*
         * Handle Reader over SWP START_READER_EVENT
         * */
        if(eventData->activated.activate_ntf.intf_param.type == NCI_INTERFACE_UICC_DIRECT || eventData->activated.activate_ntf.intf_param.type == NCI_INTERFACE_ESE_DIRECT )
        {
            SecureElement::getInstance().notifyEEReaderEvent(NFA_RD_SWP_READER_START, eventData->activated.activate_ntf.rf_tech_param.mode);
            break;
        }
        if((eventData->activated.activate_ntf.protocol != NFA_PROTOCOL_NFC_DEP) && (!isListenMode(eventData->activated)))
        {
            sCurrentRfInterface = (tNFA_INTF_TYPE) eventData->activated.activate_ntf.intf_param.type;
        }
        if (EXTNS_GetConnectFlag() == TRUE)
        {
            NfcTag::getInstance().setActivationState ();
            nativeNfcTag_doConnectStatus(true);
            break;
        }
        NfcTag::getInstance().setActive(true);
        if (sIsDisabling || !sIsNfaEnabled)
            break;
        gActivated = true;

        NfcTag::getInstance().setActivationState ();
        if (gIsSelectingRfInterface)
        {
            nativeNfcTag_doConnectStatus(true);
            if(NfcTag::getInstance().isEzLinkType() == true)
            {
                NfcTag::getInstance().connectionEventHandler (NFA_ACTIVATED_UPDATE_EVT, eventData);
            }
            break;
        }

        nativeNfcTag_resetPresenceCheck();
        if (isPeerToPeer(eventData->activated))
        {
            if (sReaderModeEnabled)
            {/*if last transaction is complete or prev state is Idle
             then proceed to nxt state*/
                if((gFelicaReaderState == STATE_IDLE) ||
                    (gFelicaReaderState == STATE_FRAMERF_INTF_SELECTED))
                {
                    ALOGD("%s: Activating Reader Mode in P2P ", __FUNCTION__);
                    gFelicaReaderState = STATE_NFCDEP_ACTIVATED_NFCDEP_INTF;
                    SwitchP2PToT3TRead();
                }
                else
                {
                    ALOGD("%s: Invalid FelicaReaderState : %d  ", __FUNCTION__,gFelicaReaderState);
                    gFelicaReaderState = STATE_IDLE;
                    ALOGD("%s: ignoring peer target in reader mode.", __FUNCTION__);
                    NFA_Deactivate (FALSE);
                }
                break;
            }
            sP2pActive = true;
            ALOGD("%s: NFA_ACTIVATED_EVT; is p2p", __FUNCTION__);
            // Disable RF field events in case of p2p
            UINT8  nfa_disable_rf_events[] = { 0x00 };
            ALOGD ("%s: Disabling RF field events", __FUNCTION__);
#if 0
            status = NFA_SetConfig(NCI_PARAM_ID_RF_FIELD_INFO, sizeof(nfa_disable_rf_events),
                    &nfa_disable_rf_events[0]);
            if (status == NFA_STATUS_OK) {
                ALOGD ("%s: Disabled RF field events", __FUNCTION__);
            } else {
                ALOGE ("%s: Failed to disable RF field events", __FUNCTION__);
            }
#endif
            // For the SE, consider the field to be on while p2p is active.
            SecureElement::getInstance().notifyRfFieldEvent (true);
        }
        else if (pn544InteropIsBusy() == false)
        {
            NfcTag::getInstance().connectionEventHandler (connEvent, eventData);

            if(NfcTag::getInstance ().mNumDiscNtf)
            {
                NFA_Deactivate (TRUE);
            }
            // We know it is not activating for P2P.  If it activated in
            // listen mode then it is likely for an SE transaction.
            // Send the RF Event.
            if (isListenMode(eventData->activated))
            {
                sSeRfActive = true;
                SecureElement::getInstance().notifyListenModeState (true);
            }
        }
        break;

    case NFA_DEACTIVATED_EVT: // NFC link/protocol deactivated
        ALOGD("%s: NFA_DEACTIVATED_EVT   Type: %u, gIsTagDeactivating: %d", __FUNCTION__, eventData->deactivated.type,gIsTagDeactivating);
        notifyPollingEventwhileNfcOff();
        if (true == getReconnectState())
        {
            ALOGD("Reconnect in progress : Do nothing");
            break;
        }

        NfcTag::getInstance().setDeactivationState (eventData->deactivated);
        if(NfcTag::getInstance ().mNumDiscNtf)
        {
            NfcTag::getInstance ().mNumDiscNtf--;
            NfcTag::getInstance().selectNextTag();
        }
        if (eventData->deactivated.type != NFA_DEACTIVATE_TYPE_SLEEP)
        {
            {
                SyncEventGuard g (gDeactivatedEvent);
                gActivated = false; //guard this variable from multi-threaded access
                gDeactivatedEvent.notifyOne ();
            }
             NfcTag::getInstance ().mNumDiscNtf = 0;
             NfcTag::getInstance ().mTechListIndex =0;

            nativeNfcTag_resetPresenceCheck();
            NfcTag::getInstance().connectionEventHandler (connEvent, eventData);
            nativeNfcTag_abortWaits();
            NfcTag::getInstance().abort ();
        }
        else if (gIsTagDeactivating)
        {
            NfcTag::getInstance().setActive(false);
            nativeNfcTag_doDeactivateStatus(0);
        }
        else if (EXTNS_GetDeactivateFlag() == TRUE)
        {
            NfcTag::getInstance().setActive(false);
            nativeNfcTag_doDeactivateStatus(0);
        }

        // If RF is activated for what we think is a Secure Element transaction
        // and it is deactivated to either IDLE or DISCOVERY mode, notify w/event.
        if ((eventData->deactivated.type == NFA_DEACTIVATE_TYPE_IDLE)
                || (eventData->deactivated.type == NFA_DEACTIVATE_TYPE_DISCOVERY))
        {
            if (sSeRfActive) {
                sSeRfActive = false;
                if (!sIsDisabling && sIsNfaEnabled)
                    SecureElement::getInstance().notifyListenModeState (false);
            } else if (sP2pActive) {
                sP2pActive = false;
                // Make sure RF field events are re-enabled
                ALOGD("%s: NFA_DEACTIVATED_EVT; is p2p", __FUNCTION__);
                // Disable RF field events in case of p2p
                UINT8  nfa_enable_rf_events[] = { 0x01 };
/*
                if (!sIsDisabling && sIsNfaEnabled)
                {
                    ALOGD ("%s: Enabling RF field events", __FUNCTION__);
                    status = NFA_SetConfig(NCI_PARAM_ID_RF_FIELD_INFO, sizeof(nfa_enable_rf_events),
                            &nfa_enable_rf_events[0]);
                    if (status == NFA_STATUS_OK) {
                        ALOGD ("%s: Enabled RF field events", __FUNCTION__);
                    } else {
                        ALOGE ("%s: Failed to enable RF field events", __FUNCTION__);
                    }
                    // Consider the field to be off at this point
                    SecureElement::getInstance().notifyRfFieldEvent (false);
                }
*/
            }
        }
        if (sReaderModeEnabled && (eventData->deactivated.type == NFA_DEACTIVATE_TYPE_SLEEP))
        {
            if(gFelicaReaderState == STATE_NFCDEP_ACTIVATED_NFCDEP_INTF)
            {
                SyncEventGuard g (sRespCbEvent);
                ALOGD("%s: Sending Sem Post for Deactivated", __FUNCTION__);
                sRespCbEvent.notifyOne ();
                ALOGD("Switching to T3T\n");
                gFelicaReaderState = STATE_DEACTIVATED_TO_SLEEP;
            }
            else
            {
                ALOGD("%s: FelicaReaderState Invalid", __FUNCTION__);
                gFelicaReaderState = STATE_IDLE;
            }
        }
        break;

    case NFA_TLV_DETECT_EVT: // TLV Detection complete
        status = eventData->tlv_detect.status;
        ALOGD("%s: NFA_TLV_DETECT_EVT: status = %d, protocol = %d, num_tlvs = %d, num_bytes = %d",
             __FUNCTION__, status, eventData->tlv_detect.protocol,
             eventData->tlv_detect.num_tlvs, eventData->tlv_detect.num_bytes);
        if (status != NFA_STATUS_OK)
        {
            ALOGE("%s: NFA_TLV_DETECT_EVT error: status = %d", __FUNCTION__, status);
        }
        break;

    case NFA_NDEF_DETECT_EVT: // NDEF Detection complete;
        //if status is failure, it means the tag does not contain any or valid NDEF data;
        //pass the failure status to the NFC Service;
        status = eventData->ndef_detect.status;
        ALOGD("%s: NFA_NDEF_DETECT_EVT: status = 0x%X, protocol = %u, "
             "max_size = %lu, cur_size = %lu, flags = 0x%X", __FUNCTION__,
             status,
             eventData->ndef_detect.protocol, eventData->ndef_detect.max_size,
             eventData->ndef_detect.cur_size, eventData->ndef_detect.flags);
        NfcTag::getInstance().connectionEventHandler (connEvent, eventData);
        nativeNfcTag_doCheckNdefResult(status,
            eventData->ndef_detect.max_size, eventData->ndef_detect.cur_size,
            eventData->ndef_detect.flags);
        break;

    case NFA_DATA_EVT: // Data message received (for non-NDEF reads)
        ALOGD("%s: NFA_DATA_EVT: status = 0x%X, len = %d", __FUNCTION__, eventData->status, eventData->data.len);
        nativeNfcTag_doTransceiveStatus(eventData->status, eventData->data.p_data, eventData->data.len);
        break;
    case NFA_RW_INTF_ERROR_EVT:
        ALOGD("%s: NFC_RW_INTF_ERROR_EVT", __FUNCTION__);
        nativeNfcTag_notifyRfTimeout();
        nativeNfcTag_doReadCompleted (NFA_STATUS_TIMEOUT);
        break;
    case NFA_SELECT_CPLT_EVT: // Select completed
        status = eventData->status;
        ALOGD("%s: NFA_SELECT_CPLT_EVT: status = %d", __FUNCTION__, status);
        if (status != NFA_STATUS_OK)
        {
            ALOGE("%s: NFA_SELECT_CPLT_EVT error: status = %d", __FUNCTION__, status);
        }
        break;

    case NFA_READ_CPLT_EVT: // NDEF-read or tag-specific-read completed
        ALOGD("%s: NFA_READ_CPLT_EVT: status = 0x%X", __FUNCTION__, eventData->status);
        nativeNfcTag_doReadCompleted (eventData->status);
        NfcTag::getInstance().connectionEventHandler (connEvent, eventData);
        break;

    case NFA_WRITE_CPLT_EVT: // Write completed
        ALOGD("%s: NFA_WRITE_CPLT_EVT: status = %d", __FUNCTION__, eventData->status);
        nativeNfcTag_doWriteStatus (eventData->status == NFA_STATUS_OK);
        break;

    case NFA_SET_TAG_RO_EVT: // Tag set as Read only
        ALOGD("%s: NFA_SET_TAG_RO_EVT: status = %d", __FUNCTION__, eventData->status);
        nativeNfcTag_doMakeReadonlyResult(eventData->status);
        break;

    case NFA_CE_NDEF_WRITE_START_EVT: // NDEF write started
        ALOGD("%s: NFA_CE_NDEF_WRITE_START_EVT: status: %d", __FUNCTION__, eventData->status);

        if (eventData->status != NFA_STATUS_OK)
            ALOGE("%s: NFA_CE_NDEF_WRITE_START_EVT error: status = %d", __FUNCTION__, eventData->status);
        break;

    case NFA_CE_NDEF_WRITE_CPLT_EVT: // NDEF write completed
        ALOGD("%s: FA_CE_NDEF_WRITE_CPLT_EVT: len = %lu", __FUNCTION__, eventData->ndef_write_cplt.len);
        break;

    case NFA_LLCP_ACTIVATED_EVT: // LLCP link is activated
        ALOGD("%s: NFA_LLCP_ACTIVATED_EVT: is_initiator: %d  remote_wks: %d, remote_lsc: %d, remote_link_miu: %d, local_link_miu: %d",
             __FUNCTION__,
             eventData->llcp_activated.is_initiator,
             eventData->llcp_activated.remote_wks,
             eventData->llcp_activated.remote_lsc,
             eventData->llcp_activated.remote_link_miu,
             eventData->llcp_activated.local_link_miu);

        PeerToPeer::getInstance().llcpActivatedHandler (getNative(0, 0), eventData->llcp_activated);
        break;

    case NFA_LLCP_DEACTIVATED_EVT: // LLCP link is deactivated
        ALOGD("%s: NFA_LLCP_DEACTIVATED_EVT", __FUNCTION__);
        PeerToPeer::getInstance().llcpDeactivatedHandler (getNative(0, 0), eventData->llcp_deactivated);
        break;
    case NFA_LLCP_FIRST_PACKET_RECEIVED_EVT: // Received first packet over llcp
        ALOGD("%s: NFA_LLCP_FIRST_PACKET_RECEIVED_EVT", __FUNCTION__);
        PeerToPeer::getInstance().llcpFirstPacketHandler (getNative(0, 0));
        break;
    case NFA_PRESENCE_CHECK_EVT:
        ALOGD("%s: NFA_PRESENCE_CHECK_EVT", __FUNCTION__);
        nativeNfcTag_doPresenceCheckResult (eventData->status);
        break;
    case NFA_FORMAT_CPLT_EVT:
        ALOGD("%s: NFA_FORMAT_CPLT_EVT: status=0x%X", __FUNCTION__, eventData->status);
        nativeNfcTag_formatStatus (eventData->status == NFA_STATUS_OK);
        break;

    case NFA_I93_CMD_CPLT_EVT:
        ALOGD("%s: NFA_I93_CMD_CPLT_EVT: status=0x%X", __FUNCTION__, eventData->status);
        break;

    case NFA_CE_UICC_LISTEN_CONFIGURED_EVT :
        ALOGD("%s: NFA_CE_UICC_LISTEN_CONFIGURED_EVT : status=0x%X", __FUNCTION__, eventData->status);
        SecureElement::getInstance().connectionEventHandler (connEvent, eventData);
        break;

    case NFA_CE_ESE_LISTEN_CONFIGURED_EVT :
        ALOGD("%s: NFA_CE_ESE_LISTEN_CONFIGURED_EVT : status=0x%X", __FUNCTION__, eventData->status);
        SecureElement::getInstance().connectionEventHandler (connEvent, eventData);
        break;

    case NFA_SET_P2P_LISTEN_TECH_EVT:
        ALOGD("%s: NFA_SET_P2P_LISTEN_TECH_EVT", __FUNCTION__);
        PeerToPeer::getInstance().connectionEventHandler (connEvent, eventData);
        break;
    case NFA_CE_LOCAL_TAG_CONFIGURED_EVT:
        ALOGD("%s: NFA_CE_LOCAL_TAG_CONFIGURED_EVT", __FUNCTION__);
        break;
    default:
        ALOGE("%s: unknown event ????", __FUNCTION__);
        break;
    }
}


/*******************************************************************************
**
** Function:        nfcManager_initNativeStruc
**
** Description:     Initialize variables.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_initNativeStruc (JNIEnv* e, jobject o)
{
    ALOGD ("%s: enter", __FUNCTION__);

    nfc_jni_native_data* nat = (nfc_jni_native_data*)malloc(sizeof(struct nfc_jni_native_data));
    if (nat == NULL)
    {
        ALOGE ("%s: fail allocate native data", __FUNCTION__);
        return JNI_FALSE;
    }

    memset (nat, 0, sizeof(*nat));
    e->GetJavaVM(&(nat->vm));
    nat->env_version = e->GetVersion();
    nat->manager = e->NewGlobalRef(o);

    ScopedLocalRef<jclass> cls(e, e->GetObjectClass(o));
    jfieldID f = e->GetFieldID(cls.get(), "mNative", "J");
    e->SetLongField(o, f, (jlong)nat);

    /* Initialize native cached references */
    gCachedNfcManagerNotifyNdefMessageListeners = e->GetMethodID(cls.get(),
            "notifyNdefMessageListeners", "(Lcom/android/nfc/dhimpl/NativeNfcTag;)V");
    gCachedNfcManagerNotifyTransactionListeners = e->GetMethodID(cls.get(),
            "notifyTransactionListeners", "([B[BI)V");
    gCachedNfcManagerNotifyConnectivityListeners = e->GetMethodID(cls.get(),
                "notifyConnectivityListeners", "(I)V");
    gCachedNfcManagerNotifyEmvcoMultiCardDetectedListeners = e->GetMethodID(cls.get(),
                "notifyEmvcoMultiCardDetectedListeners", "()V");
    gCachedNfcManagerNotifyLlcpLinkActivation = e->GetMethodID(cls.get(),
            "notifyLlcpLinkActivation", "(Lcom/android/nfc/dhimpl/NativeP2pDevice;)V");
    gCachedNfcManagerNotifyLlcpLinkDeactivated = e->GetMethodID(cls.get(),
            "notifyLlcpLinkDeactivated", "(Lcom/android/nfc/dhimpl/NativeP2pDevice;)V");
    gCachedNfcManagerNotifyLlcpFirstPacketReceived = e->GetMethodID(cls.get(),
            "notifyLlcpLinkFirstPacketReceived", "(Lcom/android/nfc/dhimpl/NativeP2pDevice;)V");
    sCachedNfcManagerNotifyTargetDeselected = e->GetMethodID(cls.get(),
            "notifyTargetDeselected","()V");
    gCachedNfcManagerNotifySeFieldActivated = e->GetMethodID(cls.get(),
            "notifySeFieldActivated", "()V");
    gCachedNfcManagerNotifySeFieldDeactivated = e->GetMethodID(cls.get(),
            "notifySeFieldDeactivated", "()V");
    gCachedNfcManagerNotifySeListenActivated = e->GetMethodID(cls.get(),
            "notifySeListenActivated", "()V");
    gCachedNfcManagerNotifySeListenDeactivated = e->GetMethodID(cls.get(),
            "notifySeListenDeactivated", "()V");

    gCachedNfcManagerNotifyHostEmuActivated = e->GetMethodID(cls.get(),
            "notifyHostEmuActivated", "()V");

    gCachedNfcManagerNotifyAidRoutingTableFull = e->GetMethodID(cls.get(),
            "notifyAidRoutingTableFull", "()V");

    gCachedNfcManagerNotifyHostEmuData = e->GetMethodID(cls.get(),
            "notifyHostEmuData", "([B)V");

    gCachedNfcManagerNotifyHostEmuDeactivated = e->GetMethodID(cls.get(),
            "notifyHostEmuDeactivated", "()V");

    gCachedNfcManagerNotifyRfFieldActivated = e->GetMethodID(cls.get(),
            "notifyRfFieldActivated", "()V");
    gCachedNfcManagerNotifyRfFieldDeactivated = e->GetMethodID(cls.get(),
            "notifyRfFieldDeactivated", "()V");

    sCachedNfcManagerNotifySeApduReceived = e->GetMethodID(cls.get(),
            "notifySeApduReceived", "([B)V");

    sCachedNfcManagerNotifySeMifareAccess = e->GetMethodID(cls.get(),
            "notifySeMifareAccess", "([B)V");

    sCachedNfcManagerNotifySeEmvCardRemoval =  e->GetMethodID(cls.get(),
            "notifySeEmvCardRemoval", "()V");

    gCachedNfcManagerNotifySWPReaderRequested = e->GetMethodID (cls.get(),
            "notifySWPReaderRequested", "(ZZ)V");

    gCachedNfcManagerNotifySWPReaderActivated = e->GetMethodID (cls.get(),
            "notifySWPReaderActivated", "()V");

    gCachedNfcManagerNotifySWPReaderDeActivated = e->GetMethodID (cls.get(),
            "notifyonSWPReaderDeActivated", "()V");

    if (nfc_jni_cache_object(e, gNativeNfcTagClassName, &(nat->cached_NfcTag)) == -1)
    {
        ALOGE ("%s: fail cache NativeNfcTag", __FUNCTION__);
        return JNI_FALSE;
    }

    if (nfc_jni_cache_object(e, gNativeP2pDeviceClassName, &(nat->cached_P2pDevice)) == -1)
    {
        ALOGE ("%s: fail cache NativeP2pDevice", __FUNCTION__);
        return JNI_FALSE;
    }

    ALOGD ("%s: exit", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfaDeviceManagementCallback
**
** Description:     Receive device management events from stack.
**                  dmEvent: Device-management event ID.
**                  eventData: Data associated with event ID.
**
** Returns:         None
**
*******************************************************************************/
void nfaDeviceManagementCallback (UINT8 dmEvent, tNFA_DM_CBACK_DATA* eventData)
{
    ALOGD ("%s: enter; event=0x%X", __FUNCTION__, dmEvent);

    switch (dmEvent)
    {
    case NFA_DM_ENABLE_EVT: /* Result of NFA_Enable */
        {
            SyncEventGuard guard (sNfaEnableEvent);
            ALOGD ("%s: NFA_DM_ENABLE_EVT; status=0x%X",
                    __FUNCTION__, eventData->status);
            sIsNfaEnabled = eventData->status == NFA_STATUS_OK;
            sIsDisabling = false;
            sNfaEnableEvent.notifyOne ();
        }
        break;

    case NFA_DM_DISABLE_EVT: /* Result of NFA_Disable */
        {
            SyncEventGuard guard (sNfaDisableEvent);
            ALOGD ("%s: NFA_DM_DISABLE_EVT", __FUNCTION__);
            sIsNfaEnabled = false;
            sIsDisabling = false;
            sNfaDisableEvent.notifyOne ();
        }
        break;

    case NFA_DM_SET_CONFIG_EVT: //result of NFA_SetConfig
        ALOGD ("%s: NFA_DM_SET_CONFIG_EVT", __FUNCTION__);
        {
            SyncEventGuard guard (sNfaSetConfigEvent);
            sNfaSetConfigEvent.notifyOne();
        }
        break;

    case NFA_DM_GET_CONFIG_EVT: /* Result of NFA_GetConfig */
        ALOGD ("%s: NFA_DM_GET_CONFIG_EVT", __FUNCTION__);
        {
            HciRFParams::getInstance().connectionEventHandler(dmEvent,eventData);
            SyncEventGuard guard (sNfaGetConfigEvent);
            if (eventData->status == NFA_STATUS_OK &&
                    eventData->get_config.tlv_size <= sizeof(sConfig))
            {
                sCurrentConfigLen = eventData->get_config.tlv_size;
                memcpy(sConfig, eventData->get_config.param_tlvs, eventData->get_config.tlv_size);

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
#ifdef CHECK_FOR_NFCEE_CONFIGURATION
                if(sCheckNfceeFlag)
                    checkforNfceeBuffer();
#endif
#endif
            }
            else
            {
                ALOGE("%s: NFA_DM_GET_CONFIG failed", __FUNCTION__);
                sCurrentConfigLen = 0;
            }
            sNfaGetConfigEvent.notifyOne();
        }
        break;

    case NFA_DM_RF_FIELD_EVT:
        checkforTranscation(NFA_TRANS_DM_RF_FIELD_EVT, (void *)eventData);
        ALOGD ("%s: NFA_DM_RF_FIELD_EVT; status=0x%X; field status=%u", __FUNCTION__,
              eventData->rf_field.status, eventData->rf_field.rf_field_status);
        if (sIsDisabling || !sIsNfaEnabled)
            break;

        if (!sP2pActive && eventData->rf_field.status == NFA_STATUS_OK)
        {
            SecureElement::getInstance().notifyRfFieldEvent (
                eventData->rf_field.rf_field_status == NFA_DM_RF_FIELD_ON);

            struct nfc_jni_native_data *nat = getNative(NULL, NULL);
            JNIEnv* e = NULL;
            ScopedAttach attach(nat->vm, &e);
            if (e == NULL)
            {
                ALOGE ("jni env is null");
                return;
            }
            if (eventData->rf_field.rf_field_status == NFA_DM_RF_FIELD_ON)
                e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyRfFieldActivated);
            else
                e->CallVoidMethod (nat->manager, android::gCachedNfcManagerNotifyRfFieldDeactivated);
        }
        break;

    case NFA_DM_NFCC_TRANSPORT_ERR_EVT:
    case NFA_DM_NFCC_TIMEOUT_EVT:
        {
            if (dmEvent == NFA_DM_NFCC_TIMEOUT_EVT)
                ALOGE ("%s: NFA_DM_NFCC_TIMEOUT_EVT; abort", __FUNCTION__);
            else if (dmEvent == NFA_DM_NFCC_TRANSPORT_ERR_EVT)
                ALOGE ("%s: NFA_DM_NFCC_TRANSPORT_ERR_EVT; abort", __FUNCTION__);
            NFA_HciW4eSETransaction_Complete();
            nativeNfcTag_abortWaits();
            NfcTag::getInstance().abort ();
            sAbortConnlessWait = true;
            nativeLlcpConnectionlessSocket_abortWait();
            {
                ALOGD ("%s: aborting  sNfaEnableDisablePollingEvent", __FUNCTION__);
                SyncEventGuard guard (sNfaEnableDisablePollingEvent);
                sNfaEnableDisablePollingEvent.notifyOne();
            }
            {
                ALOGD ("%s: aborting  sNfaEnableEvent", __FUNCTION__);
                SyncEventGuard guard (sNfaEnableEvent);
                sNfaEnableEvent.notifyOne();
            }
            {
                ALOGD ("%s: aborting  sNfaDisableEvent", __FUNCTION__);
                SyncEventGuard guard (sNfaDisableEvent);
                sNfaDisableEvent.notifyOne();
            }
            sDiscoveryEnabled = false;
            sPollingEnabled = false;
            PowerSwitch::getInstance ().abort ();

            if (!sIsDisabling && sIsNfaEnabled)
            {
                EXTNS_Close();
                NFA_Disable(FALSE);
                sIsDisabling = true;
            }
            else
            {
                sIsNfaEnabled = false;
                sIsDisabling = false;
            }
            PowerSwitch::getInstance ().initialize (PowerSwitch::UNKNOWN_LEVEL);
            ALOGE ("%s: crash NFC service", __FUNCTION__);
            //////////////////////////////////////////////
            //crash the NFC service process so it can restart automatically
            abort ();
            //////////////////////////////////////////////
        }
        break;

    case NFA_DM_PWR_MODE_CHANGE_EVT:
        PowerSwitch::getInstance ().deviceManagementCallback (dmEvent, eventData);
        break;

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    case NFA_DM_SET_ROUTE_CONFIG_REVT:
        ALOGD ("%s: NFA_DM_SET_ROUTE_CONFIG_REVT; status=0x%X",
                __FUNCTION__, eventData->status);
        if(eventData->status != NFA_STATUS_OK)
        {
            ALOGD("AID Routing table configuration Failed!!!");
        }
        else
        {
            ALOGD("AID Routing Table configured.");
        }
        break;
#endif

    case NFA_DM_EMVCO_PCD_COLLISION_EVT:
        ALOGE("STATUS_EMVCO_PCD_COLLISION - Multiple card detected");
        SecureElement::getInstance().notifyEmvcoMultiCardDetectedListeners();
        break;
    default:
        ALOGD ("%s: unhandled event", __FUNCTION__);
        break;
    }
}

/*******************************************************************************
**
** Function:        nfcManager_sendRawFrame
**
** Description:     Send a raw frame.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_sendRawFrame (JNIEnv* e, jobject, jbyteArray data)
{
    ScopedByteArrayRO bytes(e, data);
    uint8_t* buf = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&bytes[0]));
    size_t bufLen = bytes.size();
    tNFA_STATUS status = NFA_SendRawFrame (buf, bufLen, 0);

    return (status == NFA_STATUS_OK);
}

/*******************************************************************************
**
** Function:        nfcManager_routeAid
**
** Description:     Route an AID to an EE
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_routeAid (JNIEnv* e, jobject, jbyteArray aid, jint route, jint power, jboolean isprefix)
{
    ScopedByteArrayRO bytes(e, aid);
    uint8_t* buf = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&bytes[0]));
    size_t bufLen = bytes.size();

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
    bool result = RoutingManager::getInstance().addAidRouting(buf, bufLen, route, power, isprefix);
#else
    bool result = RoutingManager::getInstance().addAidRouting(buf, bufLen, route);

#endif
    return result;
}

/*******************************************************************************
**
** Function:        nfcManager_clearRouting
**
** Description:     Clean all AIDs in routing table
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/
static void nfcManager_clearRouting (JNIEnv*, jobject)
{
    RoutingManager::getInstance().clearAidRouting();
}
/*******************************************************************************
**
** Function:        nfcManager_getAidTableSize
** Description:     Get the current size of AID routing table.The maximum
**  supported size for AID routing table is NFA_EE_MAX_AID_CFG_LEN(160) bytes.
**
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/
static jint nfcManager_getAidTableSize (JNIEnv*, jobject, jint switch_on, jint switch_off, jint battery_off)
{
    (void)switch_on;
    (void)switch_off;
    (void)battery_off;
    return NFA_GetAidTableSize();
}
/*******************************************************************************
**
** Function:        nfcManager_setDefaultRoute
**
** Description:     Set the default route in routing table
**                  e: JVM environment.
**                  o: Java object.
**
*******************************************************************************/

static jboolean nfcManager_setDefaultRoute (JNIEnv*, jobject, jint defaultRouteEntry, jint defaultProtoRouteEntry, jint defaultTechRouteEntry)
{
    jboolean result = FALSE;
    if (sRfEnabled) {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

    result = RoutingManager::getInstance().setDefaultRoute(defaultRouteEntry, defaultProtoRouteEntry, defaultTechRouteEntry);
    RoutingManager::getInstance().commitRouting();

    startRfDiscovery(true);

    return result;
}

/*******************************************************************************
**
** Function:        nfcManager_commitRouting
**
** Description:     Sends the AID routing table to the controller
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_commitRouting (JNIEnv* e, jobject)
{
    return RoutingManager::getInstance().commitRouting();
}

/*******************************************************************************
**
** Function:        nfcManager_doInitialize
**
** Description:     Turn on NFC.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_doInitialize (JNIEnv* e, jobject o)
{
    ALOGD ("%s: enter; ver=%s nfa=%s NCI_VERSION=0x%02X",
        __FUNCTION__, nfca_version_string, nfa_version_string, NCI_VERSION);
    tNFA_STATUS stat = NFA_STATUS_OK;

    NfcTag::getInstance ().mNfcDisableinProgress = false;
    PowerSwitch & powerSwitch = PowerSwitch::getInstance ();

    if (sIsNfaEnabled)
    {
        ALOGD ("%s: already enabled", __FUNCTION__);
        goto TheEnd;
    }

if ((signal(SIGABRT, sig_handler) == SIG_ERR) &&
        (signal(SIGSEGV, sig_handler) == SIG_ERR))
    {
        ALOGE("Failed to register signal handeler");
     }

    powerSwitch.initialize (PowerSwitch::FULL_POWER);

    {
        unsigned long num = 0;

        NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
        theInstance.Initialize(); //start GKI, NCI task, NFC task

        {
            SyncEventGuard guard (sNfaEnableEvent);
            tHAL_NFC_ENTRY* halFuncEntries = theInstance.GetHalEntryFuncs ();

            NFA_Init (halFuncEntries);

            stat = NFA_Enable (nfaDeviceManagementCallback, nfaConnectionCallback);
            if (stat == NFA_STATUS_OK)
            {
                num = initializeGlobalAppLogLevel ();
                CE_SetTraceLevel (num);
                LLCP_SetTraceLevel (num);
                NFC_SetTraceLevel (num);
                RW_SetTraceLevel (num);
                NFA_SetTraceLevel (num);
                NFA_P2pSetTraceLevel (num);
                sNfaEnableEvent.wait(); //wait for NFA command to finish
            }
            EXTNS_Init(nfaDeviceManagementCallback, nfaConnectionCallback);
        }

        if (stat == NFA_STATUS_OK)
        {
            //sIsNfaEnabled indicates whether stack started successfully
            if (sIsNfaEnabled)
            {
                SecureElement::getInstance().initialize (getNative(e, o));
                setListenMode();
                RoutingManager::getInstance().initialize(getNative(e, o));
                HciRFParams::getInstance().initialize ();
                sIsSecElemSelected = (SecureElement::getInstance().getActualNumEe() - 1 );
                sIsSecElemDetected = sIsSecElemSelected;
                nativeNfcTag_registerNdefTypeHandler ();
                NfcTag::getInstance().initialize (getNative(e, o));
                PeerToPeer::getInstance().initialize ();
                PeerToPeer::getInstance().handleNfcOnOff (true);
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
#ifdef CHECK_FOR_NFCEE_CONFIGURATION
                checkforNfceeConfig();
#endif
#endif
                /////////////////////////////////////////////////////////////////////////////////
                // Add extra configuration here (work-arounds, etc.)
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
                if (gGeneralPowershutDown == VEN_CFG_NFC_ON_POWER_ON)
                {
                    stat = SetVenConfigValue(gGeneralPowershutDown);
                    if (stat != NFA_STATUS_OK)
                    {
                        ALOGE ("%s: fail enable SetVenConfigValue; error=0x%X", __FUNCTION__, stat);
                    }
                    ALOGE ("%s: set the VEN_CFG to %d", __FUNCTION__,gGeneralPowershutDown);
                    gGeneralPowershutDown = 0;
                }
#endif
                struct nfc_jni_native_data *nat = getNative(e, o);

                if ( nat )
                {
                    if (GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
                        nat->tech_mask = num;
                    else
                        nat->tech_mask = DEFAULT_TECH_MASK;
                    ALOGD ("%s: tag polling tech mask=0x%X", __FUNCTION__, nat->tech_mask);
                }

                // if this value exists, set polling interval.
                if (GetNumValue(NAME_NFA_DM_DISC_DURATION_POLL, &num, sizeof(num)))
                    nat->discovery_duration = num;
                else
                    nat->discovery_duration = DEFAULT_DISCOVERY_DURATION;

                NFA_SetRfDiscoveryDuration(nat->discovery_duration);

#if(NFC_NXP_NOT_OPEN_INCLUDED != TRUE)
                // Do custom NFCA startup configuration.
                doStartupConfig();
#endif
                goto TheEnd;
            }
        }

        ALOGE ("%s: fail nfa enable; error=0x%X", __FUNCTION__, stat);

        if (sIsNfaEnabled)
        {
            EXTNS_Close();
            stat = NFA_Disable (FALSE /* ungraceful */);
        }

        theInstance.Finalize();
    }

TheEnd:
    if (sIsNfaEnabled)
        PowerSwitch::getInstance ().setLevel (PowerSwitch::LOW_POWER);
    ALOGD ("%s: exit", __FUNCTION__);
    return sIsNfaEnabled ? JNI_TRUE : JNI_FALSE;
}



/*******************************************************************************
**
** Function:        nfcManager_getDefaultAidRoute
**
** Description:     Get the default Aid Route Entry.
**                  e: JVM environment.
**                  o: Java object.
**                  mode: Not used.
**
** Returns:         None
**
*******************************************************************************/

//(GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num)))
static jint nfcManager_getDefaultAidRoute (JNIEnv* e, jobject o)
{
    unsigned long num = 0;
    GetNxpNumValue(NAME_DEFAULT_AID_ROUTE, &num, sizeof(num));
    return num;

}

/*******************************************************************************
**
** Function:        nfcManager_getDefaultDesfireRoute
**
** Description:     Get the default Desfire Route Entry.
**                  e: JVM environment.
**                  o: Java object.
**                  mode: Not used.
**
** Returns:         None
**
*******************************************************************************/

//(GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num)))
static jint nfcManager_getDefaultDesfireRoute (JNIEnv* e, jobject o)
{
    unsigned long num = 0;
    GetNxpNumValue(NAME_DEFAULT_DESFIRE_ROUTE, (void*)&num, sizeof(num));
    ALOGD ("%s: enter; NAME_DEFAULT_DESFIRE_ROUTE = %02x", __FUNCTION__, num);
    return num;

}

/*******************************************************************************
**
** Function:        nfcManager_getDefaultMifareCLTRoute
**
** Description:     Get the default mifare CLT Route Entry.
**                  e: JVM environment.
**                  o: Java object.
**                  mode: Not used.
**
** Returns:         None
**
*******************************************************************************/

//(GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num)))
static jint nfcManager_getDefaultMifareCLTRoute (JNIEnv* e, jobject o)
{
    unsigned long num = 0;
    GetNxpNumValue(NAME_DEFAULT_MIFARE_CLT_ROUTE, &num, sizeof(num));
    return num;

}

/*******************************************************************************
**
** Function:        nfcManager_enableDiscovery
**
** Description:     Start polling and listening for devices.
**                  e: JVM environment.
**                  o: Java object.
**                  technologies_mask: the bitmask of technologies for which to enable discovery
**                  enable_lptd: whether to enable low power polling (default: false)
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_enableDiscovery (JNIEnv* e, jobject o, jint technologies_mask,
    jboolean enable_lptd, jboolean reader_mode, jboolean enable_host_routing,
    jboolean restart)
{
    tNFA_STATUS status = NFA_STATUS_OK;
    tNFA_TECHNOLOGY_MASK tech_mask = DEFAULT_TECH_MASK;
    unsigned long num = 0;
    unsigned long p2p_listen_mask = 0;
    tNFA_HANDLE handle = NFA_HANDLE_INVALID;
    struct nfc_jni_native_data *nat = NULL;
    if(e == NULL && o == NULL)
    {
        nat = transaction_data.transaction_nat;
    }
    else
    {
        nat = getNative(e, o);
    }

    if(get_transcation_stat() == true)
    {
        ALOGD("Transcation is in progress store the requst");
        set_last_request(1, nat);
        transaction_data.discovery_params.technologies_mask = technologies_mask;
        transaction_data.discovery_params.enable_lptd = enable_lptd;
        transaction_data.discovery_params.reader_mode = reader_mode;
        transaction_data.discovery_params.enable_host_routing = enable_host_routing;
        transaction_data.discovery_params.restart = restart;
        return;
    }
    if (technologies_mask == -1 && nat)
        tech_mask = (tNFA_TECHNOLOGY_MASK)nat->tech_mask;
    else if (technologies_mask != -1)
        tech_mask = (tNFA_TECHNOLOGY_MASK) technologies_mask;
    sEnableLptd = enable_lptd;
    ALOGD ("%s: enter; tech_mask = %02x", __FUNCTION__, tech_mask);

    if(!sForceDiscovery && sDiscoveryEnabled && !restart)
    {
        ALOGE ("%s: already discovering", __FUNCTION__);
        return;
    }

    tNFA_STATUS stat = NFA_STATUS_OK;

    ALOGD ("%s: sIsSecElemSelected=%u", __FUNCTION__, sIsSecElemSelected);

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);

    if (sRfEnabled) {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

#if 0 //EEPROM Init optimization
    {
        UINT8 sel_info = 0x60;
        UINT8 lf_protocol = 0x02;
        {
            SyncEventGuard guard (android::sNfaSetConfigEvent);
            status = NFA_SetConfig(NCI_PARAM_ID_LF_PROTOCOL, sizeof(UINT8), &lf_protocol);
            if (status == NFA_STATUS_OK)
                sNfaSetConfigEvent.wait ();
            else
                ALOGE ("%s: Could not able to configure lf_protocol", __FUNCTION__);
        }

        {
            SyncEventGuard guard (android::sNfaSetConfigEvent);
            status = NFA_SetConfig(NCI_PARAM_ID_LA_SEL_INFO, sizeof(UINT8), &sel_info);
            if (status == NFA_STATUS_OK)
                sNfaSetConfigEvent.wait ();
            else
                ALOGE ("%s: Could not able to configure sel_info", __FUNCTION__);
        }
    }
#endif //EEPROM Init optimization
/*
    {
        StoreScreenState(3);
        status = SetScreenState(true, false);
        if (status != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail enable SetScreenState; error=0x%X", __FUNCTION__, status);
        }
    }
*/
    if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num))))
    {
        ALOGE ("%s:UICC_LISTEN_MASK=0x0%d;", __FUNCTION__, num);
    }
//    if ((GetNumValue("P2P_LISTEN_TECH_MASK", &p2p_listen_mask, sizeof(p2p_listen_mask))))
//    {
//        ALOGE ("%s:P2P_LISTEN_TECH_MASK=0x0%d;", __FUNCTION__, p2p_listen_mask);
//    }


    // Check polling configuration
    if (tech_mask != 0)
    {
        ALOGD ("%s: Disable p2pListening", __FUNCTION__);
        PeerToPeer::getInstance().enableP2pListening (false);
        stopPolling_rfDiscoveryDisabled();
        enableDisableLptd(enable_lptd);
        startPolling_rfDiscoveryDisabled(tech_mask);

        // Start P2P listening if tag polling was enabled
        if (sPollingEnabled)
        {
            ALOGD ("%s: Enable p2pListening", __FUNCTION__);
            PeerToPeer::getInstance().enableP2pListening (!reader_mode);

            if (reader_mode && !sReaderModeEnabled)
            {
                sReaderModeEnabled = true;
                NFA_PauseP2p();
                NFA_DisableListening();
                NFA_SetRfDiscoveryDuration(READER_MODE_DISCOVERY_DURATION);
            }
            else if (sReaderModeEnabled)
            {
                struct nfc_jni_native_data *nat = getNative(e, o);
                sReaderModeEnabled = false;
                NFA_ResumeP2p();
                NFA_EnableListening();
                NFA_SetRfDiscoveryDuration(nat->discovery_duration);
            }
            else
            {
                {
                    ALOGD ("%s: restart UICC listen mode (%02X)", __FUNCTION__, (num & 0xC7));
                    handle = SecureElement::getInstance().getEseHandleFromGenericId(SecureElement::UICC_ID);
                    SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
                    stat = NFA_CeConfigureUiccListenTech (handle, 0x00);
                    if(stat == NFA_STATUS_OK)
                    {
                        SecureElement::getInstance().mUiccListenEvent.wait ();
                    }
                    else
                        ALOGE ("fail to stop UICC listen");
                }
                {
                    SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
                    stat = NFA_CeConfigureUiccListenTech (handle, (num & 0xC7));
                    if(stat == NFA_STATUS_OK)
                    {
                        SecureElement::getInstance().mUiccListenEvent.wait ();
                    }
                    else
                        ALOGE ("fail to start UICC listen");
                }
            }
        }
    }
    else
    {
        // No technologies configured, stop polling
        stopPolling_rfDiscoveryDisabled();
    }
//FIXME: Added in L, causing routing table update for screen on/off. Need to check.
#if 0
    // Check listen configuration
    if (enable_host_routing)
    {
        RoutingManager::getInstance().enableRoutingToHost();
        RoutingManager::getInstance().commitRouting();
    }
    else
    {
        RoutingManager::getInstance().disableRoutingToHost();
        RoutingManager::getInstance().commitRouting();
    }
#endif
    // Start P2P listening if tag polling was enabled or the mask was 0.
    if (sDiscoveryEnabled || (tech_mask == 0))
    {
        handle = SecureElement::getInstance().getEseHandleFromGenericId(SecureElement::UICC_ID);
        if (getScreenState() == NFA_SCREEN_STATE_UNLOCKED )
        {
            ALOGD ("%s: Enable p2pListening", __FUNCTION__);
            PeerToPeer::getInstance().enableP2pListening (true);
        }
        else
        {
            ALOGD ("%s: Disable p2pListening", __FUNCTION__);
            PeerToPeer::getInstance().enableP2pListening (false);
        }

        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            stat = NFA_CeConfigureUiccListenTech (handle, 0x00);
            if(stat == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }

        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            stat = NFA_CeConfigureUiccListenTech (handle, (num & 0xC7));
            if(stat == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }
    }
    // Actually start discovery.
    startRfDiscovery (true);
    sDiscoveryEnabled = true;

    PowerSwitch::getInstance ().setModeOn (PowerSwitch::DISCOVERY);
    sForceDiscovery = false;

    ALOGD ("%s: exit", __FUNCTION__);
}


/*******************************************************************************
**
** Function:        nfcManager_disableDiscovery
**
** Description:     Stop polling and listening for devices.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
void nfcManager_disableDiscovery (JNIEnv* e, jobject o)
{
    tNFA_STATUS status = NFA_STATUS_OK;
    unsigned long num = 0;
    unsigned long p2p_listen_mask =0;
    tNFA_HANDLE handle = NFA_HANDLE_INVALID;
    ALOGD ("%s: enter;", __FUNCTION__);

    if(get_transcation_stat() == true)
    {
        ALOGD("Transcatin is in progress store the request");
        set_last_request(2, NULL);
        return;
    }
    pn544InteropAbortNow ();
    if (sDiscoveryEnabled == false)
    {
        ALOGD ("%s: already disabled", __FUNCTION__);
        goto TheEnd;
    }



    // Stop RF Discovery.
    startRfDiscovery (false);

    if (sPollingEnabled)
        status = stopPolling_rfDiscoveryDisabled();
    sDiscoveryEnabled = false;

    if ((GetNumValue(NAME_UICC_LISTEN_TECH_MASK, &num, sizeof(num))))
    {
        ALOGE ("%s:UICC_LISTEN_MASK=0x0%d;", __FUNCTION__, num);
    }
    if ((GetNumValue("P2P_LISTEN_TECH_MASK", &p2p_listen_mask, sizeof(p2p_listen_mask))))
    {
        ALOGE ("%s:P2P_LISTEN_MASK=0x0%d;", __FUNCTION__, p2p_listen_mask);
    }

    PeerToPeer::getInstance().enableP2pListening (false);
#if 0 //EEPROM Init optimization
    {
        UINT8 sel_info = 0x20;
        UINT8 lf_protocol = 0x00;
        {
            SyncEventGuard guard (android::sNfaSetConfigEvent);
            status = NFA_SetConfig(NCI_PARAM_ID_LA_SEL_INFO, sizeof(UINT8), &sel_info);
            if (status == NFA_STATUS_OK)
                sNfaSetConfigEvent.wait ();
            else
                ALOGE ("%s: Could not able to configure sel_info", __FUNCTION__);
        }

        {
            SyncEventGuard guard (android::sNfaSetConfigEvent);
            status = NFA_SetConfig(NCI_PARAM_ID_LF_PROTOCOL, sizeof(UINT8), &lf_protocol);
            if (status == NFA_STATUS_OK)
                sNfaSetConfigEvent.wait ();
            else
                ALOGE ("%s: Could not able to configure lf_protocol", __FUNCTION__);
        }
    }

#endif //EEPROM Init optimization
  /*
    {
        StoreScreenState(1);
        status = SetScreenState(1);
        if (status != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail disable SetScreenState; error=0x%X", __FUNCTION__, status);
        }
    }*/

    //To support card emulation in screen off state.
//    if (SecureElement::getInstance().isBusy() == true )
    if (sIsSecElemSelected && (sHCEEnabled == false ))
    {
        handle = SecureElement::getInstance().getEseHandleFromGenericId(SecureElement::UICC_ID);
        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            status = NFA_CeConfigureUiccListenTech (handle, 0x00);
            if (status == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }

        {
            SyncEventGuard guard (SecureElement::getInstance().mUiccListenEvent);
            status = NFA_CeConfigureUiccListenTech (handle, (num & 0x07));
            if(status == NFA_STATUS_OK)
            {
                SecureElement::getInstance().mUiccListenEvent.wait ();
            }
            else
                ALOGE ("fail to start UICC listen");
        }

        //PeerToPeer::getInstance().setP2pListenMask(p2p_listen_mask & 0x05);
        //PeerToPeer::getInstance().enableP2pListening (true);
        PeerToPeer::getInstance().enableP2pListening (false);
    }
    startRfDiscovery (true);
    //if nothing is active after this, then tell the controller to power down
    //if (! PowerSwitch::getInstance ().setModeOff (PowerSwitch::DISCOVERY))
        //PowerSwitch::getInstance ().setLevel (PowerSwitch::LOW_POWER);

    // We may have had RF field notifications that did not cause
    // any activate/deactive events. For example, caused by wireless
    // charging orbs. Those may cause us to go to sleep while the last
    // field event was indicating a field. To prevent sticking in that
    // state, always reset the rf field status when we disable discovery.
    SecureElement::getInstance().resetRfFieldStatus();
TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
}

void enableDisableLptd (bool enable)
{
    // This method is *NOT* thread-safe. Right now
    // it is only called from the same thread so it's
    // not an issue.
    static bool sCheckedLptd = false;
    static bool sHasLptd = false;

    tNFA_STATUS stat = NFA_STATUS_OK;
    if (!sCheckedLptd)
    {
        sCheckedLptd = true;
        SyncEventGuard guard (sNfaGetConfigEvent);
        tNFA_PMID configParam[1] = {NCI_PARAM_ID_TAGSNIFF_CFG};
        stat = NFA_GetConfig(1, configParam);
        if (stat != NFA_STATUS_OK)
        {
            ALOGE("%s: NFA_GetConfig failed", __FUNCTION__);
            return;
        }
        sNfaGetConfigEvent.wait ();
        if (sCurrentConfigLen < 4 || sConfig[1] != NCI_PARAM_ID_TAGSNIFF_CFG) {
            ALOGE("%s: Config TLV length %d returned is too short", __FUNCTION__,
                    sCurrentConfigLen);
            return;
        }
        if (sConfig[3] == 0) {
            ALOGE("%s: LPTD is disabled, not enabling in current config", __FUNCTION__);
            return;
        }
        sHasLptd = true;
    }
    // Bail if we checked and didn't find any LPTD config before
    if (!sHasLptd) return;
    UINT8 enable_byte = enable ? 0x01 : 0x00;

    SyncEventGuard guard(sNfaSetConfigEvent);

    stat = NFA_SetConfig(NCI_PARAM_ID_TAGSNIFF_CFG, 1, &enable_byte);
    if (stat == NFA_STATUS_OK)
        sNfaSetConfigEvent.wait ();
    else
        ALOGE("%s: Could not configure LPTD feature", __FUNCTION__);
    return;
}

void setUiccIdleTimeout (bool enable)
{
    // This method is *NOT* thread-safe. Right now
    // it is only called from the same thread so it's
    // not an issue.
    tNFA_STATUS stat = NFA_STATUS_OK;
    UINT8 swp_cfg_byte0 = 0x00;
    {
        SyncEventGuard guard (sNfaGetConfigEvent);
        tNFA_PMID configParam[1] = {0xC2};
        stat = NFA_GetConfig(1, configParam);
        if (stat != NFA_STATUS_OK)
        {
            ALOGE("%s: NFA_GetConfig failed", __FUNCTION__);
            return;
        }
        sNfaGetConfigEvent.wait ();
        if (sCurrentConfigLen < 4 || sConfig[1] != 0xC2) {
            ALOGE("%s: Config TLV length %d returned is too short", __FUNCTION__,
                    sCurrentConfigLen);
            return;
        }
        swp_cfg_byte0 = sConfig[3];
    }
    SyncEventGuard guard(sNfaSetConfigEvent);
    if (enable)
        swp_cfg_byte0 |= 0x01;
    else
        swp_cfg_byte0 &= ~0x01;

    stat = NFA_SetConfig(0xC2, 1, &swp_cfg_byte0);
    if (stat == NFA_STATUS_OK)
        sNfaSetConfigEvent.wait ();
    else
        ALOGE("%s: Could not configure UICC idle timeout feature", __FUNCTION__);
    return;
}


/*******************************************************************************
**
** Function:        nfcManager_doCreateLlcpServiceSocket
**
** Description:     Create a new LLCP server socket.
**                  e: JVM environment.
**                  o: Java object.
**                  nSap: Service access point.
**                  sn: Service name
**                  miu: Maximum information unit.
**                  rw: Receive window size.
**                  linearBufferLength: Max buffer size.
**
** Returns:         NativeLlcpServiceSocket Java object.
**
*******************************************************************************/
static jobject nfcManager_doCreateLlcpServiceSocket (JNIEnv* e, jobject, jint nSap, jstring sn, jint miu, jint rw, jint linearBufferLength)
{
    PeerToPeer::tJNI_HANDLE jniHandle = PeerToPeer::getInstance().getNewJniHandle ();

    ScopedUtfChars serviceName(e, sn);

    ALOGD ("%s: enter: sap=%i; name=%s; miu=%i; rw=%i; buffLen=%i", __FUNCTION__, nSap, serviceName.c_str(), miu, rw, linearBufferLength);

    /* Create new NativeLlcpServiceSocket object */
    jobject serviceSocket = NULL;
    if (nfc_jni_cache_object_local(e, gNativeLlcpServiceSocketClassName, &(serviceSocket)) == -1)
    {
        ALOGE ("%s: Llcp socket object creation error", __FUNCTION__);
        return NULL;
    }

    /* Get NativeLlcpServiceSocket class object */
    ScopedLocalRef<jclass> clsNativeLlcpServiceSocket(e, e->GetObjectClass(serviceSocket));
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE("%s: Llcp Socket get object class error", __FUNCTION__);
        return NULL;
    }

    if (!PeerToPeer::getInstance().registerServer (jniHandle, serviceName.c_str()))
    {
        ALOGE("%s: RegisterServer error", __FUNCTION__);
        return NULL;
    }

    jfieldID f;

    /* Set socket handle to be the same as the NfaHandle*/
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mHandle", "I");
    e->SetIntField(serviceSocket, f, (jint) jniHandle);
    ALOGD ("%s: socket Handle = 0x%X", __FUNCTION__, jniHandle);

    /* Set socket linear buffer length */
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mLocalLinearBufferLength", "I");
    e->SetIntField(serviceSocket, f,(jint)linearBufferLength);
    ALOGD ("%s: buffer length = %d", __FUNCTION__, linearBufferLength);

    /* Set socket MIU */
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mLocalMiu", "I");
    e->SetIntField(serviceSocket, f,(jint)miu);
    ALOGD ("%s: MIU = %d", __FUNCTION__, miu);

    /* Set socket RW */
    f = e->GetFieldID(clsNativeLlcpServiceSocket.get(), "mLocalRw", "I");
    e->SetIntField(serviceSocket, f,(jint)rw);
    ALOGD ("%s:  RW = %d", __FUNCTION__, rw);

    sLastError = 0;
    ALOGD ("%s: exit", __FUNCTION__);
    return serviceSocket;
}


/*******************************************************************************
**
** Function:        nfcManager_doGetLastError
**
** Description:     Get the last error code.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Last error code.
**
*******************************************************************************/
static jint nfcManager_doGetLastError(JNIEnv*, jobject)
{
    ALOGD ("%s: last error=%i", __FUNCTION__, sLastError);
    return sLastError;
}


/*******************************************************************************
**
** Function:        nfcManager_doDeinitialize
**
** Description:     Turn off NFC.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_doDeinitialize (JNIEnv*, jobject)
{
    ALOGD ("%s: enter", __FUNCTION__);

    sIsDisabling = true;
    NFA_HciW4eSETransaction_Complete();
    pn544InteropAbortNow ();
    RoutingManager::getInstance().onNfccShutdown();
    SecureElement::getInstance().finalize ();
    PowerSwitch::getInstance ().initialize (PowerSwitch::UNKNOWN_LEVEL);
    //Stop the discovery before calling NFA_Disable.
    if(sRfEnabled)
        startRfDiscovery(false);
    tNFA_STATUS stat = NFA_STATUS_OK;

    if (sIsNfaEnabled)
    {
#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
        jint venconfig;
        if (gGeneralPowershutDown == VEN_CFG_NFC_OFF_POWER_OFF)
        {
            stat = SetVenConfigValue(gGeneralPowershutDown);
            if (stat != NFA_STATUS_OK)
            {
               ALOGE ("%s: fail enable SetVenConfigValue; error=0x%X", __FUNCTION__, stat);
            }
            ALOGE ("%s: set the VEN_CFG to %d", __FUNCTION__,gGeneralPowershutDown);
            gGeneralPowershutDown = 0;
        }
#endif
        SyncEventGuard guard (sNfaDisableEvent);
        EXTNS_Close();
        tNFA_STATUS stat = NFA_Disable (TRUE /* graceful */);
        if (stat == NFA_STATUS_OK)
        {
            ALOGD ("%s: wait for completion", __FUNCTION__);
            sNfaDisableEvent.wait (); //wait for NFA command to finish
            PeerToPeer::getInstance ().handleNfcOnOff (false);
        }
        else
        {
            ALOGE ("%s: fail disable; error=0x%X", __FUNCTION__, stat);
        }
    }
    NfcTag::getInstance ().mNfcDisableinProgress = true;
    nativeNfcTag_abortWaits();
    NfcTag::getInstance().abort ();
    sAbortConnlessWait = true;
    nativeLlcpConnectionlessSocket_abortWait();
    sIsNfaEnabled = false;
    sDiscoveryEnabled = false;
    sIsDisabling = false;
    sPollingEnabled = false;
//    sIsSecElemSelected = false;
    sIsSecElemSelected = 0;
    gActivated = false;

    {
        //unblock NFA_EnablePolling() and NFA_DisablePolling()
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        sNfaEnableDisablePollingEvent.notifyOne ();
    }

    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    theInstance.Finalize();

    ALOGD ("%s: exit", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doCreateLlcpSocket
**
** Description:     Create a LLCP connection-oriented socket.
**                  e: JVM environment.
**                  o: Java object.
**                  nSap: Service access point.
**                  miu: Maximum information unit.
**                  rw: Receive window size.
**                  linearBufferLength: Max buffer size.
**
** Returns:         NativeLlcpSocket Java object.
**
*******************************************************************************/
static jobject nfcManager_doCreateLlcpSocket (JNIEnv* e, jobject, jint nSap, jint miu, jint rw, jint linearBufferLength)
{
    ALOGD ("%s: enter; sap=%d; miu=%d; rw=%d; buffer len=%d", __FUNCTION__, nSap, miu, rw, linearBufferLength);

    PeerToPeer::tJNI_HANDLE jniHandle = PeerToPeer::getInstance().getNewJniHandle ();
    PeerToPeer::getInstance().createClient (jniHandle, miu, rw);

    /* Create new NativeLlcpSocket object */
    jobject clientSocket = NULL;
    if (nfc_jni_cache_object_local(e, gNativeLlcpSocketClassName, &(clientSocket)) == -1)
    {
        ALOGE ("%s: fail Llcp socket creation", __FUNCTION__);
        return clientSocket;
    }

    /* Get NativeConnectionless class object */
    ScopedLocalRef<jclass> clsNativeLlcpSocket(e, e->GetObjectClass(clientSocket));
    if (e->ExceptionCheck())
    {
        e->ExceptionClear();
        ALOGE ("%s: fail get class object", __FUNCTION__);
        return clientSocket;
    }

    jfieldID f;

    /* Set socket SAP */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mSap", "I");
    e->SetIntField (clientSocket, f, (jint) nSap);

    /* Set socket handle */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mHandle", "I");
    e->SetIntField (clientSocket, f, (jint) jniHandle);

    /* Set socket MIU */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mLocalMiu", "I");
    e->SetIntField (clientSocket, f, (jint) miu);

    /* Set socket RW */
    f = e->GetFieldID (clsNativeLlcpSocket.get(), "mLocalRw", "I");
    e->SetIntField (clientSocket, f, (jint) rw);

    ALOGD ("%s: exit", __FUNCTION__);
    return clientSocket;
}


/*******************************************************************************
**
** Function:        nfcManager_doCreateLlcpConnectionlessSocket
**
** Description:     Create a connection-less socket.
**                  e: JVM environment.
**                  o: Java object.
**                  nSap: Service access point.
**                  sn: Service name.
**
** Returns:         NativeLlcpConnectionlessSocket Java object.
**
*******************************************************************************/
static jobject nfcManager_doCreateLlcpConnectionlessSocket (JNIEnv *, jobject, jint nSap, jstring /*sn*/)
{
    ALOGD ("%s: nSap=0x%X", __FUNCTION__, nSap);
    return NULL;
}


/*******************************************************************************
**
** Function:        nfcManager_doGetSecureElementList
**
** Description:     Get a list of secure element handles.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         List of secure element handles.
**
*******************************************************************************/
static jintArray nfcManager_doGetSecureElementList(JNIEnv* e, jobject)
{
    ALOGD ("%s", __FUNCTION__);
    return SecureElement::getInstance().getListOfEeHandles(e);
}

/*******************************************************************************
**
** Function:        setListenMode
**
** Description:     NFC controller starts routing data in listen mode.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void setListenMode()
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_HANDLE ee_handleList[NFA_EE_MAX_EE_SUPPORTED];
    UINT8 i, seId, count;

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    SecureElement::getInstance().getEeHandleList(ee_handleList, &count);
    if (count > NFA_EE_MAX_EE_SUPPORTED) {
    count = NFA_EE_MAX_EE_SUPPORTED;
        ALOGD ("Count is more than NFA_EE_MAX_EE_SUPPORTED ,Forcing to NFA_EE_MAX_EE_SUPPORTED");
    }
    for ( i = 0; i < count; i++)
    {
        seId = SecureElement::getInstance().getGenericEseId(ee_handleList[i]);
        SecureElement::getInstance().activate (seId);
        sIsSecElemSelected++;
    }

    startRfDiscovery (true);
    PowerSwitch::getInstance ().setModeOn (PowerSwitch::SE_ROUTING);
//TheEnd:                           /*commented to eliminate warning label defined but not used*/
    ALOGD ("%s: exit", __FUNCTION__);
}


/*******************************************************************************
**
** Function:        nfcManager_doSelectSecureElement
**
** Description:     NFC controller starts routing data in listen mode.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSelectSecureElement(JNIEnv *e, jobject o, jint seId)
{
    ALOGD ("%s: enter", __FUNCTION__);
    bool stat = true;

    if (sIsSecElemSelected >= sIsSecElemDetected)
    {
        ALOGD ("%s: already selected", __FUNCTION__);
        goto TheEnd;
    }

    PowerSwitch::getInstance ().setLevel (PowerSwitch::FULL_POWER);

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

//    stat = SecureElement::getInstance().activate (0xABCDEF);
    stat = SecureElement::getInstance().activate (seId);
    if (stat)
    {
        SecureElement::getInstance().routeToSecureElement();
        sIsSecElemSelected++;
     //   if(sHCEEnabled == false)
      //  {
        //    RoutingManager::getInstance().setRouting(false);
       // }
    }
//    sIsSecElemSelected = true;

    startRfDiscovery (true);
    PowerSwitch::getInstance ().setModeOn (PowerSwitch::SE_ROUTING);
TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
}

/*******************************************************************************
**
** Function:        nfcManager_doSetSEPowerOffState
**
** Description:     NFC controller enable/disabe card emulation in power off
**                  state from EE.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSetSEPowerOffState(JNIEnv *e, jobject o, jint seId, jboolean enable)
{
    tNFA_HANDLE ee_handle;
    UINT8 power_state_mask = ~NFA_EE_PWR_STATE_SWITCH_OFF;

    if(enable == true)
    {
        power_state_mask = NFA_EE_PWR_STATE_SWITCH_OFF;
    }

    ee_handle = SecureElement::getInstance().getEseHandleFromGenericId(seId);

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }

    tNFA_STATUS status = NFA_AddEePowerState(ee_handle,power_state_mask);


    // Commit the routing configuration
    status |= NFA_EeUpdateNow();

    if (status != NFA_STATUS_OK)
        ALOGE("Failed to commit routing configuration");

    startRfDiscovery (true);

//    TheEnd:                   /*commented to eliminate warning label defined but not used*/
        ALOGD ("%s: exit", __FUNCTION__);

}


/*******************************************************************************
**
** Function:        nfcManager_GetDefaultSE
**
** Description:     Get default Secure Element.
**
**
** Returns:         Returns 0.
**
*******************************************************************************/
static jint nfcManager_GetDefaultSE(JNIEnv *e, jobject o)
{
    unsigned long num;
    GetNxpNumValue (NAME_NXP_DEFAULT_SE, (void*)&num, sizeof(num));
    ALOGD ("%d: nfcManager_GetDefaultSE", num);
    return num;
}


static jint nfcManager_getSecureElementTechList(JNIEnv *e, jobject o)
{

    uint8_t sak;
    jint tech = 0x00;
    ALOGD ("nfcManager_getSecureElementTechList -Enter");
    sak = HciRFParams::getInstance().getESeSak();
    bool isTypeBPresent = HciRFParams::getInstance().isTypeBSupported();

    ALOGD ("nfcManager_getSecureElementTechList - sak is %0x", sak);

    if(sak & 0x08)
    {
        tech |= TARGET_TYPE_MIFARE_CLASSIC;
    }

    if( sak & 0x20 )
    {
        tech |= NFA_TECHNOLOGY_MASK_A;
    }

    if( isTypeBPresent == true)
    {
        tech |= NFA_TECHNOLOGY_MASK_B;
    }
    ALOGD ("nfcManager_getSecureElementTechList - tech is %0x", tech);
    return tech;

}

static void nfcManager_setSecureElementListenTechMask(JNIEnv *e, jobject o, jint tech_mask)
{
    ALOGD ("%s: ENTER", __FUNCTION__);
//    tNFA_STATUS status;                   /*commented to eliminate unused variable warning*/

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    SecureElement::getInstance().setEseListenTechMask(tech_mask);

    startRfDiscovery (true);

    ALOGD ("%s: EXIT", __FUNCTION__);
}


static jbyteArray nfcManager_getSecureElementUid(JNIEnv *e, jobject o)
{
    unsigned long num=0;
    jbyteArray jbuff = NULL;
    uint8_t bufflen = 0;
    uint8_t buf[16] = {0,};
    ALOGD ("nfcManager_getSecureElementUid -Enter");
    HciRFParams::getInstance().getESeUid(&buf[0], &bufflen);
    if(bufflen > 0)
     {
       jbuff = e->NewByteArray (bufflen);
       e->SetByteArrayRegion (jbuff, 0, bufflen, (jbyte*) buf);
     }
    return jbuff;
}


static tNFA_STATUS nfcManager_setEmvCoPollProfile(JNIEnv *e, jobject o,
        jboolean enable, jint route)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_TECHNOLOGY_MASK tech_mask = 0;

    ALOGE("In nfcManager_setEmvCoPollProfile enable = 0x%x route = 0x%x", enable, route);
    /* Stop polling */
    if ( isDiscoveryStarted())
    {
        // Stop RF discovery to reconfigure
        startRfDiscovery(false);
    }

    status = EmvCo_dosetPoll(enable);
    if (status != NFA_STATUS_OK)
    {
        ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, status);
        goto TheEnd;
    }

    if (enable)
    {
        if (route == 0x00)
        {
            /* DH enable polling for A and B*/
            tech_mask = NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B;
        }
        else if(route == 0x01)
        {
            /* UICC is end-point at present not supported by FW */
            /* TBD : Get eeinfo (use handle appropirately, depending up
             * on it enable the polling */
        }
        else if(route == 0x02)
        {
            /* ESE is end-point at present not supported by FW */
            /* TBD : Get eeinfo (use handle appropirately, depending up
             * on it enable the polling */
        }
        else
        {

        }
    }
    else
    {
        unsigned long num = 0;
        if (GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
            tech_mask = num;
    }

    ALOGD ("%s: enable polling", __FUNCTION__);
    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        status = NFA_EnablePolling (tech_mask);
        if (status == NFA_STATUS_OK)
        {
            ALOGD ("%s: wait for enable event", __FUNCTION__);
            sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_ENABLED_EVT
        }
        else
        {
            ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, status);
        }
    }

TheEnd:
    /* start polling */
    if ( !isDiscoveryStarted())
    {
        // Start RF discovery to reconfigure
        startRfDiscovery(true);
    }
    return status;

}


/*******************************************************************************
**
** Function:        nfcManager_doDeselectSecureElement
**
** Description:     NFC controller stops routing data in listen mode.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doDeselectSecureElement(JNIEnv *e, jobject o,  jint seId)
{
    ALOGD ("%s: enter", __FUNCTION__);
    bool stat = false;
    bool bRestartDiscovery = false;

    if (! sIsSecElemSelected)
    {
        ALOGE ("%s: already deselected", __FUNCTION__);
        goto TheEnd2;
    }

    if (PowerSwitch::getInstance ().getLevel() == PowerSwitch::LOW_POWER)
    {
        ALOGD ("%s: do not deselect while power is OFF", __FUNCTION__);
//        sIsSecElemSelected = false;
        sIsSecElemSelected--;
        goto TheEnd;
    }

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
        bRestartDiscovery = true;
    }

    //if controller is not routing to sec elems AND there is no pipe connected,
    //then turn off the sec elems
    if (SecureElement::getInstance().isBusy() == false)
    {
        //SecureElement::getInstance().deactivate (0xABCDEF);
        stat = SecureElement::getInstance().deactivate (seId);
        if(stat)
        {
            sIsSecElemSelected--;
//            RoutingManager::getInstance().commitRouting();
        }
    }

    NFA_EeUpdateNow();

TheEnd:
    if (bRestartDiscovery)
        startRfDiscovery (true);

    //if nothing is active after this, then tell the controller to power down
    if (! PowerSwitch::getInstance ().setModeOff (PowerSwitch::SE_ROUTING))
        PowerSwitch::getInstance ().setLevel (PowerSwitch::LOW_POWER);

TheEnd2:
    ALOGD ("%s: exit", __FUNCTION__);
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
/*******************************************************************************
**
** Function:        nfcManager_setDefaultTechRoute
**
** Description:     Setting Default Technology Routing
**                  e:  JVM environment.
**                  o:  Java object.
**                  seId:  SecureElement Id
**                  tech_swithon:  technology switch_on
**                  tech_switchoff:  technology switch_off
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_setDefaultTechRoute(JNIEnv *e, jobject o, jint seId,
        jint tech_switchon, jint tech_switchoff)
{
    ALOGD ("%s: ENTER", __FUNCTION__);
//    tNFA_STATUS status;                   /*commented to eliminate unused variable warning*/

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    RoutingManager::getInstance().setDefaultTechRouting (seId, tech_switchon, tech_switchoff);
    // start discovery.
    startRfDiscovery (true);
}

/*******************************************************************************
**
** Function:        nfcManager_setDefaultProtoRoute
**
** Description:     Setting Default Protocol Routing
**
**                  e:  JVM environment.
**                  o:  Java object.
**                  seId:  SecureElement Id
**                  proto_swithon:  Protocol switch_on
**                  proto_switchoff:  Protocol switch_off
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_setDefaultProtoRoute(JNIEnv *e, jobject o, jint seId,
        jint proto_switchon, jint proto_switchoff)
{
    ALOGD ("%s: ENTER", __FUNCTION__);
//    tNFA_STATUS status;                   /*commented to eliminate unused variable warning*/
    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    RoutingManager::getInstance().setDefaultProtoRouting (seId, proto_switchon, proto_switchoff);
    // start discovery.
    startRfDiscovery (true);
}
#endif
/*******************************************************************************
**
** Function:        isPeerToPeer
**
** Description:     Whether the activation data indicates the peer supports NFC-DEP.
**                  activated: Activation data.
**
** Returns:         True if the peer supports NFC-DEP.
**
*******************************************************************************/
static bool isPeerToPeer (tNFA_ACTIVATED& activated)
{
    return activated.activate_ntf.protocol == NFA_PROTOCOL_NFC_DEP;
}

/*******************************************************************************
**
** Function:        isListenMode
**
** Description:     Indicates whether the activation data indicates it is
**                  listen mode.
**
** Returns:         True if this listen mode.
**
*******************************************************************************/
static bool isListenMode(tNFA_ACTIVATED& activated)
{
    return ((NFC_DISCOVERY_TYPE_LISTEN_A == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_B == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_F == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_A_ACTIVE == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_F_ACTIVE == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_ISO15693 == activated.activate_ntf.rf_tech_param.mode)
            || (NFC_DISCOVERY_TYPE_LISTEN_B_PRIME == activated.activate_ntf.rf_tech_param.mode));
}

/*******************************************************************************
**
** Function:        nfcManager_doCheckLlcp
**
** Description:     Not used.
**
** Returns:         True
**
*******************************************************************************/
static jboolean nfcManager_doCheckLlcp(JNIEnv*, jobject)
{
    ALOGD("%s", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doActivateLlcp
**
** Description:     Not used.
**
** Returns:         True
**
*******************************************************************************/
static jboolean nfcManager_doActivateLlcp(JNIEnv*, jobject)
{
    ALOGD("%s", __FUNCTION__);
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doAbort
**
** Description:     Not used.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doAbort(JNIEnv*, jobject)
{
    ALOGE("%s: abort()", __FUNCTION__);
    abort();
}


/*******************************************************************************
**
** Function:        nfcManager_doDownload
**
** Description:     Download firmware patch files.  Do not turn on NFC.
**
** Returns:         True if ok.
**
*******************************************************************************/
static jboolean nfcManager_doDownload(JNIEnv*, jobject)
{
    ALOGD("%s", __FUNCTION__);
    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    theInstance.InitializeHalDeviceContext();
    theInstance.DownloadFirmware();
    return JNI_TRUE;
}


/*******************************************************************************
**
** Function:        nfcManager_doResetTimeouts
**
** Description:     Not used.
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doResetTimeouts(JNIEnv*, jobject)
{
    ALOGD ("%s: %d millisec", __FUNCTION__, DEFAULT_GENERAL_TRANS_TIMEOUT);
    gGeneralTransceiveTimeout = DEFAULT_GENERAL_TRANS_TIMEOUT;
}


/*******************************************************************************
**
** Function:        nfcManager_doSetTimeout
**
** Description:     Set timeout value.
**                  e: JVM environment.
**                  o: Java object.
**                  tech: technology ID.
**                  timeout: Timeout value.
**
** Returns:         True if ok.
**
*******************************************************************************/
static bool nfcManager_doSetTimeout(JNIEnv*, jobject, jint /*tech*/, jint timeout)
{
    if (timeout <= 0)
    {
        ALOGE("%s: Timeout must be positive.",__FUNCTION__);
        return false;
    }

    ALOGD ("%s: timeout=%d", __FUNCTION__, timeout);
    gGeneralTransceiveTimeout = timeout;
    return true;
}


/*******************************************************************************
**
** Function:        nfcManager_doGetTimeout
**
** Description:     Get timeout value.
**                  e: JVM environment.
**                  o: Java object.
**                  tech: Not used.
**
** Returns:         Timeout value.
**
*******************************************************************************/
static jint nfcManager_doGetTimeout(JNIEnv*, jobject, jint /*tech*/)
{
    ALOGD ("%s: timeout=%d", __FUNCTION__, gGeneralTransceiveTimeout);
    return gGeneralTransceiveTimeout;
}


/*******************************************************************************
**
** Function:        nfcManager_doDump
**
** Description:     Not used.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         Text dump.
**
*******************************************************************************/
static jstring nfcManager_doDump(JNIEnv* e, jobject)
{
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "libnfc llc error_count=%u", /*libnfc_llc_error_count*/ 0);
    return e->NewStringUTF(buffer);
}


/*******************************************************************************
**
** Function:        nfcManager_doSetP2pInitiatorModes
**
** Description:     Set P2P initiator's activation modes.
**                  e: JVM environment.
**                  o: Java object.
**                  modes: Active and/or passive modes.  The values are specified
**                          in external/libnfc-nxp/inc/phNfcTypes.h.  See
**                          enum phNfc_eP2PMode_t.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doSetP2pInitiatorModes (JNIEnv *e, jobject o, jint modes)
{
    ALOGD ("%s: modes=0x%X", __FUNCTION__, modes);
    struct nfc_jni_native_data *nat = getNative(e, o);

    tNFA_TECHNOLOGY_MASK mask = 0;
    if (modes & 0x01) mask |= NFA_TECHNOLOGY_MASK_A;
    if (modes & 0x02) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x04) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x08) mask |= NFA_TECHNOLOGY_MASK_A_ACTIVE;
    if (modes & 0x10) mask |= NFA_TECHNOLOGY_MASK_F_ACTIVE;
    if (modes & 0x20) mask |= NFA_TECHNOLOGY_MASK_F_ACTIVE;
    nat->tech_mask = mask;
}


/*******************************************************************************
**
** Function:        nfcManager_doSetP2pTargetModes
**
** Description:     Set P2P target's activation modes.
**                  e: JVM environment.
**                  o: Java object.
**                  modes: Active and/or passive modes.
**
** Returns:         None.
**
*******************************************************************************/
static void nfcManager_doSetP2pTargetModes (JNIEnv*, jobject, jint modes)
{
    ALOGD ("%s: modes=0x%X", __FUNCTION__, modes);
    // Map in the right modes
    tNFA_TECHNOLOGY_MASK mask = 0;
    if (modes & 0x01) mask |= NFA_TECHNOLOGY_MASK_A;
    if (modes & 0x02) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x04) mask |= NFA_TECHNOLOGY_MASK_F;
    if (modes & 0x08) mask |= NFA_TECHNOLOGY_MASK_A_ACTIVE | NFA_TECHNOLOGY_MASK_F_ACTIVE;

    PeerToPeer::getInstance().setP2pListenMask(mask);
}

static void nfcManager_doEnableScreenOffSuspend(JNIEnv* e, jobject o)
{
    PowerSwitch::getInstance().setScreenOffPowerState(PowerSwitch::POWER_STATE_FULL);
}

static void nfcManager_doDisableScreenOffSuspend(JNIEnv* e, jobject o)
{
    PowerSwitch::getInstance().setScreenOffPowerState(PowerSwitch::POWER_STATE_OFF);
}

/*****************************************************************************
**
** JNI functions for android-4.0.1_r1
**
*****************************************************************************/
static JNINativeMethod gMethods[] =
{
    {"doDownload", "()Z",
            (void *)nfcManager_doDownload},

    {"initializeNativeStructure", "()Z",
            (void*) nfcManager_initNativeStruc},

    {"doInitialize", "()Z",
            (void*) nfcManager_doInitialize},

    {"doDeinitialize", "()Z",
            (void*) nfcManager_doDeinitialize},

    {"sendRawFrame", "([B)Z",
            (void*) nfcManager_sendRawFrame},

    {"doRouteAid", "([BIIZ)Z",
            (void*) nfcManager_routeAid},
    {"clearRouting", "()V",
            (void*) nfcManager_clearRouting},

    {"setDefaultRoute", "(III)Z",
            (void*) nfcManager_setDefaultRoute},

    {"getAidTableSize", "()I",
            (void*) nfcManager_getAidTableSize},

    {"getDefaultAidRoute", "()I",
            (void*) nfcManager_getDefaultAidRoute},

    {"getDefaultDesfireRoute", "()I",
            (void*) nfcManager_getDefaultDesfireRoute},

    {"getDefaultMifareCLTRoute", "()I",
            (void*) nfcManager_getDefaultMifareCLTRoute},


    {"doEnableDiscovery", "(IZZZZ)V",
            (void*) nfcManager_enableDiscovery},

    {"doGetSecureElementList", "()[I",
            (void *)nfcManager_doGetSecureElementList},

    {"doSelectSecureElement", "(I)V",
            (void *)nfcManager_doSelectSecureElement},

    {"doDeselectSecureElement", "(I)V",
            (void *)nfcManager_doDeselectSecureElement},

    {"doSetSEPowerOffState", "(IZ)V",
            (void *)nfcManager_doSetSEPowerOffState},
    {"setDefaultTechRoute", "(III)V",
            (void *)nfcManager_setDefaultTechRoute},

    {"setDefaultProtoRoute", "(III)V",
            (void *)nfcManager_setDefaultProtoRoute},

     {"setEmvCoPollProfile", "(ZI)I",
             (void *)nfcManager_setEmvCoPollProfile},
     {"GetDefaultSE","()I",
         (void *)nfcManager_GetDefaultSE},

    {"doCheckLlcp", "()Z",
            (void *)nfcManager_doCheckLlcp},

    {"doActivateLlcp", "()Z",
            (void *)nfcManager_doActivateLlcp},

    {"doCreateLlcpConnectionlessSocket", "(ILjava/lang/String;)Lcom/android/nfc/dhimpl/NativeLlcpConnectionlessSocket;",
            (void *)nfcManager_doCreateLlcpConnectionlessSocket},

    {"doCreateLlcpServiceSocket", "(ILjava/lang/String;III)Lcom/android/nfc/dhimpl/NativeLlcpServiceSocket;",
            (void*) nfcManager_doCreateLlcpServiceSocket},

    {"doCreateLlcpSocket", "(IIII)Lcom/android/nfc/dhimpl/NativeLlcpSocket;",
            (void*) nfcManager_doCreateLlcpSocket},

    {"doGetLastError", "()I",
            (void*) nfcManager_doGetLastError},

    {"disableDiscovery", "()V",
            (void*) nfcManager_disableDiscovery},

    {"doSetTimeout", "(II)Z",
            (void *)nfcManager_doSetTimeout},

    {"doGetTimeout", "(I)I",
            (void *)nfcManager_doGetTimeout},

    {"doResetTimeouts", "()V",
            (void *)nfcManager_doResetTimeouts},

    {"doAbort", "()V",
            (void *)nfcManager_doAbort},

    {"doSetP2pInitiatorModes", "(I)V",
            (void *)nfcManager_doSetP2pInitiatorModes},

    {"doSetP2pTargetModes", "(I)V",
            (void *)nfcManager_doSetP2pTargetModes},

    {"doEnableScreenOffSuspend", "()V",
            (void *)nfcManager_doEnableScreenOffSuspend},

    {"doDisableScreenOffSuspend", "()V",
            (void *)nfcManager_doDisableScreenOffSuspend},

    {"doDump", "()Ljava/lang/String;",
            (void *)nfcManager_doDump},

    {"getChipVer", "()I",
             (void *)nfcManager_getChipVer},

    {"JCOSDownload", "()I",
            (void *)nfcManager_doJcosDownload},

    {"doGetSecureElementTechList", "()I",
            (void *)nfcManager_getSecureElementTechList},

    {"doGetSecureElementUid", "()[B",
            (void *)nfcManager_getSecureElementUid},

    {"doSetSecureElementListenTechMask", "(I)V",
            (void *)nfcManager_setSecureElementListenTechMask},
    {"doSetScreenState", "(I)V",
            (void*)nfcManager_doSetScreenState},
    //Factory Test Code
    {"doPrbsOn", "(II)V",
            (void *)nfcManager_doPrbsOn},
    {"doPrbsOff", "()V",
            (void *)nfcManager_doPrbsOff},
    // SWP self test
    {"SWPSelfTest", "(I)I",
            (void *)nfcManager_SWPSelfTest},
    // check firmware version
    {"getFWVersion", "()I",
            (void *)nfcManager_getFwVersion},
    {"doSetEEPROM", "([B)V",
            (void*)nfcManager_doSetEEPROM},
    //Factory Test Code

    {"doSetVenConfigValue", "(I)V",
            (void *)nfcManager_doSetVenConfigValue},

    {"doSetScrnState", "(I)V",
            (void *)nfcManager_doSetScrnState}


};


/*******************************************************************************
**
** Function:        register_com_android_nfc_NativeNfcManager
**
** Description:     Regisgter JNI functions with Java Virtual Machine.
**                  e: Environment of JVM.
**
** Returns:         Status of registration.
**
*******************************************************************************/
int register_com_android_nfc_NativeNfcManager (JNIEnv *e)
{
    ALOGD ("%s: enter", __FUNCTION__);
    PowerSwitch::getInstance ().initialize (PowerSwitch::UNKNOWN_LEVEL);
    ALOGD ("%s: exit", __FUNCTION__);
    return jniRegisterNativeMethods (e, gNativeNfcManagerClassName, gMethods, NELEM (gMethods));
}


/*******************************************************************************
**
** Function:        startRfDiscovery
**
** Description:     Ask stack to start polling and listening for devices.
**                  isStart: Whether to start.
**
** Returns:         None
**
*******************************************************************************/
void startRfDiscovery(bool isStart)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;

    ALOGD ("%s: is start=%d", __FUNCTION__, isStart);
    SyncEventGuard guard (sNfaEnableDisablePollingEvent);
    status  = isStart ? NFA_StartRfDiscovery () : NFA_StopRfDiscovery ();
    if (status == NFA_STATUS_OK)
    {
        if(gGeneralPowershutDown == VEN_CFG_NFC_OFF_POWER_OFF)
            sDiscCmdwhleNfcOff = true;
        sNfaEnableDisablePollingEvent.wait (); //wait for NFA_RF_DISCOVERY_xxxx_EVT
        sRfEnabled = isStart;
        sDiscCmdwhleNfcOff = false;
    }
    else
    {
        ALOGE ("%s: Failed to start/stop RF discovery; error=0x%X", __FUNCTION__, status);
    }
}

/*******************************************************************************
**
** Function:        notifyPollingEventwhileNfcOff
**
** Description:     Notifies sNfaEnableDisablePollingEvent if tag operations
**                  is in progress at the time Nfc Off is in progress to avoid
**                  NFC off thread infinite block.
**
** Returns:         None
**
*******************************************************************************/
static void notifyPollingEventwhileNfcOff()
{
    ALOGD ("%s: sDiscCmdwhleNfcOff=%x", __FUNCTION__, sDiscCmdwhleNfcOff);
    if(sDiscCmdwhleNfcOff == true)
    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        sNfaEnableDisablePollingEvent.notifyOne ();
    }
}

/*******************************************************************************
**
** Function:        doStartupConfig
**
** Description:     Configure the NFC controller.
**
** Returns:         None
**
*******************************************************************************/
void doStartupConfig()
{
    struct nfc_jni_native_data *nat = getNative(0, 0);
    tNFA_STATUS stat = NFA_STATUS_FAILED;
    int actualLen = 0;

    // If polling for Active mode, set the ordering so that we choose Active over Passive mode first.
    if (nat && (nat->tech_mask & (NFA_TECHNOLOGY_MASK_A_ACTIVE | NFA_TECHNOLOGY_MASK_F_ACTIVE)))
    {
        UINT8  act_mode_order_param[] = { 0x01 };
        SyncEventGuard guard (sNfaSetConfigEvent);
        stat = NFA_SetConfig(NCI_PARAM_ID_ACT_ORDER, sizeof(act_mode_order_param), &act_mode_order_param[0]);
        if (stat == NFA_STATUS_OK)
            sNfaSetConfigEvent.wait ();
    }

    //configure RF polling frequency for each technology
    static tNFA_DM_DISC_FREQ_CFG nfa_dm_disc_freq_cfg;
    //values in the polling_frequency[] map to members of nfa_dm_disc_freq_cfg
    UINT8 polling_frequency [8] = {1, 1, 1, 1, 1, 1, 1, 1};
    actualLen = GetStrValue(NAME_POLL_FREQUENCY, (char*)polling_frequency, 8);
    if (actualLen == 8)
    {
        ALOGD ("%s: polling frequency", __FUNCTION__);
        memset (&nfa_dm_disc_freq_cfg, 0, sizeof(nfa_dm_disc_freq_cfg));
        nfa_dm_disc_freq_cfg.pa = polling_frequency [0];
        nfa_dm_disc_freq_cfg.pb = polling_frequency [1];
        nfa_dm_disc_freq_cfg.pf = polling_frequency [2];
        nfa_dm_disc_freq_cfg.pi93 = polling_frequency [3];
        nfa_dm_disc_freq_cfg.pbp = polling_frequency [4];
        nfa_dm_disc_freq_cfg.pk = polling_frequency [5];
        nfa_dm_disc_freq_cfg.paa = polling_frequency [6];
        nfa_dm_disc_freq_cfg.pfa = polling_frequency [7];
        p_nfa_dm_rf_disc_freq_cfg = &nfa_dm_disc_freq_cfg;
    }
}


/*******************************************************************************
**
** Function:        nfcManager_isNfcActive
**
** Description:     Used externaly to determine if NFC is active or not.
**
** Returns:         'true' if the NFC stack is running, else 'false'.
**
*******************************************************************************/
bool nfcManager_isNfcActive()
{
    return sIsNfaEnabled;
}

/*******************************************************************************
**
** Function:        startStopPolling
**
** Description:     Start or stop polling.
**                  isStartPolling: true to start polling; false to stop polling.
**
** Returns:         None.
**
*******************************************************************************/
void startStopPolling (bool isStartPolling)
{
    ALOGD ("%s: enter; isStart=%u", __FUNCTION__, isStartPolling);
    startRfDiscovery (false);

    if (isStartPolling) startPolling_rfDiscoveryDisabled(0);
    else stopPolling_rfDiscoveryDisabled();

    startRfDiscovery (true);
    ALOGD ("%s: exit", __FUNCTION__);
}


static tNFA_STATUS startPolling_rfDiscoveryDisabled(tNFA_TECHNOLOGY_MASK tech_mask) {
    tNFA_STATUS stat = NFA_STATUS_FAILED;

    unsigned long num = 0;

    if (tech_mask == 0 && GetNumValue(NAME_POLLING_TECH_MASK, &num, sizeof(num)))
        tech_mask = num;
    else if (tech_mask == 0) tech_mask = DEFAULT_TECH_MASK;

    SyncEventGuard guard (sNfaEnableDisablePollingEvent);
    ALOGD ("%s: enable polling", __FUNCTION__);
    stat = NFA_EnablePolling (tech_mask);
    if (stat == NFA_STATUS_OK)
    {
        ALOGD ("%s: wait for enable event", __FUNCTION__);
        sPollingEnabled = true;
        sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_ENABLED_EVT
    }
    else
    {
        ALOGE ("%s: fail enable polling; error=0x%X", __FUNCTION__, stat);
    }

    return stat;
}

static tNFA_STATUS stopPolling_rfDiscoveryDisabled() {
    tNFA_STATUS stat = NFA_STATUS_FAILED;

    SyncEventGuard guard (sNfaEnableDisablePollingEvent);
    ALOGD ("%s: disable polling", __FUNCTION__);
    stat = NFA_DisablePolling ();
    if (stat == NFA_STATUS_OK) {
        sPollingEnabled = false;
        sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_DISABLED_EVT
    } else {
        ALOGE ("%s: fail disable polling; error=0x%X", __FUNCTION__, stat);
    }

    return stat;
}


/*******************************************************************************
**
** Function:        nfcManager_getChipVer
**
** Description:     Gets the chip version.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         None      0x00
**                  PN547C2   0x01
**                  PN65T     0x02 .
**
*******************************************************************************/
static int nfcManager_getChipVer(JNIEnv* e, jobject o)
{
    ALOGD ("%s: enter", __FUNCTION__);
    unsigned long num =0;

    GetNxpNumValue(NAME_NXP_NFC_CHIP, (void *)&num, sizeof(num));
    ALOGD ("%d: nfcManager_getChipVer", num);
    return num;
}
/*******************************************************************************
**
** Function:        DWPChannel_init
**
** Description:     Initializes the DWP channel functions.
**
** Returns:         True if ok.
**
*******************************************************************************/
#ifdef NFC_NXP_P61
void DWPChannel_init(IChannel_t *DWP)
{
    ALOGD ("%s: enter", __FUNCTION__);
    DWP->open = open;
    DWP->close = close;
    DWP->transceive = transceive;
    DWP->doeSE_Reset = doeSE_Reset;
}
#endif
/*******************************************************************************
**
** Function:        nfcManager_doJcosDownload
**
** Description:     start jcos download.
**                  e: JVM environment.
**                  o: Java object.
**
** Returns:         True if ok.
**
*******************************************************************************/
static int nfcManager_doJcosDownload(JNIEnv* e, jobject o)
{
#ifdef NFC_NXP_P61
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status, tStatus;
    bool stat = false;
    UINT8 param;
    status, tStatus= NFA_STATUS_FAILED;

    if (sRfEnabled) {
        // Stop RF Discovery if we were polling
        startRfDiscovery (false);
    }
    DWPChannel_init(&Dwp);
    status = JCDNLD_Init(&Dwp);
    if(status != NFA_STATUS_OK)
    {
        ALOGE("%s: JCDND initialization failed", __FUNCTION__);
    }
    else
    {
        // Commented the Disable standby
        /* param = 0x00; //Disable standby
        SyncEventGuard guard (sNfaVSCResponseEvent);
        tStatus = NFA_SendVsCommand (0x00,sizeof(param),&param,nfaVSCCallback);
        if(NFA_STATUS_OK == tStatus)
        {
            sNfaVSCResponseEvent.wait(); //wait for NFA VS command to finish
            ALOGE("%s: start JcopOs_Download", __FUNCTION__);
            status = JCDNLD_StartDownload();
        }*/
        ALOGE("%s: start JcopOs_Download", __FUNCTION__);
        status = JCDNLD_StartDownload();
    }

    // Commented the Enable standby
    /*  param = 0x01; //Enable standby
    SyncEventGuard guard (sNfaVSCResponseEvent);
    tStatus = NFA_SendVsCommand (0x00,sizeof(param),&param,nfaVSCCallback);
    if(NFA_STATUS_OK == tStatus)
    {
         sNfaVSCResponseEvent.wait(); //wait for NFA VS command to finish

    } */
    stat = JCDNLD_DeInit();

    startRfDiscovery (true);

    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);
#else
    tNFA_STATUS status = 0x0F;
    ALOGD ("%s: No p61", __FUNCTION__);
#endif
    return status;
}

void nfcManager_doSetVenConfigValue(JNIEnv *e, jobject o, jint venconfig)
{
    /* Store the shutdown state */
    gGeneralPowershutDown = venconfig;
}


void nfcManager_doSetScrnState(JNIEnv *e, jobject o, jint Enable)
{
       SetScreenState(Enable);
}

bool isDiscoveryStarted()
{
    return sRfEnabled;
}
/*******************************************************************************
**
** Function:        StoreScreenState
**
** Description:     Sets  screen state
**
** Returns:         None
**
*******************************************************************************/
static void StoreScreenState(int state)
{
    screenstate = state;
}


/*******************************************************************************
**
** Function:        getScreenState
**
** Description:     returns screen state
**
** Returns:         int
**
*******************************************************************************/
int getScreenState()
{
    return screenstate;
}
/*******************************************************************************
**
** Function:        nfcManager_doSetScreenState
**
** Description:     Set screen state
**
** Returns:         None
**
*******************************************************************************/
static void nfcManager_doSetScreenState (JNIEnv* e, jobject o, jint state)
{
    tNFA_STATUS status = NFA_STATUS_OK;

    ALOGD ("%s: state = %d", __FUNCTION__, state);
    if(get_transcation_stat() == true)
    {
        ALOGD("Payment is in progress stopping enable/disable discovery");
        set_lastScreenStateRequest((eScreenState_t)state);
        return;
    }
    int old = getScreenState();
    if(old == state) {
        ALOGD("Screen state is not changed.");
        return;
    }

    if (state) {
        if (sRfEnabled) {
            // Stop RF discovery to reconfigure
            startRfDiscovery(false);
        }
    if(state == NFA_SCREEN_STATE_LOCKED || state == NFA_SCREEN_STATE_OFF)
    {
        SyncEventGuard guard (sNfaEnableDisablePollingEvent);
        status = NFA_DisablePolling ();
        if (status == NFA_STATUS_OK)
        {
            sNfaEnableDisablePollingEvent.wait (); //wait for NFA_POLL_DISABLED_EVT
        }else
        ALOGE ("%s: Failed to disable polling; error=0x%X", __FUNCTION__, status);
    }

        status = SetScreenState(state);
        if (status != NFA_STATUS_OK)
        {
            ALOGE ("%s: fail enable SetScreenState; error=0x%X", __FUNCTION__, status);
        }
        if ((old == NFA_SCREEN_STATE_OFF && state == NFA_SCREEN_STATE_LOCKED)||
            (old == NFA_SCREEN_STATE_LOCKED && state == NFA_SCREEN_STATE_OFF))
        {
            startRfDiscovery(true);
        }

        StoreScreenState(state);
    }

}

/*******************************************************************************
 **
 ** Function:       get_last_request
 **
 ** Description:    returns the last enable/disable discovery event
 **
 ** Returns:        last request (char) .
 **
 *******************************************************************************/
static char get_last_request()
{
    return(transaction_data.last_request);
}
/*******************************************************************************
 **
 ** Function:       set_last_request
 **
 ** Description:    stores the last enable/disable discovery event
 **
 ** Returns:        None .
 **
 *******************************************************************************/
static void set_last_request(char status, struct nfc_jni_native_data *nat)
{
    transaction_data.last_request = status;
    if (nat != NULL)
    {
        transaction_data.transaction_nat = nat;
    }
}
/*******************************************************************************
 **
 ** Function:       set_transcation_stat
 **
 ** Description:    updates the transaction status
 **
 ** Returns:        None .
 **
 *******************************************************************************/
static void set_transcation_stat(bool result)
{
    transaction_data.trans_in_progress = result;
}

/*******************************************************************************
 **
 ** Function:       get_lastScreenStateRequest
 **
 ** Description:    returns the last screen state request
 **
 ** Returns:        last screen state request event (eScreenState_t) .
 **
 *******************************************************************************/
static eScreenState_t get_lastScreenStateRequest()
{
    ALOGD ("%s: %d", __FUNCTION__, transaction_data.last_screen_state_request);
    return(transaction_data.last_screen_state_request);
}

/*******************************************************************************
 **
 ** Function:       set_lastScreenStateRequest
 **
 ** Description:    stores the last screen state request
 **
 ** Returns:        None .
 **
 *******************************************************************************/
static void set_lastScreenStateRequest(eScreenState_t status)
{
    ALOGD ("%s: current=%d, new=%d", __FUNCTION__, transaction_data.last_screen_state_request, status);
    transaction_data.last_screen_state_request = status;
}

/*******************************************************************************
**
** Function:        switchBackTimerProc_transaction
**
** Description:     Callback function for interval timer.
**
** Returns:         None
**
*******************************************************************************/
static void cleanupTimerProc_transaction(union sigval)
{
    ALOGD("Inside cleanupTimerProc");
    cleanup_timer();
}

void cleanup_timer()
{
    ALOGD("Inside cleanup");
    //set_transcation_stat(false);
    pthread_t transaction_thread;
    int irret = -1;
    ALOGD ("%s", __FUNCTION__);

    /* Transcation is done process the last request*/
    irret = pthread_create(&transaction_thread, NULL, enableThread, NULL);
    if(irret != 0)
    {
        ALOGD("Unable to create the thread");
    }
    transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_END;
}

/*******************************************************************************
 **
 ** Function:       get_transcation_stat
 **
 ** Description:    returns the transaction status whether it is in progress
 **
 ** Returns:        bool .
 **
 *******************************************************************************/
static bool get_transcation_stat(void)
{
    return( transaction_data.trans_in_progress);
}

/*******************************************************************************
 **
 **
 ** Function:        checkforTranscation
 **
 ** Description:     Receive connection-related events from stack.
 **                  connEvent: Event code.
 **                  eventData: Event data.
 **
 ** Returns:         None
 **
 *******************************************************************************/
void checkforTranscation(UINT8 connEvent, void* eventData)
{
    tNFA_CONN_EVT_DATA* eventTrans_data = (tNFA_CONN_EVT_DATA *) eventData;
    tNFA_DM_CBACK_DATA* eventDM_Conn_data = (tNFA_DM_CBACK_DATA *) eventData;
    tNFA_EE_CBACK_DATA* ee_action_data = (tNFA_EE_CBACK_DATA *) eventData;
    tNFA_EE_ACTION& action = ee_action_data->action;

    UINT32 time_millisec = 0;
    pthread_t transaction_thread;
    int irret = -1;
    ALOGD ("%s: enter; event=0x%X transaction_data.current_transcation_state = 0x%x", __FUNCTION__, connEvent,
            transaction_data.current_transcation_state);
    switch(connEvent)
    {
    case NFA_EE_ACTION_EVT:
        {
            if(getScreenState() == NFA_SCREEN_STATE_OFF)
            {
                if (!sP2pActive && eventDM_Conn_data->rf_field.status == NFA_STATUS_OK)
                    SecureElement::getInstance().notifyRfFieldEvent (true);
            }
            if((action.param.technology == NFC_RF_TECHNOLOGY_A)&&(getScreenState () == NFA_SCREEN_STATE_OFF))
            {
                transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_END;
                memset(&transaction_data, 0x00, sizeof(Transcation_Check_t));
            }
            else
            {
                transaction_data.current_transcation_state = NFA_TRANS_EE_ACTION_EVT;
                set_transcation_stat(true);
            }
        }
        break;
    case NFA_TRANS_CE_ACTIVATED:
        {
            if(getScreenState() == NFA_SCREEN_STATE_OFF)
            {
                if (!sP2pActive && eventDM_Conn_data->rf_field.status == NFA_STATUS_OK)
                    SecureElement::getInstance().notifyRfFieldEvent (true);
            }
                transaction_data.current_transcation_state = NFA_TRANS_CE_ACTIVATED;
                set_transcation_stat(true);
        }
        break;
    case NFA_TRANS_CE_DEACTIVATED:
        if (transaction_data.current_transcation_state == NFA_TRANS_CE_ACTIVATED)
            {
                transaction_data.current_transcation_state = NFA_TRANS_CE_DEACTIVATED;
            }
        break;
    case NFA_TRANS_DM_RF_FIELD_EVT:
        if (eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                (transaction_data.current_transcation_state == NFA_TRANS_EE_ACTION_EVT
                        || transaction_data.current_transcation_state == NFA_TRANS_CE_DEACTIVATED)
                && eventDM_Conn_data->rf_field.rf_field_status == 0)
        {
            ALOGD("start_timer");
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_FIELD_EVT_OFF;
            scleanupTimerProc_transaction.set (50, cleanupTimerProc_transaction);
        }
        else if (eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                transaction_data.current_transcation_state == NFA_TRANS_DM_RF_FIELD_EVT_OFF &&
                eventDM_Conn_data->rf_field.rf_field_status == 1)
        {
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_FIELD_EVT_ON;
            ALOGD("Payment is in progress hold the screen on/off request ");
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_START;
            scleanupTimerProc_transaction.kill ();

        }
        else if (eventDM_Conn_data->rf_field.status == NFA_STATUS_OK &&
                transaction_data.current_transcation_state == NFA_TRANS_DM_RF_TRANS_START &&
                eventDM_Conn_data->rf_field.rf_field_status == 0)
        {
            ALOGD("Transcation is done");
            transaction_data.current_transcation_state = NFA_TRANS_DM_RF_TRANS_PROGRESS;
            //set_transcation_stat(false);
            cleanup_timer();
        }
        break;
    default:
        break;
    }

    ALOGD ("%s: exit; event=0x%X transaction_data.current_transcation_state = 0x%x", __FUNCTION__, connEvent,
            transaction_data.current_transcation_state);
}

/*******************************************************************************
 **
 ** Function:       enableThread
 **
 ** Description:    thread to trigger enable/disable discovery related events
 **
 ** Returns:        None .
 **
 *******************************************************************************/
void *enableThread(void *arg)
{
    ALOGD ("%s: enter", __FUNCTION__);
    char last_request = get_last_request();
    eScreenState_t last_screen_state_request = get_lastScreenStateRequest();
    set_transcation_stat(false);
    bool screen_lock_flag = false;
    bool disable_discovery = false;

    if(sIsNfaEnabled != true || sIsDisabling == true)
        goto TheEnd;

    if (last_screen_state_request != NFA_SCREEN_STATE_DEFAULT)
    {
        ALOGD("update last screen state request: %d", last_screen_state_request);
        nfcManager_doSetScreenState(NULL, NULL, last_screen_state_request);
        if( last_screen_state_request == 3)
            screen_lock_flag = true;
    }
    else
    {
        ALOGD("No request pending");
    }

    if (last_request == 1)
    {
        ALOGD("send the last request enable");
        sDiscoveryEnabled = false;
        sPollingEnabled = false;
        nfcManager_enableDiscovery(NULL, NULL, transaction_data.discovery_params.technologies_mask, transaction_data.discovery_params.enable_lptd, transaction_data.discovery_params.reader_mode, transaction_data.discovery_params.enable_host_routing,transaction_data.discovery_params.restart);
    }
    else if (last_request == 2)
    {
        ALOGD("send the last request disable");
        nfcManager_disableDiscovery(NULL, NULL);
        disable_discovery = true;
    }
    if(screen_lock_flag && disable_discovery)
    {
        startRfDiscovery(true);
    }
    screen_lock_flag = false;
    disable_discovery = false;
    memset(&transaction_data, 0x00, sizeof(Transcation_Check_t));

TheEnd:
    ALOGD ("%s: exit", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}

/*******************************************************************************
**
** Function         sig_handler
**
** Description      This function is used to handle the different types of
**                  signal events.
**
** Returns          None
**
*******************************************************************************/
void sig_handler(int signo)
{
    switch (signo)
    {
        case SIGINT:
            ALOGE("received SIGINT\n");
            break;
        case SIGABRT:
            ALOGE("received SIGABRT\n");
            NFA_HciW4eSETransaction_Complete();
            break;
        case SIGSEGV:
            ALOGE("received SIGSEGV\n");
            break;
        case SIGHUP:
            ALOGE("received SIGHUP\n");
            break;
    }
}
/**********************************************************************************
 **
 ** Function:       T3TPollThread
 **
 ** Description:    This thread sends commands to switch from P2P to T3T
 **                 When ReaderMode is enabled, When P2P is detected,Switch to T3T
 **                 with Frame RF interface and Poll for T3T
 **
 ** Returns:         None .
 **
 **********************************************************************************/
static void* T3TPollThread(void *arg)
{
    ALOGD ("%s: enter", __FUNCTION__);
    bool status=false;

    if (sReaderModeEnabled && (sTechMask == NFA_TECHNOLOGY_MASK_F))
    {
        /*Deactivate RF to go to W4_HOST_SELECT state
          Send Select Command to Switch to FrameRF interface from NFCDEP interface
          After NFC-DEP activation with FrameRF Intf, invoke T3T Polling Cmd*/
        {
            SyncEventGuard g (sRespCbEvent);
            if (NFA_STATUS_OK != (status = NFA_Deactivate (TRUE))) //deactivate to sleep state
            {
                ALOGE ("%s: deactivate failed, status = %d", __FUNCTION__, status);
            }
            if (sRespCbEvent.wait (2000) == false) //if timeout occurred
            {
                ALOGE ("%s: timeout waiting for deactivate", __FUNCTION__);
            }
        }
        {
            SyncEventGuard g2 (sRespCbEvent);
            ALOGD ("Switching RF Interface from NFC-DEP to FrameRF for T3T\n");
            if (NFA_STATUS_OK != (status = NFA_Select (1,NFA_PROTOCOL_T3T,NFA_INTERFACE_FRAME)))
            {
                ALOGE ("%s: NFA_Select failed, status = %d", __FUNCTION__, status);
            }
            if (sRespCbEvent.wait (2000) == false) //if timeout occured
            {
                 ALOGE ("%s: timeout waiting for select", __FUNCTION__);
            }
         }
    }
    ALOGD ("%s: exit", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}

/**********************************************************************************
 **
 ** Function:       SwitchP2PToT3TRead
 **
 ** Description:    Create a thread to change the RF interface by Deactivating to Sleep
 **
 ** Returns:         None .
 **
 **********************************************************************************/

static bool SwitchP2PToT3TRead()
{
    pthread_t T3TPollThreadId;
    int irret = -1;
    ALOGD ("%s:entry", __FUNCTION__);

    /* Transcation is done process the last request*/
    irret = pthread_create(&T3TPollThreadId, NULL, T3TPollThread, NULL);
    if(irret != 0)
    {
        ALOGD("Unable to create the thread");
    }
    ALOGD ("%s:exit", __FUNCTION__);
    return irret;
}

static void NxpResponsePropCmd_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{
    ALOGD("NxpResponsePropCmd_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)
#ifdef CHECK_FOR_NFCEE_CONFIGURATION
/**********************************************************************************
 **
 ** Function:        checkforNfceeBuffer
 **
 ** Description:    checking for the Nfcee Buffer (GetConfigs for SWP_INT_SESSION_ID (EA and EB))
 **
 ** Returns:         None .
 **
 **********************************************************************************/
void checkforNfceeBuffer()
{
int i, count = 0;

    for(i=4;i<12;i++)
    {
        if(sConfig[i] == 0xff)
            count++;
    }

    if(count >= 8)
        sNfceeConfigured = 1;
    else
        sNfceeConfigured = 0;

    memset (sConfig, 0, sizeof (sConfig));

}
/**********************************************************************************
 **
 ** Function:        checkforNfceeConfig
 **
 ** Description:    checking for the Nfcee is configured or not (GetConfigs for SWP_INT_SESSION_ID (EA and EB))
 **
 ** Returns:         None .
 **
 **********************************************************************************/
void checkforNfceeConfig()
{
    UINT8 i,uicc_flag = 0,ese_flag = 0;
    UINT8 num_nfcee_present = 0;
    tNFA_HANDLE nfcee_handle[MAX_NFCEE];
    tNFA_EE_STATUS nfcee_status[MAX_NFCEE];

    unsigned long timeout_buff_val=0,check_cnt=0,retry_cnt=0;

    tNFA_STATUS status;
    tNFA_PMID param_ids_UICC[] = {0xA0, 0xEA};
    tNFA_PMID param_ids_eSE[]  = {0xA0, 0xEB};

    sCheckNfceeFlag = 1;

    ALOGD ("%s: enter", __FUNCTION__);

    status = GetNxpNumValue(NAME_NXP_DEFAULT_NFCEE_TIMEOUT, (void*)&timeout_buff_val, sizeof(timeout_buff_val));

    if(status == TRUE)
    {
        check_cnt = timeout_buff_val*RETRY_COUNT;
    }
    else
    {
        check_cnt = default_count*RETRY_COUNT;
    }

    ALOGD ("NAME_DEFAULT_NFCEE_TIMEOUT = %d", check_cnt);

    num_nfcee_present = SecureElement::getInstance().mNfceeData_t.mNfceePresent;
    ALOGD("num_nfcee_present = %d",num_nfcee_present);


    for(i = 1; i<= num_nfcee_present ; i++)
    {
        nfcee_handle[i] = SecureElement::getInstance().mNfceeData_t.mNfceeHandle[i];
        nfcee_status[i] = SecureElement::getInstance().mNfceeData_t.mNfceeStatus[i];

        if(nfcee_handle[i] == ESE_HANDLE && nfcee_status[i] == 0)
        {
            ese_flag = 1;
            ALOGD("eSE_flag SET");
        }

        if(nfcee_handle[i] == UICC_HANDLE && nfcee_status[i] == 0)
        {
            uicc_flag = 1;
            ALOGD("UICC_flag SET");
        }
    }

    if(num_nfcee_present >= 1)
    {
        SyncEventGuard guard (android::sNfaGetConfigEvent);

        if(uicc_flag)
        {
            while(check_cnt > retry_cnt)
            {
                status = NFA_GetConfig(0x01,param_ids_UICC);
                if(status == NFA_STATUS_OK)
                {
                    android::sNfaGetConfigEvent.wait();
                }

                if(sNfceeConfigured == 1)
                {
                    ALOGD("UICC Not Configured");
                }
                else
                {
                    ALOGD("UICC Configured");
                    break;
                }

                usleep(100000);
                retry_cnt++;
            }

            if(check_cnt <= retry_cnt)
                ALOGD("UICC Not Configured");
            retry_cnt=0;
        }

        if(ese_flag)
        {
            while(check_cnt > retry_cnt)
            {
                status = NFA_GetConfig(0x01,param_ids_eSE);
                if(status == NFA_STATUS_OK)
                {
                    android::sNfaGetConfigEvent.wait();
                }

                if(sNfceeConfigured == 1)
                {
                    ALOGD("eSE Not Configured");
                }
                else
                {
                    ALOGD("eSE Configured");
                    break;
                }

                usleep(100000);
                retry_cnt++;
            }

            if(check_cnt <= retry_cnt)
                ALOGD("eSE Not Configured");
            retry_cnt=0;
        }

    }

sCheckNfceeFlag = 0;

}
#endif

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)

static void nfaNxpSelfTestNtfTimerCb (union sigval)
{
    ALOGD ("%s", __FUNCTION__);
    ALOGD("NXP SWP SelfTest : Can't get a notification about SWP Status!!");
    SyncEventGuard guard (sNfaNxpNtfEvent);
    sNfaNxpNtfEvent.notifyOne ();
    SetCbStatus(NFA_STATUS_FAILED);
}

static void nfaNxpSelfTestNtfCallback(UINT8 event, UINT16 param_len, UINT8 *p_param)
{
    ALOGD ("%s", __FUNCTION__);

    if(param_len == 0x05 && p_param[3] == 00) //p_param[4]  0x00:SWP Link OK 0x03:SWP link dead.
    {
        ALOGD("NXP SWP SelfTest : SWP Link OK ");
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        if(p_param[3] == 0x03) ALOGD("NXP SWP SelfTest : SWP Link dead ");
        SetCbStatus(NFA_STATUS_FAILED);
    }

    switch(p_param[4]){ //information of PMUVCC.
        case 0x00 : ALOGD("NXP SWP SelfTest : No PMUVCC ");break;
        case 0x01 : ALOGD("NXP SWP SelfTest : PMUVCC = 1.8V ");break;
        case 0x02 : ALOGD("NXP SWP SelfTest : PMUVCC = 3.3V ");break;
        case 0x03 : ALOGD("NXP SWP SelfTest : PMUVCC = undetermined ");break;
        default   : ALOGD("NXP SWP SelfTest : unknown PMUVCC ");break;
    }

    SyncEventGuard guard (sNfaNxpNtfEvent);
    sNfaNxpNtfEvent.notifyOne ();
}

static void nfcManager_doPrbsOn(JNIEnv* e, jobject o, jint tech, jint rate)
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                    /*commented to eliminate unused variable warning*/
    UINT8 param[4];

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return;
    }

    if (sDiscoveryEnabled) {
        ALOGD("Discovery must not be enabled for SelfTest");
        return ;
    }

    if(tech < 0 || tech > 2)
    {
        ALOGD("Invalid tech! please choose A or B or F");
        return;
    }

    if(rate < 0 || rate > 3){
        ALOGD("Invalid bitrate! please choose 106 or 212 or 424 or 848");
        return;
    }

    memset(param, 0x00, sizeof(param));
    //Technology to stream 0x00:TypeA 0x01:TypeB 0x02:TypeF
    //Bitrate                       0x00:106kbps 0x01:212kbps 0x02:424kbps 0x03:848kbps
    param[0] = tech;    //technology
    param[1] = rate;    //bitrate
    param[2] = 0x00;
    param[3] = 0x01;

    status = Nxp_SelfTest(1, param);

    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);

    return;
}

static void nfcManager_doPrbsOff(JNIEnv* e, jobject o)
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                    /*commented to eliminate unused variable warning*/
    UINT8 param;

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return;
    }

    if (sDiscoveryEnabled) {
        ALOGD("Discovery must not be enabled for SelfTest");
        return;
    }

    //Factory Test Code
    //step1. PRBS Test stop : VEN RESET
    status = Nxp_SelfTest(2, &param);   //VEN RESET
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step1. PRBS Test stop : VEN RESET Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step2. PRBS Test stop : CORE RESET_CMD
    status = Nxp_SelfTest(3, &param);   //CORE_RESET_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step2. PRBS Test stop : CORE RESET_CMD Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step3. PRBS Test stop : CORE_INIT_CMD
    status = Nxp_SelfTest(4, &param);   //CORE_INIT_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step3. PRBS Test stop : CORE_INIT_CMD Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step4. : NXP_ACT_PROP_EXTN
    status = Nxp_SelfTest(5, &param);   //NXP_ACT_PROP_EXTN
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step: NXP_ACT_PROP_EXTN Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    TheEnd:
    //Factory Test Code
    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);

    return;
}

static jint nfcManager_SWPSelfTest(JNIEnv* e, jobject o, jint ch)
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
    tNFA_STATUS regcb_stat = NFA_STATUS_FAILED;
    UINT8 param[1];

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return status;
    }

    if (sDiscoveryEnabled) {
        ALOGD("Discovery must not be enabled for SelfTest");
        return status;
    }

    if (ch < 0 || ch > 1){
        ALOGD("Invalid channel!! please choose 0 or 1");
        return status;
    }


    //step1.  : CORE RESET_CMD
    status = Nxp_SelfTest(3, param);   //CORE_RESET_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step2. PRBS Test stop : CORE RESET_CMD Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step2. : CORE_INIT_CMD
    status = Nxp_SelfTest(4, param);   //CORE_INIT_CMD
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step3. PRBS Test stop : CORE_INIT_CMD Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    //step3. : NXP_ACT_PROP_EXTN
    status = Nxp_SelfTest(5, param);   //NXP_ACT_PROP_EXTN
    if(NFA_STATUS_OK != status)
    {
        ALOGD("step: NXP_ACT_PROP_EXTN Fail!");
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    regcb_stat = NFA_RegVSCback (true,nfaNxpSelfTestNtfCallback); //Register CallBack for NXP NTF
    if(NFA_STATUS_OK != regcb_stat)
    {
        ALOGD("To Regist Ntf Callback is Fail!");
        goto TheEnd;
    }

    param[0] = ch; // SWP channel 0x00 : SWP1(UICC) 0x01:SWP2(eSE)
    status = Nxp_SelfTest(0, param);
    if(NFA_STATUS_OK != status)
    {
        status = NFA_STATUS_FAILED;
        goto TheEnd;
    }

    {
        ALOGD("NFC NXP SelfTest wait for Notificaiton");
        nfaNxpSelfTestNtfTimer.set(1000, nfaNxpSelfTestNtfTimerCb);
        SyncEventGuard guard (sNfaNxpNtfEvent);
        sNfaNxpNtfEvent.wait(); //wait for NXP Self NTF to come
    }

    status = GetCbStatus();
    if(NFA_STATUS_OK != status)
    {
        status = NFA_STATUS_FAILED;
    }

    TheEnd:
    if(NFA_STATUS_OK == regcb_stat) {
        regcb_stat = NFA_RegVSCback (false,nfaNxpSelfTestNtfCallback); //DeRegister CallBack for NXP NTF
    }
    nfaNxpSelfTestNtfTimer.kill();
    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);
    return status;
}
/**********************************************************************************
 **
 ** Function:        nfcManager_getFwVersion
 **
 ** Description:     To get the FW Version
 **
 ** Returns:         int fw version as below four byte format
 **                  [0x00  0xROM_CODE_V  0xFW_MAJOR_NO  0xFW_MINOR_NO]
 **
 **********************************************************************************/

static jint nfcManager_getFwVersion(JNIEnv* e, jobject o)
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                        /*commented to eliminate unused variable warning*/
    jint version = 0, temp = 0;
    tNFC_FW_VERSION nfc_native_fw_version;

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return status;
    }
    memset(&nfc_native_fw_version, 0, sizeof(nfc_native_fw_version));

    nfc_native_fw_version = nfc_ncif_getFWVersion();
    ALOGD ("FW Version: %x.%x.%x", nfc_native_fw_version.rom_code_version,
               nfc_native_fw_version.major_version,nfc_native_fw_version.minor_version);

    temp = nfc_native_fw_version.rom_code_version;
    version = temp << 16;
    temp = nfc_native_fw_version.major_version;
    version |= temp << 8;
    version |= nfc_native_fw_version.minor_version;

    ALOGD ("%s: exit; version =0x%X", __FUNCTION__,version);
    return version;
}

static void nfcManager_doSetEEPROM(JNIEnv* e, jobject o, jbyteArray val)
{
    ALOGD ("%s: enter", __FUNCTION__);
    tNFA_STATUS status = NFA_STATUS_FAILED;
//    bool stat = false;                        /*commented to eliminate unused variable warning*/
//    UINT8 param;                              /*commented to eliminate unused variable warning*/

    if (!sIsNfaEnabled) {
        ALOGD("NFC does not enabled!!");
        return;
    }

    ALOGD ("%s: exit; status =0x%X", __FUNCTION__,status);

    return;
}

#endif
#endif
}
/* namespace android */
