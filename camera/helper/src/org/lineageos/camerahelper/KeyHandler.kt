/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.camerahelper

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.KeyEvent
import com.android.internal.os.DeviceKeyHandler
import java.util.ArrayDeque
import vendor.oplus.hardware.camera.signal.V1_0.ICameraSignal

/** Loaded by system_server through config_deviceKeyHandlerClasses. */
class KeyHandler(@Suppress("UNUSED_PARAMETER") context: Context) : DeviceKeyHandler {
    private var signal: ICameraSignal? = null
    private val retryHandler = Handler(Looper.getMainLooper())
    private val pendingEvents = ArrayDeque<MotorEvent>()
    private var retryScheduled = false
    private val retryRunnable = Runnable { drainPendingEvents() }

    override fun handleKeyEvent(event: KeyEvent): KeyEvent? {
        val scanCode = event.scanCode
        if (scanCode !in MOTOR_SCAN_CODES) return event

        if (event.action == KeyEvent.ACTION_DOWN) {
            enqueueEvent(
                MotorEvent(
                    scanCode,
                    event.eventTime * NANOS_PER_MILLISECOND,
                    SystemClock.elapsedRealtime(),
                ),
            )
            drainPendingEvents()
        }

        return null
    }

    @Synchronized
    private fun enqueueEvent(event: MotorEvent) {
        if (pendingEvents.size == MAX_PENDING_EVENTS) {
            val dropped = pendingEvents.removeFirst()
            Log.e(TAG, "Dropping stale motor event ${dropped.scanCode}; retry queue is full")
        }
        pendingEvents.addLast(event)
    }

    @Synchronized
    private fun drainPendingEvents() {
        retryHandler.removeCallbacks(retryRunnable)
        retryScheduled = false

        while (pendingEvents.isNotEmpty()) {
            val event = pendingEvents.first()
            if (SystemClock.elapsedRealtime() - event.queuedAtMillis > EVENT_TTL_MILLIS) {
                pendingEvents.removeFirst()
                Log.w(TAG, "Dropping expired motor event ${event.scanCode}")
                continue
            }
            val service = getSignalLocked()
            if (service == null) {
                scheduleRetryLocked()
                return
            }
            try {
                service.notifyMotorEvent(event.scanCode, event.eventTimeNanos)
                pendingEvents.removeFirst()
            } catch (e: Exception) {
                Log.e(TAG, "Unable to forward motor event ${event.scanCode}; retrying", e)
                signal = null
                scheduleRetryLocked()
                return
            }
        }
    }

    private fun scheduleRetryLocked() {
        if (retryScheduled) return
        retryScheduled = true
        retryHandler.postDelayed(retryRunnable, RETRY_DELAY_MILLIS)
    }

    private fun getSignalLocked(): ICameraSignal? {
        signal?.let { return it }
        return try {
            ICameraSignal.getService(SIGNAL_INSTANCE, false).also { signal = it }
        } catch (e: Exception) {
            Log.d(TAG, "Camera signal broker is not available yet", e)
            null
        }
    }

    private data class MotorEvent(
        val scanCode: Int,
        val eventTimeNanos: Long,
        val queuedAtMillis: Long,
    )

    private companion object {
        const val TAG = "CameraKeyHandler"
        const val NANOS_PER_MILLISECOND = 1_000_000L
        const val RETRY_DELAY_MILLIS = 250L
        const val EVENT_TTL_MILLIS = 2_000L
        const val MAX_PENDING_EVENTS = 32
        const val SIGNAL_INSTANCE = "default"
        val MOTOR_SCAN_CODES = 183..190
    }
}
