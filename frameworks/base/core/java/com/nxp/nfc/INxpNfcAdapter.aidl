/*
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
*/
package com.nxp.nfc;

import com.nxp.nfc.INfcAccessExtras;
import com.nxp.nfc.INfcAla;
import com.nxp.nfc.INfcDta;
import com.nxp.nfc.INxpNfcAdapterExtras;
import com.nxp.nfc.INfcVzw;

/**
 * @hide
 */
interface INxpNfcAdapter
{
    INfcAccessExtras getNfcAccessExtrasInterface(in String pkg);
    INfcAla getNfcAlaInterface();
    INfcDta getNfcDtaInterface();
    INfcVzw getNfcVzwInterface();
    INxpNfcAdapterExtras getNxpNfcAdapterExtrasInterface();

    int[] getSecureElementList(String pkg);
    int getSelectedSecureElement(String pkg);
    int selectSecureElement(String pkg,int seId);
    int deselectSecureElement(String pkg);
    void storeSePreference(int seId);
    int setEmvCoPollProfile(boolean enable, int route);
    void MifareDesfireRouteSet(int routeLoc, boolean fullPower, boolean lowPower, boolean noPower);
    void DefaultRouteSet(int routeLoc, boolean fullPower, boolean lowPower, boolean noPower);
    void MifareCLTRouteSet(int routeLoc, boolean fullPower, boolean lowPower, boolean noPower);
}
