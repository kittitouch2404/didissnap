package com.didissnap.didissnap

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.FlutterInjector
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.Executors

/**
 * ส่วน native ของแอป:
 *  - Bluetooth Classic (SPP) สำหรับ ELM327 / Vgate iCar Pro
 *  - ผูกการเชื่อมต่อเข้ากับ WiFi (สำหรับ ESP32 / ELM327 WiFi ที่ไม่มีเน็ต)
 */
class MainActivity : FlutterActivity() {
    private val sppUuid: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    private val mainHandler = Handler(Looper.getMainLooper())
    private val io = Executors.newSingleThreadExecutor()
    private val audioIo = Executors.newSingleThreadExecutor()   // เสียงเตือน: แยกเธรด ไม่ให้ไปหน่วง Bluetooth

    @Volatile private var socket: BluetoothSocket? = null
    @Volatile private var out: OutputStream? = null
    @Volatile private var generation = 0
    private var sink: EventChannel.EventSink? = null
    private var pendingPermission: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        val messenger = flutterEngine.dartExecutor.binaryMessenger

        val ch = MethodChannel(messenger, "didissnap/native")
        chan = ch
        AutoStart.onWake = { r -> mainHandler.post { chan?.invokeMethod("carWake", r) } }
        ch.setMethodCallHandler { call, result ->
            when (call.method) {
                "launchReason" -> result.success(intent?.getStringExtra("autoStartReason"))
                "openUrl" -> {
                    try {
                        startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(call.argument<String>("url") ?: "")).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                        result.success(true)
                    } catch (e: Exception) {
                        result.success(false)
                    }
                }
                "gpsPermission" -> {
                    if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
                        result.success(true)
                    } else {
                        requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION), REQ_GPS)
                        result.success(false)
                    }
                }
                "pickFile" -> {
                    pendingFile?.success(null)
                    pendingFile = result
                    try {
                        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = call.argument<String>("mime") ?: "*/*"
                        }, REQ_FILE)
                    } catch (e: Exception) {
                        pendingFile = null
                        result.success(null)
                    }
                }
                "tone" -> {
                    // เสียงเตือน: ToneGenerator ออกลำโพงเพลง (ลำโพงรถ) ไม่มีดีเลย์
                    val kind = call.argument<Int>("kind") ?: 0
                    val ms = call.argument<Int>("ms") ?: 200
                    val vol = (call.argument<Int>("vol") ?: 80).coerceIn(0, 100)
                    audioIo.execute {
                        try {
                            val tg = android.media.ToneGenerator(android.media.AudioManager.STREAM_MUSIC, vol)
                            val t = when (kind) {
                                1 -> android.media.ToneGenerator.TONE_CDMA_HIGH_L        // เตือนแรง (น้ำร้อน)
                                2 -> android.media.ToneGenerator.TONE_PROP_BEEP2         // shift
                                3 -> android.media.ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD
                                else -> android.media.ToneGenerator.TONE_PROP_BEEP
                            }
                            tg.startTone(t, ms)
                            Thread.sleep((ms + 80).toLong())
                            tg.release()
                        } catch (e: Exception) {
                        }
                    }
                    result.success(true)
                }
                "alertSound" -> {
                    playAlert(call.argument<Int>("id") ?: 0, (call.argument<Int>("vol") ?: 80).coerceIn(0, 100), call.argument<String>("text") ?: "")
                    result.success(true)
                }
                "brightness" -> {
                    // ความสว่างของหน้าจอแอป (0..1, ติดลบ = ตามระบบ)
                    val v = call.argument<Double>("v") ?: -1.0
                    val lp = window.attributes
                    lp.screenBrightness = if (v < 0) android.view.WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE else v.toFloat().coerceIn(0.01f, 1f)
                    window.attributes = lp
                    result.success(true)
                }
                "saveToDownloads" -> {
                    val src = call.argument<String>("path") ?: ""
                    val name = call.argument<String>("name") ?: java.io.File(src).name
                    val mime = call.argument<String>("mime") ?: "text/plain"
                    io.execute {
                        val ok = saveToDownloads(java.io.File(src), name, mime)
                        mainHandler.post { result.success(ok) }
                    }
                }
                "pickText" -> {
                    pendingText?.success(null)
                    pendingText = result
                    try {
                        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "*/*"
                        }, REQ_TEXT)
                    } catch (e: Exception) {
                        pendingText = null
                        result.success(null)
                    }
                }
                "pickAudio" -> {
                    pendingAudio?.success(null)
                    pendingAudio = result
                    try {
                        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "audio/*"
                        }, REQ_AUDIO)
                    } catch (e: Exception) {
                        pendingAudio = null
                        result.success(null)
                    }
                }
                "ensurePermission" -> ensurePermission(result)
                "bonded" -> result.success(bondedDevices())
                "connect" -> {
                    val address = call.argument<String>("address")
                    if (address.isNullOrEmpty()) {
                        result.error("ARG", "missing address", null)
                    } else {
                        io.execute {
                            val err = connect(address)
                            val gen = generation
                            mainHandler.post {
                                if (err == null) result.success(gen)
                                else result.error("CONNECT", err, null)
                            }
                        }
                    }
                }
                "write" -> {
                    val data = call.argument<String>("data") ?: ""
                    io.execute {
                        try {
                            val o = out ?: throw IllegalStateException("not connected")
                            o.write(data.toByteArray(Charsets.ISO_8859_1))
                            o.flush()
                            mainHandler.post { result.success(true) }
                        } catch (e: Exception) {
                            mainHandler.post { result.error("WRITE", e.message ?: e.toString(), null) }
                        }
                    }
                }
                "disconnect" -> {
                    closeSocket()
                    result.success(true)
                }
                "bindWifi" -> result.success(bindWifi(call.argument<Boolean>("on") ?: true))
                "canOverlay" -> result.success(AutoStart.canLaunchFromBackground(this))
                "requestOverlay" -> {
                    if (Build.VERSION.SDK_INT >= 23) {
                        try {
                            startActivity(
                                Intent(
                                    Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                                    Uri.parse("package:$packageName")
                                )
                            )
                        } catch (e: Exception) {
                        }
                    }
                    result.success(true)
                }
                "batterySettings" -> {
                    try {
                        startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
                    } catch (e: Exception) {
                        try {
                            startActivity(
                                Intent(
                                    Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                                    Uri.parse("package:$packageName")
                                )
                            )
                        } catch (e2: Exception) {
                        }
                    }
                    result.success(true)
                }
                "requestNotif" -> {
                    if (Build.VERSION.SDK_INT >= 33 &&
                        checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
                        PackageManager.PERMISSION_GRANTED
                    ) {
                        requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQ_NOTIF)
                    }
                    result.success(true)
                }
                "syncAutoStart" -> {
                    AutoStart.syncService(this)
                    applyLockScreen()
                    result.success(true)
                }
                else -> result.notImplemented()
            }
        }

        // GPS: ความเร็ว (m/s) + ความแม่นยำ ส่งให้ Flutter ทุก ~1 วินาที
        EventChannel(messenger, "didissnap/gps").setStreamHandler(object : EventChannel.StreamHandler {
            override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                gpsSink = events
                startGps()
            }

            override fun onCancel(arguments: Any?) {
                stopGps()
                gpsSink = null
            }
        })

        EventChannel(messenger, "didissnap/bt/stream").setStreamHandler(object : EventChannel.StreamHandler {
            override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                sink = events
            }

            override fun onCancel(arguments: Any?) {
                sink = null
            }
        })
    }

    private var chan: MethodChannel? = null
    private var pendingAudio: MethodChannel.Result? = null
    private var pendingText: MethodChannel.Result? = null
    private var pendingFile: MethodChannel.Result? = null

    // ---------------- เสียงเตือน ----------------
    private var player: android.media.MediaPlayer? = null

    /** เล่นเสียงจาก assets ของ Flutter (ก๊อปไว้ใน cache ครั้งแรก) */
    private fun assetFile(name: String): java.io.File? = try {
        val f = java.io.File(cacheDir, name.substringAfterLast('/'))
        if (!f.exists()) {
            val key = FlutterInjector.instance().flutterLoader().getLookupKeyForAsset(name)
            assets.open(key).use { inp -> f.outputStream().use { inp.copyTo(it) } }
        }
        f
    } catch (e: Exception) { null }

    /**
     * id: 0 บี๊บสั้น, 1 บี๊บคู่, 2 ไซเรนแหลม, 3 เตือนภัยยาว, 4 ติ๊ดเร็ว, 5 เสียงผิดพลาด, 6 ติ๊ง-ต่อง,
     *     7 ระฆัง, 8 ไซเรนรถแข่ง, 9 8-bit alarm, 11 ปิด
     */
    private fun playAlert(id: Int, vol: Int, text: String) {
        when (id) {
            in 0..6 -> audioIo.execute {
                try {
                    val tg = android.media.ToneGenerator(android.media.AudioManager.STREAM_MUSIC, vol)
                    fun tone(t: Int, ms: Int, gap: Int = 60) { tg.startTone(t, ms); Thread.sleep((ms + gap).toLong()) }
                    when (id) {
                        0 -> tone(android.media.ToneGenerator.TONE_PROP_BEEP, 180)
                        1 -> tone(android.media.ToneGenerator.TONE_PROP_BEEP2, 300)
                        2 -> tone(android.media.ToneGenerator.TONE_CDMA_HIGH_L, 700)
                        3 -> tone(android.media.ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD, 900)
                        4 -> repeat(5) { tone(android.media.ToneGenerator.TONE_CDMA_PIP, 70, 50) }
                        5 -> tone(android.media.ToneGenerator.TONE_SUP_ERROR, 700)
                        6 -> tone(android.media.ToneGenerator.TONE_CDMA_CONFIRM, 500)
                    }
                    tg.release()
                } catch (e: Exception) {
                }
            }
            7, 8, 9 -> {
                val f = assetFile(when (id) { 7 -> "assets/alert_bell.m4a"; 8 -> "assets/alert_siren.m4a"; else -> "assets/alert_8bit.m4a" }) ?: return
                try {
                    player?.release()
                    val mp = android.media.MediaPlayer()
                    mp.setAudioStreamType(android.media.AudioManager.STREAM_MUSIC)
                    mp.setDataSource(f.absolutePath)
                    mp.setVolume(vol / 100f, vol / 100f)
                    mp.setOnCompletionListener { it.release(); if (player === it) player = null }
                    mp.prepare()
                    mp.start()
                    player = mp
                } catch (e: Exception) {
                }
            }
        }
    }
    private var gpsSink: EventChannel.EventSink? = null
    private var gpsListener: android.location.LocationListener? = null

    @SuppressLint("MissingPermission")
    private fun startGps() {
        if (gpsListener != null) return
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            gpsSink?.success(mapOf("err" to "perm"))
            return
        }
        val lm = getSystemService(Context.LOCATION_SERVICE) as android.location.LocationManager
        val l = android.location.LocationListener { loc ->
            val acc = if (Build.VERSION.SDK_INT >= 26 && loc.hasSpeedAccuracy()) loc.speedAccuracyMetersPerSecond.toDouble() else -1.0
            gpsSink?.success(mapOf(
                "spd" to (if (loc.hasSpeed()) loc.speed.toDouble() else -1.0),
                "sacc" to acc,
                "hacc" to (if (loc.hasAccuracy()) loc.accuracy.toDouble() else -1.0),
                "t" to loc.time
            ))
        }
        try {
            lm.requestLocationUpdates(android.location.LocationManager.GPS_PROVIDER, 1000L, 0f, l, Looper.getMainLooper())
            gpsListener = l
        } catch (e: Exception) {
            gpsSink?.success(mapOf("err" to (e.message ?: "gps")))
        }
    }

    private fun stopGps() {
        val l = gpsListener ?: return
        gpsListener = null
        try {
            (getSystemService(Context.LOCATION_SERVICE) as android.location.LocationManager).removeUpdates(l)
        } catch (e: Exception) {
        }
    }

    /** ก๊อปไฟล์ไปไว้ในโฟลเดอร์ Download ของเครื่อง (เปิดด้วยแอปไฟล์ได้) */
    private fun saveToDownloads(src: java.io.File, name: String, mime: String): Boolean = try {
        if (Build.VERSION.SDK_INT >= 29) {
            val v = android.content.ContentValues().apply {
                put(android.provider.MediaStore.Downloads.DISPLAY_NAME, name)
                put(android.provider.MediaStore.Downloads.MIME_TYPE, mime)
                put(android.provider.MediaStore.Downloads.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/Didissnap")
            }
            val uri = contentResolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, v)!!
            contentResolver.openOutputStream(uri)!!.use { out -> src.inputStream().use { it.copyTo(out) } }
            true
        } else {
            @Suppress("DEPRECATION")
            val dir = java.io.File(android.os.Environment.getExternalStoragePublicDirectory(android.os.Environment.DIRECTORY_DOWNLOADS), "Didissnap")
            dir.mkdirs()
            src.copyTo(java.io.File(dir, name), overwrite = true)
            true
        }
    } catch (e: Exception) {
        false
    }

    /** เลือกไฟล์เสียงเอง: ก๊อปไว้ในโฟลเดอร์แอป (ไฟล์ต้นฉบับย้าย/ลบได้) */
    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_FILE) {
            val r = pendingFile ?: return
            pendingFile = null
            val u = data?.data
            if (resultCode != RESULT_OK || u == null) { r.success(null); return }
            io.execute {
                val p = try {
                    val f = java.io.File(cacheDir, "picked_${System.currentTimeMillis()}")
                    contentResolver.openInputStream(u)!!.use { inp -> f.outputStream().use { inp.copyTo(it) } }
                    f.absolutePath
                } catch (e: Exception) { null }
                mainHandler.post { r.success(p) }
            }
            return
        }
        if (requestCode == REQ_TEXT) {
            val r = pendingText ?: return
            pendingText = null
            val u = data?.data
            if (resultCode != RESULT_OK || u == null) { r.success(null); return }
            io.execute {
                val txt = try { contentResolver.openInputStream(u)!!.use { it.readBytes().toString(Charsets.UTF_8) } } catch (e: Exception) { null }
                mainHandler.post { r.success(txt) }
            }
            return
        }
        if (requestCode != REQ_AUDIO) return
        val res = pendingAudio ?: return
        pendingAudio = null
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) { res.success(null); return }
        io.execute {
            val path = try {
                val name = contentResolver.getType(uri)?.substringAfter('/')?.take(5) ?: "mp3"
                val f = java.io.File(filesDir, "key_sound.$name")
                contentResolver.openInputStream(uri)!!.use { inp -> f.outputStream().use { inp.copyTo(it) } }
                f.absolutePath
            } catch (e: Exception) { null }
            mainHandler.post { res.success(path) }
        }
    }

    /** แอปเปิดอยู่แล้ว แล้วถูกเรียกขึ้นมาใหม่ตอนรถติด */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        intent.getStringExtra("autoStartReason")?.let { chan?.invokeMethod("carWake", it) }
    }

    // ---------------- เปิดอัตโนมัติ / หน้าล็อก ----------------
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyLockScreen()
        AutoStart.syncService(this)
    }

    override fun onResume() {
        super.onResume()
        AutoStart.inFront = true
        applyLockScreen()
    }

    override fun onPause() {
        AutoStart.inFront = false
        super.onPause()
    }

    /** แสดงแอปทับหน้าล็อกได้เลย (ไม่ต้องปลดล็อก) ถ้าเปิดค่าตั้งไว้ */
    private fun applyLockScreen() {
        if (Build.VERSION.SDK_INT >= 27) {
            val on = AutoStart.prefBool(this, "showOnLock")
            setShowWhenLocked(on)
            setTurnScreenOn(on)
        }
    }

    // ---------------- สิทธิ์ ----------------
    private fun hasConnectPermission(): Boolean {
        if (Build.VERSION.SDK_INT < 31) return true
        return checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) ==
            PackageManager.PERMISSION_GRANTED
    }

    private fun ensurePermission(result: MethodChannel.Result) {
        if (hasConnectPermission()) {
            result.success(true)
            return
        }
        if (Build.VERSION.SDK_INT >= 31) {
            pendingPermission?.success(false)
            pendingPermission = result
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), REQ_BT)
        } else {
            result.success(true)
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_BT) {
            val ok = grantResults.isNotEmpty() &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED
            pendingPermission?.success(ok)
            pendingPermission = null
        }
    }

    // ---------------- Bluetooth ----------------
    private fun adapter(): BluetoothAdapter? =
        (getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    @SuppressLint("MissingPermission")
    private fun bondedDevices(): List<Map<String, String>> {
        if (!hasConnectPermission()) return emptyList()
        val ad = adapter() ?: return emptyList()
        return try {
            ad.bondedDevices?.map { d ->
                mapOf("name" to (d.name ?: d.address), "address" to d.address)
            } ?: emptyList()
        } catch (e: SecurityException) {
            emptyList()
        }
    }

    @SuppressLint("MissingPermission")
    private fun connect(address: String): String? {
        closeSocket()
        if (!hasConnectPermission()) return "ไม่ได้รับสิทธิ์ Bluetooth"
        val ad = adapter() ?: return "มือถือไม่มี Bluetooth"
        if (!ad.isEnabled) return "Bluetooth ปิดอยู่"
        val device = try {
            ad.getRemoteDevice(address)
        } catch (e: IllegalArgumentException) {
            return "MAC address ไม่ถูกต้อง"
        }
        try {
            ad.cancelDiscovery()
        } catch (e: Exception) {
            // ต้องใช้สิทธิ์ SCAN บน Android 12+ ไม่จำเป็น ข้ามได้
        }

        val makers: List<() -> BluetoothSocket> = listOf(
            { device.createRfcommSocketToServiceRecord(sppUuid) },
            { device.createInsecureRfcommSocketToServiceRecord(sppUuid) },
            {
                device.javaClass
                    .getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
                    .invoke(device, 1) as BluetoothSocket
            }
        )
        var lastError = "unknown"
        for (make in makers) {
            var s: BluetoothSocket? = null
            try {
                val created = make()
                s = created
                created.connect()
                generation += 1
                val gen = generation
                socket = created
                out = created.outputStream
                startReader(created, gen)
                return null
            } catch (e: Exception) {
                lastError = e.message ?: e.toString()
                try {
                    s?.close()
                } catch (e2: Exception) {
                }
            }
        }
        return "ต่อไม่ได้: $lastError"
    }

    private fun startReader(s: BluetoothSocket, gen: Int) {
        val input = s.inputStream
        val t = Thread {
            val buf = ByteArray(1024)
            try {
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    if (n == 0) continue
                    val text = String(buf, 0, n, Charsets.ISO_8859_1)
                    mainHandler.post {
                        sink?.success(mapOf("t" to "data", "d" to text, "g" to gen))
                    }
                }
            } catch (e: Exception) {
                // socket ถูกปิด
            }
            mainHandler.post { sink?.success(mapOf("t" to "closed", "g" to gen)) }
        }
        t.isDaemon = true
        t.start()
    }

    private fun closeSocket() {
        val s = socket
        socket = null
        out = null
        try {
            s?.close()
        } catch (e: Exception) {
        }
    }

    // ---------------- WiFi ----------------
    @Suppress("DEPRECATION")
    private fun bindWifi(on: Boolean): Boolean {
        if (Build.VERSION.SDK_INT < 23) return false
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            ?: return false
        if (!on) {
            cm.bindProcessToNetwork(null)
            return true
        }
        for (n in cm.allNetworks) {
            val caps = cm.getNetworkCapabilities(n) ?: continue
            if (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                return cm.bindProcessToNetwork(n)
            }
        }
        return false
    }

    override fun onDestroy() {
        closeSocket()
        io.shutdownNow()
        super.onDestroy()
    }

    companion object {
        private const val REQ_BT = 4711
        private const val REQ_NOTIF = 4712
        private const val REQ_AUDIO = 4713
        private const val REQ_TEXT = 4714
        private const val REQ_FILE = 4715
        private const val REQ_GPS = 4716
    }
}
