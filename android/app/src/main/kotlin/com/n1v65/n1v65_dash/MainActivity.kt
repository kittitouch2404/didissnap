package com.n1v65.n1v65_dash

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Build
import android.os.Handler
import android.os.Looper
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

    @Volatile private var socket: BluetoothSocket? = null
    @Volatile private var out: OutputStream? = null
    @Volatile private var generation = 0
    private var sink: EventChannel.EventSink? = null
    private var pendingPermission: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        val messenger = flutterEngine.dartExecutor.binaryMessenger

        MethodChannel(messenger, "n1v65/native").setMethodCallHandler { call, result ->
            when (call.method) {
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
                else -> result.notImplemented()
            }
        }

        EventChannel(messenger, "n1v65/bt/stream").setStreamHandler(object : EventChannel.StreamHandler {
            override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                sink = events
            }

            override fun onCancel(arguments: Any?) {
                sink = null
            }
        })
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
    }
}
