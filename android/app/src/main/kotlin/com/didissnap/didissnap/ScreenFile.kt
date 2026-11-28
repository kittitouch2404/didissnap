package com.didissnap.didissnap

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import java.util.zip.CRC32

/**
 * ส่งไฟล์ (รูป / GIF) เข้าจอ 7B ทาง BLE — ต่อจอตรง ๆ ครั้งเดียวแล้วตัด (ไม่ยุ่งกับการต่อ Superlink)
 *  จอต้องกด "เปิดรับ" ในเมนู SD card ก่อน (จอจะประกาศตัวเป็น DIDI-7B 3 นาที)
 *  ขั้นตอน: หา (service d1d15700) → ต่อ → ขอ MTU 247 → 01 เริ่ม → 02 ข้อมูลทีละชิ้น (รอตอบรับ) → 03 CRC32 → ตัด
 */
@SuppressLint("MissingPermission")
class ScreenFile(private val ctx: Context, private val onProgress: (Int) -> Unit) {
    companion object {
        val SVC: UUID = UUID.fromString("d1d15700-6f0b-4e8b-9c4e-0b5d1d150000")
        val FILE: UUID = UUID.fromString("d1d15701-6f0b-4e8b-9c4e-0b5d1d150000")
    }

    private val main = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var ch: BluetoothGattCharacteristic? = null
    private var done: ((String?) -> Unit)? = null
    private var data = ByteArray(0)
    private var name = ""
    private var off = 0
    private var chunk = 180
    private var step = 0          // 0 หา · 1 ต่อ · 2 เริ่ม · 3 ข้อมูล · 4 จบ
    private var seq = 0
    private var lastPct = -1
    private var scanning = false
    private val adapter get() = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    fun send(fileName: String, bytes: ByteArray, cb: (String?) -> Unit) {
        if (done != null) { cb("busy"); return }
        val ad = adapter
        if (ad == null || !ad.isEnabled) { cb("Bluetooth ปิดอยู่"); return }
        done = cb
        data = bytes
        name = fileName
        off = 0
        lastPct = -1
        step = 0
        val sc = ad.bluetoothLeScanner ?: return finish("Bluetooth ปิดอยู่")
        try {
            val f = ScanFilter.Builder().setServiceUuid(ParcelUuid(SVC)).build()
            val st = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
            sc.startScan(listOf(f), st, scanCb)
            scanning = true
        } catch (e: Exception) { return finish("สแกนไม่ได้: ${e.message}") }
        timeout(12000, "ไม่เจอจอ 7B — ที่จอกด เมนู → SD CARD → เปิด/ปิดรับ ก่อน")
    }

    private fun timeout(ms: Long, msg: String) {
        val s = ++seq
        main.postDelayed({ if (done != null && seq == s) finish(msg) }, ms)
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try { adapter?.bluetoothLeScanner?.stopScan(scanCb) } catch (e: Exception) {}
    }

    private fun finish(err: String?) {
        seq++
        stopScan()
        val g = gatt
        gatt = null
        ch = null
        try { g?.disconnect(); g?.close() } catch (e: Exception) {}
        val d = done
        done = null
        data = ByteArray(0)
        d?.invoke(err)
    }

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (!scanning || step != 0) return
            stopScan()
            step = 1
            main.post { connect(result.device) }
        }
        override fun onScanFailed(errorCode: Int) { main.post { finish("สแกนไม่ได้ ($errorCode)") } }
    }

    private fun connect(d: BluetoothDevice) {
        timeout(10000, "ต่อจอ 7B ไม่ได้")
        gatt = if (Build.VERSION.SDK_INT >= 23) d.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE)
        else d.connectGatt(ctx, false, cb)
    }

    @Suppress("DEPRECATION")
    private fun write(b: ByteArray): Boolean {
        val g = gatt ?: return false
        val c = ch ?: return false
        return try {
            if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(c, b, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
            else { c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT; c.value = b; g.writeCharacteristic(c) }
        } catch (e: Exception) { false }
    }

    private fun le(n: Int) = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(n).array()

    private fun next() {
        when (step) {
            2 -> { if (!write(byteArrayOf(1) + le(data.size) + name.toByteArray())) return finish("ส่งไม่ได้ (เริ่มไฟล์)") }
            3 -> {
                val n = minOf(chunk, data.size - off)
                if (!write(byteArrayOf(2) + le(off) + data.copyOfRange(off, off + n))) return finish("ส่งไม่ได้ (ข้อมูล)")
            }
            4 -> {
                val c = CRC32(); c.update(data)
                if (!write(byteArrayOf(3) + le(c.value.toInt()))) return finish("ส่งไม่ได้ (จบไฟล์)")
            }
        }
        timeout(6000, "จอ 7B ไม่ตอบ")
    }

    private fun errText(st: Int) = when (st) {
        0x80 -> "ชื่อหรือขนาดไฟล์ไม่ถูก"
        0x81 -> "ข้อมูลขาดตอน"
        0x82 -> "เขียน SD card ไม่ได้ (ใส่การ์ดหรือยัง?)"
        0x83 -> "ไฟล์เสีย (CRC ไม่ตรง) ลองใหม่"
        0x84 -> "จอปิดรับแล้ว — กด เปิด/ปิดรับ ที่จออีกครั้ง"
        else -> "ผิดพลาด ($st)"
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                if (!g.requestMtu(247)) g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                main.post { if (gatt === g && done != null) finish(if (step >= 4) null else "หลุดกลางทาง") }
            }
        }
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            chunk = (mtu - 3 - 5).coerceIn(15, 500)
            g.discoverServices()
        }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            main.post {
                if (gatt !== g) return@post
                ch = g.getService(SVC)?.getCharacteristic(FILE)
                if (ch == null) return@post finish("จอ 7B ไม่มีบริการรับไฟล์ (อัปเดตเฟิร์มแวร์จอ)")
                step = 2
                next()
            }
        }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post {
                if (gatt !== g || done == null) return@post
                if (status != BluetoothGatt.GATT_SUCCESS) return@post finish(errText(status))
                when (step) {
                    2 -> { step = 3; next() }
                    3 -> {
                        off += minOf(chunk, data.size - off)
                        val p = (off.toLong() * 100 / data.size).toInt()
                        if (p != lastPct) { lastPct = p; onProgress(p) }
                        if (off >= data.size) step = 4
                        next()
                    }
                    4 -> { step = 5; finish(null) }
                }
            }
        }
    }
}
