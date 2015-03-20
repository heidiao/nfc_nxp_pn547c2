/*
 * Copyright (C) 2012-2013 NXP Semiconductors
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
#include "DwpChannel.h"
#include "SecureElement.h"
#include <utils/Log.h>

bool IsWiredMode_Enable();

/*******************************************************************************
**
** Function:        IsWiredMode_Enable
**
** Description:     Provides the connection status of EE
**
** Returns:         True if ok.
**
*******************************************************************************/
bool IsWiredMode_Enable()
{
    static const char fn [] = "DwpChannel::IsWiredMode_Enable";
    ALOGD ("%s: enter", fn);
    SecureElement &se = SecureElement::getInstance();
    tNFA_STATUS stat = NFA_STATUS_FAILED;

    static const int MAX_NUM_EE = 5;
    UINT16 meSE =0x4C0;
    UINT8 mActualNumEe;
    tNFA_EE_INFO EeInfo[MAX_NUM_EE];
    mActualNumEe = MAX_NUM_EE;

#if 0
    if(mIsInit == false)
    {
        ALOGD ("%s: JcopOs Dwnld is not initialized", fn);
        goto TheEnd;
    }
#endif
    stat = NFA_EeGetInfo(&mActualNumEe, EeInfo);
    if(stat == NFA_STATUS_OK)
    {
        for(int xx = 0; xx <  mActualNumEe; xx++)
        {
            ALOGE("xx=%d, ee_handle=0x0%x, status=0x0%x", xx, EeInfo[xx].ee_handle,EeInfo[xx].ee_status);
            if (EeInfo[xx].ee_handle == meSE)
            {
                if(EeInfo[xx].ee_status == 0x00)
                {
                    stat = NFA_STATUS_OK;
                    ALOGD ("%s: status = 0x%x", fn, stat);
                    break;
                }
                else if(EeInfo[xx].ee_status == 0x01)
                {
                    ALOGE("%s: Enable eSE-mode set ON", fn);
                    se.SecEle_Modeset(0x01);
                    usleep(2000 * 1000);
                    stat = NFA_STATUS_OK;
                    break;
                }
                else
                {
                    stat = NFA_STATUS_FAILED;
                    break;
                }
            }
            else
            {
                stat = NFA_STATUS_FAILED;
            }

        }
    }
//TheEnd: /*commented to eliminate the label defined but not used warning*/
    ALOGD("%s: exit; status = 0x%X", fn, stat);
    if(stat == NFA_STATUS_OK)
        return true;
    else
        return false;
}

/*******************************************************************************
**
** Function:        open
**
** Description:     Opens the DWP channel to eSE
**
** Returns:         True if ok.
**
*******************************************************************************/

bool open()
{
    bool stat = true;
    SecureElement &se = SecureElement::getInstance();

    ALOGE("DwpChannel: Sec Element open Enter");

    stat = IsWiredMode_Enable();
    if(stat != true)
    {
        ALOGE("DwpChannel: Wired mode is not connected");
        return stat;
    }
    /*if controller is not routing AND there is no pipe connected,
      then turn on the sec elem*/
    if (! se.isBusy())
        stat = se.activate(0x01);

    if (stat)
    {
        //establish a pipe to sec elem
        stat = se.connectEE();
        if (!stat)
            se.deactivate (0);
    }

    return stat;
}
/*******************************************************************************
**
** Function:        close
**
** Description:     closes the DWP connection with eSE
**
** Returns:         True if ok.
**
*******************************************************************************/

bool close()
{
    bool stat = false;
    SecureElement &se = SecureElement::getInstance();

    stat = se.sendEvent(SecureElement::EVT_END_OF_APDU_TRANSFER);

    if(stat == false)
        goto TheEnd;

    stat = se.disconnectEE (0x01);

    //if controller is not routing AND there is no pipe connected,
    //then turn off the sec elem
    if (! se.isBusy())
        se.deactivate (0x01);

TheEnd:
    ALOGD("%s: exit", __FUNCTION__);
    return stat;
}

bool transceive (UINT8* xmitBuffer, INT32 xmitBufferSize, UINT8* recvBuffer,
                 INT32 recvBufferMaxSize, INT32& recvBufferActualSize, INT32 timeoutMillisec)
{
    static const char fn [] = "DwpChannel::transceive";
    bool stat = false;
    SecureElement &se = SecureElement::getInstance();
    ALOGD("%s: enter", fn);

    stat = se.transceive (xmitBuffer,
                          xmitBufferSize,
                          recvBuffer,
                          recvBufferMaxSize,
                          recvBufferActualSize,
                          timeoutMillisec);
    ALOGD("%s: exit", fn);
    return stat;
}

void doeSE_Reset(void)
{
    static const char fn [] = "DwpChannel::doeSE_Reset";
    SecureElement &se = SecureElement::getInstance();
    ALOGD("%s: enter:", fn);

    ALOGD("1st mode set calling");
    se.SecEle_Modeset(0x00);
    usleep(100 * 1000);
    ALOGD("1st mode set called");
    ALOGD("2nd mode set calling");

    se.SecEle_Modeset(0x01);
    ALOGD("2nd mode set called");

    usleep(2000 * 1000);
}
