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

import android.app.Activity;
import android.app.ActivityThread;
import android.content.pm.IPackageManager;
import android.content.pm.PackageManager;
import java.util.HashMap;
import android.nfc.INfcAdapter;
import android.nfc.NfcAdapter;
import android.nfc.INfcAdapterExtras;
import android.os.Bundle;
import android.os.IBinder;
import android.os.ServiceManager;

import java.io.IOException;
import android.os.RemoteException;

import android.util.Log;

/**
 * Represents the local NXP NFC adapter.
 *
 * Use the helper {@link #getNxpNfcAdapter(NfcAdapter)} to get the default NXP NFC
 * adapter for this Android device.
 */
public final class NxpNfcAdapter {
    private static final String TAG = "NXPNFC";

    // Guarded by NfcAdapter.class
    static boolean sIsInitialized = false;

    /**
     * The NfcAdapter object for each application context.
     * There is a 1-1 relationship between application context and
     * NfcAdapter object.
     */
    static HashMap<NfcAdapter, NxpNfcAdapter> sNfcAdapters = new HashMap(); //guard by NfcAdapter.class

    // Final after first constructor, except for
    // attemptDeadServiceRecovery() when NFC crashes - we accept a best effort
    // recovery
    private static INfcAdapter sService;
    private static INxpNfcAdapter sNxpService;

    private NxpNfcAdapter() {
    }

    /**
     * Returns the NxpNfcAdapter for application context,
     * or throws if NFC is not available.
     */
    public static synchronized NxpNfcAdapter getNxpNfcAdapter(NfcAdapter adapter) {
        if (!sIsInitialized) {
            if (adapter == null) {
                Log.v(TAG, "could not find NFC support");
                throw new UnsupportedOperationException();
            }
            sService = getServiceInterface();
            if (sService == null) {
                Log.e(TAG, "could not retrieve NFC service");
                throw new UnsupportedOperationException();
            }
            sNxpService = getNxpNfcAdapterInterface();
             if (sNxpService == null) {
                Log.e(TAG, "could not retrieve NXP NFC service");
                throw new UnsupportedOperationException();
            }
            sIsInitialized = true;
        }
        NxpNfcAdapter nxpAdapter = sNfcAdapters.get(adapter);
        if (nxpAdapter == null) {
            nxpAdapter = new NxpNfcAdapter();
            sNfcAdapters.put(adapter, nxpAdapter);
        }
        return nxpAdapter;
    }

    /** get handle to NFC service interface */
    private static INfcAdapter getServiceInterface() {
        /* get a handle to NFC service */
        IBinder b = ServiceManager.getService("nfc");
        if (b == null) {
            return null;
        }
        return INfcAdapter.Stub.asInterface(b);
    }

    /**
     * @hide
     */
    private static INxpNfcAdapter getNxpNfcAdapterInterface() {
        if (sService == null) {
            throw new UnsupportedOperationException("You need a reference from NfcAdapter to use the "
                    + " NXP NFC APIs");
        }
        try {
            return sService.getNxpNfcAdapterInterface();
        } catch (RemoteException e) {
            return null;
        }
    }

    /**
     * NFC service dead - attempt best effort recovery
     * @hide
     */
    private void attemptDeadServiceRecovery() {
        Log.e(TAG, "NFC service dead - attempting to recover");
        INfcAdapter service = getServiceInterface();
        if (service == null) {
            Log.e(TAG, "could not retrieve NFC service during service recovery");
            // nothing more can be done now, sService is still stale, we'll hit
            // this recovery path again later
            return;
        }
        // assigning to sService is not thread-safe, but this is best-effort code
        // and on a well-behaved system should never happen
        sService = service;
        sNxpService = getNxpNfcAdapterInterface();
        return;
    }

    /**
     * Get the Available Secure Element List
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @throws IOException If a failure occurred during the getAvailableSecureElementList()
     */

    public String [] getAvailableSecureElementList(String pkg) throws IOException {
        int [] seList;
        String [] arr = null;
        try{
            Log.d(TAG, "getAvailableSecureElementList-Enter");
            seList = sNxpService.getSecureElementList(pkg);
            arr= new String[seList.length];

        if (seList!=null && seList.length != 0)
        {
            Log.v(TAG,"getAvailableSecureElementList-"+ seList);
            for(int i=0;i<seList.length;i++)
            {
                Log.v(TAG, "getAvailableSecure seList[i]" + seList[i]);
                if(seList[i] == NxpConstants.SMART_MX_ID_TYPE)
                {
                    arr[i] = NxpConstants.SMART_MX_ID;
                }
                else if(seList[i] == NxpConstants.UICC_ID_TYPE)
                {
                    arr[i]= NxpConstants.UICC_ID;
                }
                else if (seList[i] == NxpConstants.ALL_SE_ID_TYPE) {
                    arr[i]= NxpConstants.ALL_SE_ID;
                }
                else {
                    throw new IOException("No Secure Element selected");
                }
            }
        }
        return arr;
        }
        catch (RemoteException e) {
            Log.e(TAG, "getAvailableSecureElementList: failed", e);
            throw new IOException("Failure in deselecting the selected Secure Element");
        }
    }

    /**
     * Select the default Secure Element to be used in Card Emulation mode
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @param seId Secure Element ID to be used : {@link NxpConstants#SMART_MX_ID} or {@link NxpConstants#UICC_ID}
     * @throws IOException If a failure occurred during the Secure Element selection
     */
    public void selectDefaultSecureElement(String pkg, String seId) throws IOException {
        int [] seList;
        int seID = 0;
        boolean seSelected = false;

        if (seId.equals(NxpConstants.UICC_ID)) {
            seID = NxpConstants.UICC_ID_TYPE;
        } else if (seId.equals(NxpConstants.SMART_MX_ID)) {
            seID= NxpConstants.SMART_MX_ID_TYPE;
        } else if (seId.equals(NxpConstants.ALL_SE_ID)) {
            seID = NxpConstants.ALL_SE_ID_TYPE;
        } else {
            Log.e(TAG, "selectDefaultSecureElement: wrong Secure Element ID");
            throw new IOException("selectDefaultSecureElement failed: Wronf Secure Element ID");
        }

        /* Deselect already selected SE if ALL_SE_ID is not selected*/

        try {
            if(sNxpService.getSelectedSecureElement(pkg) != seID) {
                sNxpService.deselectSecureElement(pkg);
            }

        } catch (RemoteException e) {
            Log.e(TAG, "selectDefaultSecureElement: getSelectedSecureElement failed", e);
            throw new IOException("Failure in deselecting the selected Secure Element");
        }

        /* Get the list of the detected Secure Element */

        try {
            seList = sNxpService.getSecureElementList(pkg);
            // ADD
            if (seList != null && seList.length != 0) {

                if (seId.compareTo(NxpConstants.ALL_SE_ID) != 0) {
                    for (int i = 0; i < seList.length; i++) {
                        if (seList[i] == seID) {
                            /* Select the Secure Element */
                            sNxpService.selectSecureElement(pkg,seID);
                            seSelected = true;
                        }
                    }
                } else {
                    /* Select all Secure Element */
                    sNxpService.selectSecureElement(pkg, seID);
                    seSelected = true;
                }
            }

            // FIXME: This should be done in case of SE selection.
            if (!seSelected) {
                if (seId.equals(NxpConstants.UICC_ID)) {
                    sNxpService.storeSePreference(seID);
                    throw new IOException("UICC not detected");
                } else if (seId.equals(NxpConstants.SMART_MX_ID)) {
                    sNxpService.storeSePreference(seID);
                    throw new IOException("SMART_MX not detected");
                } else if (seId.equals(NxpConstants.ALL_SE_ID)) {
                    sNxpService.storeSePreference(seID);
                    throw new IOException("ALL_SE not detected");
                }
            }
        } catch (RemoteException e) {
            Log.e(TAG, "selectUiccCardEmulation: getSecureElementList failed", e);
        }
    }

   /**
    * Get the handle to an INfcAccessExtras Interface
    * @hide
    */
    public INfcAccessExtras getNfcAccessExtras(String pkg) {
        try {
            return sNxpService.getNfcAccessExtrasInterface(pkg);
        } catch (RemoteException e) {
            Log.e(TAG, "getNfcAccessExtras failed", e);
            return null;
        }
    }

    /**
     * Helper to create an Nfc Ala object.
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @return the NfcAla, or null if no NfcAla exists
     */
    public NfcAla createNfcAla() {
         try {
             return new NfcAla(sNxpService.getNfcAlaInterface());
         } catch (RemoteException e) {
             Log.e(TAG, "createNfcAla failed", e);
             return null;
         }
    }

    /**
     * Helper to create an Nfc Dta object.
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @return the NfcDta, or null if no NfcDta exists
     */
    public NfcDta createNfcDta() {
         try {
             return new NfcDta(sNxpService.getNfcDtaInterface());
         } catch (RemoteException e) {
             Log.e(TAG, "createNfcDta failed", e);
             return null;
         }
    }

    /**
     * Get the ID of the Secure Element selected
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @return Secure Element ID currently selected
     * @throws IOException If a failure occurred during the getDefaultSelectedSecureElement()
     */
    public String getDefaultSelectedSecureElement(String pkg) throws IOException {
        int seID = 0;

        /* Get Selected Secure Element */
        try {
            seID = sNxpService.getSelectedSecureElement(pkg);
            if (seID == NxpConstants.UICC_ID_TYPE/*0xABCDF0*/) {
                return NxpConstants.UICC_ID;
            } else if (seID == NxpConstants.SMART_MX_ID_TYPE/*0xABCDEF*/) {
                return NxpConstants.SMART_MX_ID;
            } else if (seID == NxpConstants.ALL_SE_ID_TYPE/*0xABCDFE*/) {
                return NxpConstants.ALL_SE_ID;
            } else {
                throw new IOException("No Secure Element selected");
            }
        } catch (RemoteException e) {
            Log.e(TAG, "getSelectedSecureElement failed", e);
            throw new IOException("getSelectedSecureElement failed");
        }
    }

   /**
     * deselect the selected Secure Element
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @throws IOException If a failure occurred during the deselction of secure element.
     */
    public void deSelectedSecureElement(String pkg) throws IOException {
        /* deselected Secure Element */
        try {
            sNxpService.deselectSecureElement(pkg);
        } catch (RemoteException e) {
            Log.e(TAG, "deselectSecureElement failed", e);
            throw new IOException("deselectSecureElement failed");
        }
    }

     /**
      *New protocol route based on ISO7816 is enabled for handling the AID's, not present in the AID route table (Default Route).
      *Mifare Desfire Route is handled by the ISO-DEP protocol route entry.
      *Mifare CLT Route is handled by Tech-A based routing.
      *Felica CLT Route is handled by the Tech F based routing.
      *There are 3 new frame work API's implemented for configuring Default AID route, Mifare Desfire Route and Mifare CLT and default values are handled using the configuration file.
      *During first boot preferred settings will not be created until frame work API's are used and settings will be taken directly from configuration file as given below.
      *@param routeLoc  com.nxp.uicc.ID/com.nxp.smart_mx.ID/com.nxp.host.ID
      *@param fullPower true/false
      *@param lowPower true/false
      *@param noPower true/false
      *If all the power values are set to false, that particular route entry will be removed.
      * In case of Mifare CLT    , tech A and Tech B entry will be removed.
      *In case of Mifare Desfire ISO-DEP entry will be removed
      *In case of Default Route ISO7816 protocol entry will be removed.
      * When mifare CLT route is configured both technology A and B is configured.
      *Technology route for type F is set to UICC with all the power modes on if type F supported UICC is inserted (this is not a   configurable item.
      *NAME_NXP_DEFAULT_SE entry in the configuration file is used to give preference to any of the secure elements when multiple SE's are available.
      *If there is only one secure element in the system, the   available secure element will be the preferred secure element.
      *Handle of this preferred secure element is used for configuring the NFA_CeConfigureUiccListenTech.
      *Through configuration file default route locations for Default Aid /Mifare Desfire/Mifare CLT can be configured.
      *There is no provision for configuring the power state through configuration file
      *Configure the power state for  default route values (Mifare Desfire/Default Aid route/Mifare CLT)through functions GetDefaultMifareDesfireRouteEntry
      * GetDefaultMifareDesfireRouteEntry/GetDefaultRouteEntry/GetDefaultMifateCLTRouteEntry
      * Set listen mode routing table configuration for Mifare Desfire Route.
      * routeLoc is parameter which fetch the text from UI and compare
      * <p>Requires {@link android.Manifest.permission#NFC} permission.
      *
      * @throws IOException If a failure occurred during Mifare Desfire Route set.
      */
     public void MifareDesfireRouteSet(String routeLoc, boolean fullPower, boolean lowPower, boolean noPower)
             throws IOException {
         try{
             int seID=0;
             boolean result = false;
             if (routeLoc.equals(NxpConstants.UICC_ID)) {

             seID = NxpConstants.UICC_ID_TYPE;
             } else if (routeLoc.equals(NxpConstants.SMART_MX_ID)) {

             seID= NxpConstants.SMART_MX_ID_TYPE;

             } else if (routeLoc.equals(NxpConstants.HOST_ID)) {
                 seID = NxpConstants.HOST_ID_TYPE;
             } else {
                 Log.e(TAG, "confMifareDesfireProtoRoute: wrong default route ID");
                 throw new IOException("confMifareProtoRoute failed: Wrong default route ID");
             }
             Log.i(TAG, "calling Services");
             sNxpService.MifareDesfireRouteSet(seID, fullPower, lowPower, noPower);
             } catch (RemoteException e) {
             Log.e(TAG, "confMifareDesfireProtoRoute failed", e);
             throw new IOException("confMifareDesfireProtoRoute failed");
             }
     }

     /**
      * Set listen mode routing table configuration for Default Route.
      *  routeLoc is parameter which fetch the text from UI and compare
      * * <p>Requires {@link android.Manifest.permission#NFC} permission.
      * @throws IOException If a failure occurred during Default Route Route set.
      */
    public void DefaultRouteSet(String routeLoc, boolean fullPower, boolean lowPower, boolean noPower)
            throws IOException {
        try {
            int seID=0;
            boolean result = false;
            if (routeLoc.equals(NxpConstants.UICC_ID)) {
            seID = NxpConstants.UICC_ID_TYPE;
            } else if (routeLoc.equals(NxpConstants.SMART_MX_ID)) {
            seID= NxpConstants.SMART_MX_ID_TYPE;
            } else if (routeLoc.equals(NxpConstants.HOST_ID)) {
              seID = NxpConstants.HOST_ID_TYPE;
            } else {
                Log.e(TAG, "DefaultRouteSet: wrong default route ID");
                throw new IOException("DefaultRouteSet failed: Wrong default route ID");
            }
               sNxpService.DefaultRouteSet(seID, fullPower, lowPower, noPower);
            } catch (RemoteException e) {
            Log.e(TAG, "confsetDefaultRoute failed", e);
            throw new IOException("confsetDefaultRoute failed");
        }
    }

    /**
     * Set listen mode routing table configuration for MifareCLTRouteSet.
     * routeLoc is parameter which fetch the text from UI and compare
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     *
     * @throws IOException If a failure occurred during Mifare CLT Route set.
     */
    public void MifareCLTRouteSet(String routeLoc, boolean fullPower, boolean lowPower, boolean noPower )
            throws IOException {
        try {
            int seID=0;
            boolean result = false;
            if (routeLoc.equals(NxpConstants.UICC_ID)) {
            seID = NxpConstants.UICC_ID_TYPE;
            } else if (routeLoc.equals(NxpConstants.SMART_MX_ID)) {
            seID= NxpConstants.SMART_MX_ID_TYPE;
            } else if (routeLoc.equals(NxpConstants.HOST_ID)) {
            seID = NxpConstants.HOST_ID_TYPE;
            } else {
                Log.e(TAG, "confMifareCLT: wrong default route ID");
                throw new IOException("confMifareCLT failed: Wrong default route ID");
            }
            sNxpService.MifareCLTRouteSet(seID, fullPower, lowPower, noPower);
        } catch (RemoteException e) {
            Log.e(TAG, "confMifareCLT failed", e);
            throw new IOException("confMifareCLT failed");
        }
    }

    /**
     * @hide
     */
    public INxpNfcAdapterExtras getNxpNfcAdapterExtrasInterface(INfcAdapterExtras extras) {
        if (sNxpService == null || extras == null) {
            throw new UnsupportedOperationException("You need a context on NxpNfcAdapter to use the "
                    + " NXP NFC extras APIs");
        }
        try {
            return sNxpService.getNxpNfcAdapterExtrasInterface();
        } catch (RemoteException e) {
        Log.e(TAG, "getNxpNfcAdapterExtrasInterface failed", e);
            return null;
        }
    }
}
