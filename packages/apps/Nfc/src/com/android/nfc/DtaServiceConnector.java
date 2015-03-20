/**
 *                    Copyright (c), NXP Semiconductors
 *
 *                       (C)NXP Semiconductors B.V.2014
 *         All rights are reserved. Reproduction in whole or in part is
 *        prohibited without the written consent of the copyright owner.
 *    NXP reserves the right to make changes without notice at any time.
 *   NXP makes no warranty, expressed, implied or statutory, including but
 *   not limited to any implied warranty of merchantability or fitness for any
 *  particular purpose, or that the use will not infringe any third party patent,
 *   copyright or trademark. NXP must not be liable for any loss or damage
 *                            arising from its use.
 *
 */

package com.android.nfc;

import java.util.List;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;

public class DtaServiceConnector {

    Context mContext;
    Messenger dtaMessenger = null;
    boolean isBound;

    public DtaServiceConnector(Context mContext) {
        this.mContext = mContext;
    }

    public void bindService() {
        if(!isBound){
        Intent intent = new Intent("com.phdtaui.messageservice.ACTION_BIND");
        mContext.bindService(createExplicitFromImplicitIntent(mContext,intent), myConnection,Context.BIND_AUTO_CREATE);
        }
    }

    private ServiceConnection myConnection = new ServiceConnection() {
        public void onServiceConnected(ComponentName className, IBinder service) {
            dtaMessenger = new Messenger(service);
            isBound = true;
        }

        public void onServiceDisconnected(ComponentName className) {
            dtaMessenger = null;
            isBound = false;
        }
    };

    public void sendMessage(String ndefMessage) {
        if(!isBound)return;
        Message msg = Message.obtain();
        Bundle bundle = new Bundle();
        bundle.putString("NDEF_MESSAGE", ndefMessage);
        msg.setData(bundle);
        try {
            dtaMessenger.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }catch (NullPointerException e) {
            e.printStackTrace();
            }
    }
    public static Intent createExplicitFromImplicitIntent(Context context, Intent implicitIntent){
        PackageManager pm = context.getPackageManager();
        List<ResolveInfo> resolveInfo = pm.queryIntentServices(implicitIntent, 0);
        if (resolveInfo == null || resolveInfo.size() != 1) {
        return null;
        }
        ResolveInfo serviceInfo = resolveInfo.get(0);
        String packageName = serviceInfo.serviceInfo.packageName;
        String className = serviceInfo.serviceInfo.name;
        ComponentName component = new ComponentName(packageName, className);
        Intent explicitIntent = new Intent(implicitIntent);
        explicitIntent.setComponent(component);
        return explicitIntent;
    }
}
