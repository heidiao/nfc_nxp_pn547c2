/*
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
 */
package com.nxp.nfc;

import android.os.RemoteException;
import android.util.Log;

import java.io.IOException;

/**
 * This class provides the primary API for managing Applet load applet.
*/
public final class NfcAla {
    private static final String TAG = "NfcAla";
    private INfcAla mService;

    public NfcAla(INfcAla mAlaService) {
        mService = mAlaService;
    }

    /**
     * This API performs Applet load Applet operation it fetches the
     * secure script from the path choice and string pkg is used for SHA verification
     * of callers context's package name by ALA applet.
     * @param pkg Callers package name
     * @param choice Secure script path
     * @return int :- SUCCESS returns 0 otherwise non-zero.
     * <p>Requires {@link android.Manifest.permission#NFC} permission.
     * @throws IOException If a failure occurred during appletLoadApplet
     */
    public int appletLoadApplet(String pkg, String choice) throws IOException {
        try {
            int status = mService.appletLoadApplet(pkg, choice);
            // Handle potential errors
            if (status == 0x00) {
                return status;
            } else {
                throw new IOException("Unable to Load applet");
            }
         } catch (RemoteException e) {
               Log.e(TAG, "RemoteException in AppletLoadApplet(): ", e);
               throw new IOException("RemoteException in AppletLoadApplet()");
         }
    }

    /**
     * This API lists all the applets loaded through ALA module.
     * @param pkg Callers package name
     * @param name List of all applet
     * @return int :- SUCCESS returns 0 otherwise non-zero.
     * @throws IOException If a failure occurred during getListofApplets
     */
    public int getListofApplets(String pkg, String[] name) throws IOException {
        try {
            int num = mService.getListofApplets(pkg, name);
            // Handle potential error
            return num;

         } catch (RemoteException e) {
               Log.e(TAG, "RemoteException in GetListofApplets(): ", e);
               throw new IOException("RemoteException in GetListofApplets()");
         }
    }

    /**
     * This API returns the certificate key of the ALA applet present
     * @return byte[] :- Returns certificate key byte array.
     * @throws IOException If a failure occurred during getKeyCertificate
     */
    public byte[] getKeyCertificate() throws IOException {
        try{
            byte[] data = mService.getKeyCertificate();
            if((data != null) && (data.length != 0x00))
                return data;
            else
                throw new IOException("invalid data received");
        } catch (RemoteException e) {
              Log.e(TAG, "RemoteException in getKeyCertificate(): ", e);
              throw new IOException("RemoteException in getKeyCertificate()");
        }
    }
}
