/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.camerahelper

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.hardware.TriggerEvent
import android.hardware.TriggerEventListener
import android.os.Handler
import android.util.Log

/**
 * Uses the vendor free-fall sensor when it is exposed by the SSC HAL. Devices
 * without that sensor use a conservative, duration-based accelerometer check.
 */
class FallHandler(
    private val sensorManager: SensorManager,
    private val handler: Handler,
    private val onFall: () -> Unit,
) : SensorEventListener {
    private var activeSensor: Sensor? = null
    private var usingAccelerometer = false
    private var usingTriggerSensor = false
    private var lowGravityStartedAtNanos = 0L
    private var rearmStartedAtNanos = 0L
    private var accelerometerArmed = true
    private var triggerRearmFailures = 0

    private val triggerRearmRunnable = object : Runnable {
        override fun run() {
            val sensor = activeSensor
            if (sensor == null) {
                if (!start()) handler.postDelayed(this, TRIGGER_REARM_DELAY_MILLIS)
                return
            }
            if (!usingTriggerSensor) return
            if (requestTrigger(sensor)) {
                triggerRearmFailures = 0
                return
            }

            triggerRearmFailures++
            if (triggerRearmFailures < MAX_TRIGGER_REARM_FAILURES) {
                Log.e(TAG, "Unable to rearm vendor fall trigger; retrying")
                handler.postDelayed(this, TRIGGER_REARM_DELAY_MILLIS)
                return
            }

            Log.e(TAG, "Vendor fall trigger remained unavailable; selecting a fallback")
            activeSensor = null
            usingTriggerSensor = false
            triggerRearmFailures = 0
            if (!start()) handler.postDelayed(this, TRIGGER_REARM_DELAY_MILLIS)
        }
    }

    private val triggerListener = object : TriggerEventListener() {
        override fun onTrigger(event: TriggerEvent) {
            val triggeredSensor = event.sensor
            handler.post {
                if (activeSensor != triggeredSensor || !usingTriggerSensor) return@post
                onFall()
                triggerRearmFailures = 0
                handler.removeCallbacks(triggerRearmRunnable)
                handler.postDelayed(triggerRearmRunnable, TRIGGER_REARM_DELAY_MILLIS)
            }
        }
    }

    fun start(): Boolean {
        if (activeSensor != null) return true
        handler.removeCallbacks(triggerRearmRunnable)
        resetAccelerometerState()

        val vendorSensors = try {
            findVendorSensors()
        } catch (e: RuntimeException) {
            Log.e(TAG, "Unable to enumerate vendor fall sensors", e)
            emptyList()
        }
        for (vendorSensor in vendorSensors) {
            if (vendorSensor.reportingMode == Sensor.REPORTING_MODE_ONE_SHOT) {
                if (requestTrigger(vendorSensor)) {
                    activeSensor = vendorSensor
                    usingTriggerSensor = true
                    Log.i(TAG, "Using vendor fall trigger ${vendorSensor.stringType}")
                    return true
                }
                continue
            }
            if (register(vendorSensor, SensorManager.SENSOR_DELAY_NORMAL)) {
                activeSensor = vendorSensor
                usingAccelerometer = false
                Log.i(TAG, "Using vendor fall sensor ${vendorSensor.stringType}")
                return true
            }
        }

        val accelerometer = try {
            sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        } catch (e: RuntimeException) {
            Log.e(TAG, "Unable to get accelerometer fallback", e)
            null
        }
        if (accelerometer != null && register(accelerometer, SensorManager.SENSOR_DELAY_GAME)) {
            activeSensor = accelerometer
            usingAccelerometer = true
            Log.w(TAG, "Vendor fall sensor unavailable; using accelerometer fallback")
            return true
        } else {
            Log.e(TAG, "No usable fall-detection sensor; retrying")
            return false
        }
    }

    fun stop() {
        handler.removeCallbacks(triggerRearmRunnable)
        try {
            if (usingTriggerSensor) {
                sensorManager.cancelTriggerSensor(triggerListener, activeSensor)
            } else {
                sensorManager.unregisterListener(this)
            }
        } catch (e: RuntimeException) {
            Log.w(TAG, "Unable to unregister fall sensor", e)
        }
        activeSensor = null
        resetAccelerometerState()
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor != activeSensor) return
        if (!usingAccelerometer) {
            if (event.values.isNotEmpty() && event.values[0] > 0.0f) {
                onFall()
            }
            return
        }

        if (event.values.size < 3) return

        val x = event.values[0]
        val y = event.values[1]
        val z = event.values[2]
        val magnitudeSquared = x * x + y * y + z * z
        val now = event.timestamp

        if (accelerometerArmed && magnitudeSquared < FREE_FALL_THRESHOLD_SQUARED) {
            if (lowGravityStartedAtNanos == 0L) {
                lowGravityStartedAtNanos = now
            } else if (now - lowGravityStartedAtNanos >= FREE_FALL_DURATION_NANOS) {
                accelerometerArmed = false
                lowGravityStartedAtNanos = 0L
                rearmStartedAtNanos = 0L
                onFall()
            }
            return
        }

        lowGravityStartedAtNanos = 0L
        if (!accelerometerArmed && magnitudeSquared > REARM_THRESHOLD_SQUARED) {
            if (rearmStartedAtNanos == 0L) {
                rearmStartedAtNanos = now
            } else if (now - rearmStartedAtNanos >= REARM_DURATION_NANOS) {
                accelerometerArmed = true
                rearmStartedAtNanos = 0L
            }
        } else if (!accelerometerArmed) {
            rearmStartedAtNanos = 0L
        }
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) = Unit

    private fun findVendorSensors(): List<Sensor> {
        val sensors = sensorManager.getSensorList(Sensor.TYPE_ALL)
        val result = mutableListOf<Sensor>()
        for (type in VENDOR_SENSOR_TYPES) {
            sensors.filterTo(result) { it.stringType == type }
        }
        return result
    }

    private fun register(sensor: Sensor, rate: Int): Boolean {
        return try {
            sensorManager.registerListener(this, sensor, rate, handler)
        } catch (e: RuntimeException) {
            Log.e(TAG, "Unable to register fall sensor ${sensor.stringType}", e)
            false
        }
    }

    private fun requestTrigger(sensor: Sensor): Boolean {
        return try {
            sensorManager.requestTriggerSensor(triggerListener, sensor)
        } catch (e: RuntimeException) {
            Log.e(TAG, "Unable to request fall trigger ${sensor.stringType}", e)
            false
        }
    }

    private fun resetAccelerometerState() {
        usingAccelerometer = false
        usingTriggerSensor = false
        lowGravityStartedAtNanos = 0L
        rearmStartedAtNanos = 0L
        accelerometerArmed = true
        triggerRearmFailures = 0
    }

    private companion object {
        const val TAG = "CameraFallHandler"

        val VENDOR_SENSOR_TYPES = listOf(
            "free_fall_detect",
            "free_fall",
            "camera_protect",
        )

        const val FREE_FALL_THRESHOLD_MPS2 = 3.0f
        const val FREE_FALL_THRESHOLD_SQUARED =
            FREE_FALL_THRESHOLD_MPS2 * FREE_FALL_THRESHOLD_MPS2
        const val FREE_FALL_DURATION_NANOS = 120_000_000L

        const val REARM_THRESHOLD_MPS2 = 7.0f
        const val REARM_THRESHOLD_SQUARED = REARM_THRESHOLD_MPS2 * REARM_THRESHOLD_MPS2
        const val REARM_DURATION_NANOS = 750_000_000L
        const val TRIGGER_REARM_DELAY_MILLIS = 1_000L
        const val MAX_TRIGGER_REARM_FAILURES = 5
    }
}
