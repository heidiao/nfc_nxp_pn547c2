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
#include <semaphore.h>
#include <errno.h>
#include "OverrideLog.h"
#include "NfcJniUtil.h"
#include "SyncEvent.h"
#include "JavaClassConstants.h"
#include "config.h"
#include "NfcAdaptation.h"

extern "C"
{
#include "nfa_api.h"
#include "nfa_rw_api.h"
}
/* Structure to store screen state */
typedef enum screen_state
{
    NFA_SCREEN_STATE_DEFAULT = 0x00,
    NFA_SCREEN_STATE_OFF,
    NFA_SCREEN_STATE_LOCKED,
    NFA_SCREEN_STATE_UNLOCKED
}eScreenState_t;
typedef struct nxp_feature_data
{
    SyncEvent    NxpFeatureConfigEvt;
    tNFA_STATUS  wstatus;
}Nxp_Feature_Data_t;

namespace android
{
static Nxp_Feature_Data_t gnxpfeature_conf;
SyncEvent    NxpSetVenConfigEvt;

}

void SetCbStatus(tNFA_STATUS status);
tNFA_STATUS GetCbStatus(void);
static void NxpResponse_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);
static void NxpResponse_SetDhlf_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);
void NxpResponse_SetVenConfig_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param);


namespace android
{
void SetCbStatus(tNFA_STATUS status)
{
    gnxpfeature_conf.wstatus = status;
}

tNFA_STATUS GetCbStatus(void)
{
    return gnxpfeature_conf.wstatus;
}

static void NxpResponse_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{

    ALOGD("NxpResponse_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);

    if(p_param[3] == 0x00)
    {
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        SetCbStatus(NFA_STATUS_FAILED);
    }

    SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
    gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne ();

}
static void NxpResponse_SetDhlf_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{

    ALOGD("NxpResponse_SetDhlf_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);

    if(p_param[3] == 0x00)
    {
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        SetCbStatus(NFA_STATUS_FAILED);
    }

    SyncEventGuard guard(gnxpfeature_conf.NxpFeatureConfigEvt);
    gnxpfeature_conf.NxpFeatureConfigEvt.notifyOne ();

}
void NxpResponse_SetVenConfig_Cb(UINT8 event, UINT16 param_len, UINT8 *p_param)
{
    ALOGD("NxpResponse_SetVenConfig_Cb Received length data = 0x%x status = 0x%x", param_len, p_param[3]);
    if(p_param[3] == 0x00)
    {
        SetCbStatus(NFA_STATUS_OK);
    }
    else
    {
        SetCbStatus(NFA_STATUS_FAILED);
    }
    SyncEventGuard guard(NxpSetVenConfigEvt);
    NxpSetVenConfigEvt.notifyOne ();
}

#if(NFC_NXP_NOT_OPEN_INCLUDED == TRUE)

/*******************************************************************************
 **
 ** Function:        EmvCo_dosetPoll
 **
 ** Description:     Enable/disable Emv Co polling
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS EmvCo_dosetPoll(jboolean enable)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t cmd_buf[] ={0x20, 0x02, 0x05, 0x01, 0xA0, 0x44, 0x01, 0x00};

    ALOGD("%s: enter", __FUNCTION__);

    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (gnxpfeature_conf.NxpFeatureConfigEvt);
    if(enable)
    {
        NFA_SetEmvCoState(TRUE);
        ALOGD("EMV-CO polling profile");
        cmd_buf[7] = 0x01; /*EMV-CO Poll*/
    }
    else
    {
        NFA_SetEmvCoState(FALSE);
        ALOGD("NFC forum polling profile");
    }
    status = NFA_SendNxpNciCommand(sizeof(cmd_buf), cmd_buf, NxpResponse_Cb);
    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
    } else {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }

    status = GetCbStatus();
    return status;
}

/*******************************************************************************
 **
 ** Function:        SetScreenState
 **
 ** Description:     set/clear SetDHListenFilter
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS SetScreenState(jint state)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t screen_off_state_cmd_buff[] = {0x2F, 0x15, 0x01, 0x01};

    ALOGD("%s: enter", __FUNCTION__);

    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (gnxpfeature_conf.NxpFeatureConfigEvt);
    if(state == NFA_SCREEN_STATE_OFF)
    {
        ALOGD("Set Screen OFF");
        screen_off_state_cmd_buff[3] = 0x01;
    }
    else if(state == NFA_SCREEN_STATE_LOCKED)
    {
        ALOGD("Screen ON-locked");
        screen_off_state_cmd_buff[3] = 0x02;
    }
    else if(state == NFA_SCREEN_STATE_UNLOCKED)
    {
        ALOGD("Screen ON-Unlocked");
        screen_off_state_cmd_buff[3] = 0x00;
    }
    else
    {
        ALOGD("Invalid screen state");
    }
    status = NFA_SendNxpNciCommand(sizeof(screen_off_state_cmd_buff), screen_off_state_cmd_buff, NxpResponse_SetDhlf_Cb);
    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
    } else {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }

    status = GetCbStatus();
    return status;
}


/*******************************************************************************
 **
 ** Function:        SetVenConfigValue
 **
 ** Description:     setting the Ven Config Value
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS SetVenConfigValue(jint venconfig)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t cmd_buf[] = {0x20, 0x02, 0x05, 0x01, 0xA0, 0x07, 0x01, 0x03};
    ALOGD("%s: enter", __FUNCTION__);
    if(venconfig == VEN_CFG_NFC_OFF_POWER_OFF)
    {
        ALOGD("Setting the VEN_CFG to 2, Disable ESE events");
        cmd_buf[7] = 0x02;
    }
    else if(venconfig == VEN_CFG_NFC_ON_POWER_ON)
    {
        ALOGD("Setting the VEN_CFG to 3, Make ");
        cmd_buf[7] = 0x03;
    }
    else
    {
        ALOGE("Wrong VEN_CFG Value");
        return status;
    }
    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (NxpSetVenConfigEvt);
    status = NFA_SendNxpNciCommand(sizeof(cmd_buf), cmd_buf, NxpResponse_SetVenConfig_Cb);
    if (status == NFA_STATUS_OK)
    {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        NxpSetVenConfigEvt.wait(); /* wait for callback */
    }
    else
    {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }
    status = GetCbStatus();
    return status;
}


//Factory Test Code --start
/*******************************************************************************
 **
 ** Function:        Nxp_SelfTest
 **
 ** Description:     SelfTest SWP, PRBS
 **
 ** Returns:         success/failure
 **
 *******************************************************************************/
tNFA_STATUS Nxp_SelfTest(uint8_t testcase, uint8_t* param)
{
    tNFA_STATUS status = NFA_STATUS_FAILED;
    uint8_t swp_test[] ={0x2F, 0x3E, 0x01, 0x00};   //SWP SelfTest
    uint8_t prbs_test[] ={0x2F, 0x30, 0x04, 0x00, 0x00, 0x00, 0x05};    //PRBS SelfTest
    //Factory Test Code for PRBS STOP --/
//    uint8_t prbs_stop[] ={0x2F, 0x30, 0x04, 0x53, 0x54, 0x4F, 0x50};  //STOP!!    /*commented to eliminate unused variable warning*/
    uint8_t rst_cmd[] ={0x20, 0x00, 0x01, 0x01};    //CORE_RESET_CMD
    uint8_t init_cmd[] ={0x20, 0x01, 0x00};         //CORE_INIT_CMD
    uint8_t prop_ext_act_cmd[] ={0x2F, 0x02, 0x00};         //CORE_INIT_CMD

    //Factory Test Code for PRBS STOP --/
    uint8_t cmd_buf[7] = {0,};
    uint8_t cmd_len = 0;

    ALOGD("%s: enter", __FUNCTION__);

    NfcAdaptation& theInstance = NfcAdaptation::GetInstance();
    tHAL_NFC_ENTRY* halFuncEntries = theInstance.GetHalEntryFuncs ();

    SetCbStatus(NFA_STATUS_FAILED);
    SyncEventGuard guard (gnxpfeature_conf.NxpFeatureConfigEvt);

    memset(cmd_buf, 0x00, sizeof(cmd_buf));

    switch(testcase){
    case 0 ://SWP Self-Test
        cmd_len = sizeof(swp_test);
        swp_test[3] = param[0];  //select channel 0x00:UICC(SWP1) 0x01:eSE(SWP2)
        memcpy(cmd_buf, swp_test, 4);
        break;

    case 1 ://PRBS Test start
        cmd_len = sizeof(prbs_test);
        //Technology to stream 0x00:TypeA 0x01:TypeB 0x02:TypeF
        //Bitrate                       0x00:106kbps 0x01:212kbps 0x02:424kbps 0x03:848kbps
        memcpy(&prbs_test[3], param, 4);
        memcpy(cmd_buf, prbs_test, 7);
        break;

        //Factory Test Code
    case 2 ://step1. PRBS Test stop : VEN RESET
        halFuncEntries->power_cycle();
        return NFCSTATUS_SUCCESS;
        break;

    case 3 ://step2. PRBS Test stop : CORE RESET
        cmd_len = sizeof(rst_cmd);
        memcpy(cmd_buf, rst_cmd, 4);
        break;

    case 4 ://step3. PRBS Test stop : CORE_INIT
        cmd_len = sizeof(init_cmd);
        memcpy(cmd_buf, init_cmd, 3);
        break;
        //Factory Test Code

    case 5 ://step5. : NXP_ACT_PROP_EXTN
        cmd_len = sizeof(prop_ext_act_cmd);
        memcpy(cmd_buf, prop_ext_act_cmd, 3);
        break;

    default :
        ALOGD("NXP_SelfTest Invalid Parameter!!");
        return status;
    }

    status = NFA_SendNxpNciCommand(cmd_len, cmd_buf, NxpResponse_SetDhlf_Cb);
    if (status == NFA_STATUS_OK) {
        ALOGD ("%s: Success NFA_SendNxpNciCommand", __FUNCTION__);
        gnxpfeature_conf.NxpFeatureConfigEvt.wait(); /* wait for callback */
    } else {
        ALOGE ("%s: Failed NFA_SendNxpNciCommand", __FUNCTION__);
    }

    status = GetCbStatus();
    return status;
}
//Factory Test Code --end
#endif

} /*namespace android*/
