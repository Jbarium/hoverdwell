package com.bariumana.hoverdwell;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.WindowManager;

public class OverlayService extends Service {
    static final String SHOW = "com.bariumana.hoverdwell.SHOW", HIDE = "com.bariumana.hoverdwell.HIDE";
    WindowManager wm;
    MenuView view;
    long lastT = -1;

    final BroadcastReceiver rcv = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) {
            long t = i.getLongExtra("t", 0);
            if (t < lastT) return;            // drop out-of-order messages
            lastT = t;
            if (SHOW.equals(i.getAction())) show(i); else hide();
        }
    };

    @Override public void onCreate() {
        super.onCreate();
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        IntentFilter f = new IntentFilter();
        f.addAction(SHOW);
        f.addAction(HIDE);
        registerReceiver(rcv, f);
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            nm.createNotificationChannel(new NotificationChannel("hd", "HoverDwell", NotificationManager.IMPORTANCE_MIN));
            Notification n = new Notification.Builder(this, "hd")
                    .setSmallIcon(android.R.drawable.ic_menu_compass)
                    .setContentTitle("HoverDwell running").build();
            startForeground(1, n);
        }
    }

    void show(Intent i) {
        try {
            DisplayMetrics dm = new DisplayMetrics();
            wm.getDefaultDisplay().getRealMetrics(dm);
            float kx = dm.widthPixels / (float) i.getIntExtra("w", 1080);
            float ky = dm.heightPixels / (float) i.getIntExtra("h", 1920);
            String lab = i.getStringExtra("labels");
            String[] labels = lab == null ? new String[0] : lab.split("\\|");
            if (view == null) {
                view = new MenuView(this);
                int type = Build.VERSION.SDK_INT >= 26
                        ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                        : WindowManager.LayoutParams.TYPE_PHONE;
                WindowManager.LayoutParams lp = new WindowManager.LayoutParams(
                        WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT, type,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                                | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                        PixelFormat.TRANSLUCENT);
                wm.addView(view, lp);
            }
            view.set(i.getIntExtra("cx", 0) * kx, i.getIntExtra("cy", 0) * ky,
                    i.getIntExtra("r", 200) * kx, i.getIntExtra("ri", 80) * kx,
                    labels, i.getIntExtra("hl", -1), i.getIntExtra("sel", 900));
        } catch (Exception e) {
            Log.e("HoverDwell", "show failed (overlay permission?)", e);
            view = null;
        }
    }

    void hide() {
        if (view != null) {
            try { wm.removeView(view); } catch (Exception ignored) {}
            view = null;
        }
    }

    @Override public int onStartCommand(Intent i, int f, int id) { return START_STICKY; }
    @Override public void onDestroy() { unregisterReceiver(rcv); hide(); super.onDestroy(); }
    @Override public IBinder onBind(Intent i) { return null; }
}