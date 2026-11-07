package com.didissnap.didissnap

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.provider.Settings

/**
 * เปิดแอปอัตโนมัติ:
 *  - autoBoot   : มือถือเปิดเครื่องเสร็จ
 *  - autoPower  : เริ่มชาร์จไฟ (ที่ชาร์จในรถจ่ายไฟตอนบิดกุญแจ) — ต้องมี service เฝ้าไว้
 *  - autoBt     : มือถือต่อ Bluetooth กับอุปกรณ์ที่เลือก (เช่น เครื่องเสียงรถ)
 * ค่าตั้งอ่านจาก SharedPreferences ของ Flutter (คีย์ขึ้นต้นด้วย "flutter.")
 */
object AutoStart {
    private const val PREFS = "FlutterSharedPreferences"

    fun prefBool(ctx: Context, key: String): Boolean = try {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("flutter.$key", false)
    } catch (e: Exception) {
        false
    }

    fun prefString(ctx: Context, key: String): String = try {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("flutter.$key", "") ?: ""
    } catch (e: Exception) {
        ""
    }

    /** Android 10+ ต้องได้สิทธิ์ "แสดงทับแอปอื่น" ถึงจะเปิดหน้าจอแอปเองจากเบื้องหลังได้ */
    fun canLaunchFromBackground(ctx: Context): Boolean {
        if (Build.VERSION.SDK_INT < 29) return true
        return Settings.canDrawOverlays(ctx)
    }

    fun launch(ctx: Context, reason: String) {
        val i = Intent(ctx, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
            putExtra("autoStartReason", reason)
        }
        try {
            ctx.startActivity(i)
        } catch (e: Exception) {
            // ถูกระบบบล็อก (ยังไม่ได้ให้สิทธิ์แสดงทับแอปอื่น)
        }
    }

    /** เริ่ม/หยุด service เฝ้าการชาร์จ ตามค่าตั้ง */
    fun syncService(ctx: Context) {
        val i = Intent(ctx, PowerWatchService::class.java)
        if (prefBool(ctx, "autoPower")) {
            try {
                if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
            } catch (e: Exception) {
            }
        } else {
            try {
                ctx.stopService(i)
            } catch (e: Exception) {
            }
        }
    }
}

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        AutoStart.syncService(ctx)
        val a = intent.action ?: return
        if (a == Intent.ACTION_MY_PACKAGE_REPLACED) return
        if (AutoStart.prefBool(ctx, "autoBoot")) AutoStart.launch(ctx, "boot")
    }
}

class BtReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        if (intent.action != BluetoothDevice.ACTION_ACL_CONNECTED) return
        if (!AutoStart.prefBool(ctx, "autoBt")) return
        val want = AutoStart.prefString(ctx, "autoBtAddress")
        if (want.isEmpty()) return
        @Suppress("DEPRECATION")
        val dev: BluetoothDevice? = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
        val addr = try {
            dev?.address
        } catch (e: SecurityException) {
            null
        }
        if (addr != null && addr.equals(want, ignoreCase = true)) {
            AutoStart.launch(ctx, "bluetooth")
        }
    }
}

/** service เบา ๆ ที่รอสัญญาณ "เริ่มชาร์จ" (Android ไม่ส่งสัญญาณนี้ให้แอปที่ไม่ได้รันอยู่) */
class PowerWatchService : Service() {
    private var receiver: BroadcastReceiver? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        startInForeground()
        val r = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                if (intent.action == Intent.ACTION_POWER_CONNECTED &&
                    AutoStart.prefBool(ctx, "autoPower")
                ) {
                    AutoStart.launch(ctx, "power")
                }
            }
        }
        receiver = r
        registerReceiver(r, IntentFilter(Intent.ACTION_POWER_CONNECTED))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!AutoStart.prefBool(this, "autoPower")) {
            stopSelf()
            return START_NOT_STICKY
        }
        return START_STICKY
    }

    @Suppress("DEPRECATION")
    private fun startInForeground() {
        val chId = "autostart"
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(
                NotificationChannel(chId, "เปิดแอปอัตโนมัติ", NotificationManager.IMPORTANCE_MIN)
            )
        }
        val pi = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val b = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, chId) else Notification.Builder(this)
        val n = b.setContentTitle("Didissnap")
            .setContentText("รอไฟจากรถ (บิดกุญแจ) เพื่อเปิดหน้าปัด")
            .setSmallIcon(applicationInfo.icon)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(7, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(7, n)
        }
    }

    override fun onDestroy() {
        receiver?.let {
            try {
                unregisterReceiver(it)
            } catch (e: Exception) {
            }
        }
        super.onDestroy()
    }
}
