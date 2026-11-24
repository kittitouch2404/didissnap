package com.didissnap.didissnap

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
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
import java.util.UUID

/**
 * ต่อ ESP32-C3 (ตัวอ่าน OBD ที่ช่อง OBD2) ทาง Bluetooth BLE
 *  - หา C3 จาก service UUID -> ต่อ -> ขอ MTU 185 -> เปิดแจ้งเตือน DATA
 *  - แพ็กเก็ต (DidiNowGw / Dtc / Info / RawRes) ส่งให้ Flutter ทาง [onPacket]
 *  - หลุดแล้วหาใหม่เองจนกว่าจะ stop()
 */
@SuppressLint("MissingPermission")
class C3Ble(private val ctx: Context, private val onPacket: (ByteArray) -> Unit, private val onState: (String) -> Unit) {
    companion object {
        val SVC: UUID = UUID.fromString("d1d15500-6f0b-4e8b-9c4e-0b5d1d150000")
        val DATA: UUID = UUID.fromString("d1d15501-6f0b-4e8b-9c4e-0b5d1d150000")
        val CMD: UUID = UUID.fromString("d1d15502-6f0b-4e8b-9c4e-0b5d1d150000")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val main = Handler(Looper.getMainLooper())
    private var running = false
    private var scanning = false
    private var gatt: BluetoothGatt? = null
    private var cmdC: BluetoothGattCharacteristic? = null
    var mtuNow = 23
    private val adapter: BluetoothAdapter? get() = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    fun start() {
        if (running) return
        running = true
        scan()
    }

    fun stop() {
        running = false
        stopScan()
        try { gatt?.disconnect(); gatt?.close() } catch (e: Exception) {}
        gatt = null
        cmdC = null
        onState("off")
    }

    // คิวเขียน: Android เขียน BLE ได้ทีละครั้ง ต้องรอตอบรับก่อนเขียนอันถัดไป
    private val queue = ArrayDeque<Pair<ByteArray, ((Boolean) -> Unit)?>>()
    private var busy: ((Boolean) -> Unit)? = null
    private var busyOn = false
    private var busySeq = 0
    private var failN = 0

    /** ส่งคำสั่งให้ C3 (เข้าคิว) — [done] = เขียนสำเร็จ/ไม่สำเร็จ (C3 ตอบรับแล้ว) */
    fun write(b: ByteArray, done: ((Boolean) -> Unit)? = null): Boolean {
        if (gatt == null || cmdC == null) { done?.invoke(false); return false }
        main.post {
            if (queue.size > 64) { done?.invoke(false); return@post }
            queue.addLast(b to done)
            pump()
        }
        return true
    }

    @Suppress("DEPRECATION")
    private fun pump() {
        if (busyOn) return
        val g = gatt
        val c = cmdC
        if (g == null || c == null) { flush(); return }
        val (b, done) = queue.removeFirstOrNull() ?: return
        val ok = try {
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(c, b, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == 0
            } else {
                c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                c.value = b
                g.writeCharacteristic(c)
            }
        } catch (e: Exception) { false }
        if (!ok) {   // Android ยังไม่ว่าง (เช่นกำลังเขียน descriptor) → ลองใหม่สูงสุด 10 ครั้ง
            if (++failN <= 10) queue.addFirst(b to done) else { failN = 0; done?.invoke(false) }
            main.postDelayed({ pump() }, 20)
            return
        }
        failN = 0
        busyOn = true
        busy = done
        val seq = ++busySeq
        main.postDelayed({ if (busyOn && busySeq == seq) finish(false) }, 3000)   // ไม่ตอบรับ 3 วิ
    }

    private fun finish(ok: Boolean) {
        val d = busy
        busy = null
        busyOn = false
        d?.invoke(ok)
        pump()
    }

    private fun flush() {
        busy?.invoke(false); busy = null; busyOn = false
        while (queue.isNotEmpty()) queue.removeFirst().second?.invoke(false)
    }

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (!scanning) return
            stopScan()
            connect(result.device)
        }
        override fun onScanFailed(errorCode: Int) {
            scanning = false
            onState("scan failed $errorCode")
            retry()
        }
    }

    private fun scan() {
        if (!running) return
        val sc = adapter?.bluetoothLeScanner
        if (sc == null) { onState("Bluetooth off"); retry(); return }
        onState("searching C3")
        val f = ScanFilter.Builder().setServiceUuid(ParcelUuid(SVC)).build()
        val st = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try {
            sc.startScan(listOf(f), st, scanCb)
            scanning = true
            // ไม่เจอใน 8 วิ -> หยุดแล้วลองใหม่ (Android ไม่ชอบสแกนค้างนาน)
            main.postDelayed({ if (scanning) { stopScan(); retry() } }, 8000)
        } catch (e: Exception) {
            onState("scan error: ${e.message}")
            retry()
        }
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try { adapter?.bluetoothLeScanner?.stopScan(scanCb) } catch (e: Exception) {}
    }

    private fun retry() {
        if (running) main.postDelayed({ if (running && gatt == null && !scanning) scan() }, 2000)
    }

    private fun connect(d: BluetoothDevice) {
        onState("connecting ${d.address}")
        gatt = if (Build.VERSION.SDK_INT >= 23) d.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE)
        else d.connectGatt(ctx, false, cb)
    }

    private val cb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                if (!g.requestMtu(185)) g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                try { g.close() } catch (e: Exception) {}
                main.post {
                    if (gatt === g) { gatt = null; cmdC = null; flush() }
                    onState("disconnected")
                    retry()
                }
            }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            main.post { if (gatt === g) finish(status == BluetoothGatt.GATT_SUCCESS) }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            mtuNow = mtu
            g.discoverServices()
        }

        @Suppress("DEPRECATION")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val s = g.getService(SVC)
            val dc = s?.getCharacteristic(DATA)
            cmdC = s?.getCharacteristic(CMD)
            if (dc == null || cmdC == null) { g.disconnect(); return }
            g.setCharacteristicNotification(dc, true)
            val d = dc.getDescriptor(CCCD)
            if (d != null) {
                if (Build.VERSION.SDK_INT >= 33) {
                    g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                } else {
                    d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    g.writeDescriptor(d)
                }
            }
            main.post { onState("connected") }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            if (c.uuid == DATA) { val v = value.copyOf(); main.post { onPacket(v) } }
        }

        @Deprecated("Deprecated in Java")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT >= 33) return   // API 33+ ใช้ตัวบน
            if (c.uuid == DATA) { val v = c.value?.copyOf() ?: return; main.post { onPacket(v) } }
        }
    }
}
