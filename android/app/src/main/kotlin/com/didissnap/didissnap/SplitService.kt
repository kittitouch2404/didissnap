package com.didissnap.didissnap

import android.accessibilityservice.AccessibilityService
import android.view.accessibility.AccessibilityEvent

/**
 * ตัวช่วยแบ่งจอ: แอปทั่วไปสั่ง Android ให้เข้า/ออกโหมดแบ่งจอเองไม่ได้
 * ต้องผ่าน Accessibility (GLOBAL_ACTION_TOGGLE_SPLIT_SCREEN) — ไม่อ่านหน้าจอ / ไม่รับเหตุการณ์ของแอปอื่น
 */
class SplitService : AccessibilityService() {
    companion object {
        @Volatile var inst: SplitService? = null
        fun toggle(): Boolean = inst?.performGlobalAction(GLOBAL_ACTION_TOGGLE_SPLIT_SCREEN) ?: false
    }

    override fun onServiceConnected() { inst = this }
    override fun onUnbind(intent: android.content.Intent?): Boolean { inst = null; return super.onUnbind(intent) }
    override fun onDestroy() { inst = null; super.onDestroy() }
    override fun onAccessibilityEvent(event: AccessibilityEvent?) {}
    override fun onInterrupt() {}
}
