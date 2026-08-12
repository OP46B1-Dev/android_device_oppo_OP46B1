/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.camerahelper

import android.app.AlertDialog
import android.app.Service
import android.content.Intent
import android.hardware.SensorManager
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.IHwBinder
import android.os.Looper
import android.os.ServiceManager
import android.os.SystemClock
import android.util.Log
import android.view.WindowManager
import vendor.oplus.hardware.camera.signal.V1_0.CameraState
import vendor.oplus.hardware.camera.signal.V1_0.ICameraSignal
import vendor.oplus.hardware.camera.signal.V1_0.ICameraSignalListener
import vendor.oplus.hardware.motor.IMotor

class CameraMotorService : Service() {
    private lateinit var stateThread: HandlerThread
    private lateinit var stateHandler: Handler
    private lateinit var cameraManager: CameraManager
    private lateinit var sensorManager: SensorManager
    private val mainHandler = Handler(Looper.getMainLooper())

    private var signal: ICameraSignal? = null
    private var motor: IMotor? = null
    private var fallHandler: FallHandler? = null
    private var calibrationReady = false
    private var torchCallbackRegistered = false
    @Volatile private var destroyed = false

    private val sessions = mutableMapOf<SessionKey, Int>()
    private val lastSequence = mutableMapOf<SessionKey, Long>()
    private val lastConfigurationId = mutableMapOf<SessionKey, Int>()
    private val latestSessionId = mutableMapOf<Int, Long>()
    private val cameraIdsAwaitingReplay = mutableSetOf<Int>()
    private val torchStates = mutableMapOf<String, Boolean>()
    private var torchCameraIds = setOf(DEFAULT_TORCH_CAMERA_ID)

    private var commandedDirection = DIRECTION_UNKNOWN
    private var movingDirection = DIRECTION_UNKNOWN
    private var settledDirection = DIRECTION_UNKNOWN
    private var faultDirection = DIRECTION_UNKNOWN
    private val suppressedAbnormalUntilMillis = mutableMapOf<Int, Long>()
    private val staleMoveEventGuards = mutableMapOf<Int, StaleMoveEventGuard>()
    private var downPending = false
    private var restartAfterDown = false
    private var safetyReason = SafetyReason.NONE
    private var safetyLatchedAtMillis = 0L
    private var safetyReleaseRequested = false
    private var raiseAfterSafetyRelease = false
    private var safetyTorchIds = emptySet<String>()
    private var pendingTorchRestoreIds = emptySet<String>()
    private val pendingTorchDisableIds = mutableSetOf<String>()
    private var activeDialog: AlertDialog? = null
    private val lastMotorEventByCode = mutableMapOf<Int, Long>()

    private val signalListener = object : ICameraSignalListener.Stub() {
        override fun onCameraStateChanged(
            cameraId: Int,
            state: Int,
            sessionId: Long,
            sequence: Long,
            configurationId: Int,
        ) {
            if (destroyed) return
            stateHandler.post {
                if (!destroyed) {
                    handleCameraState(cameraId, state, sessionId, sequence, configurationId)
                }
            }
        }

        override fun onMotorEvent(scanCode: Int, eventTimeNanos: Long) {
            if (destroyed) return
            stateHandler.post {
                if (!destroyed) handleMotorEvent(scanCode, eventTimeNanos)
            }
        }
    }

    private val torchCallback = object : CameraManager.TorchCallback() {
        override fun onTorchModeChanged(cameraId: String, enabled: Boolean) {
            if (destroyed) return
            if (!isMotorTorch(cameraId)) return
            torchStates[cameraId] = enabled
            if (!enabled) {
                pendingTorchDisableIds.remove(cameraId)
                cancelTorchDisableRetryIfIdle()
            }
            if (enabled && safetyReason != SafetyReason.NONE) {
                safetyTorchIds = safetyTorchIds + cameraId
                disableTorch(setOf(cameraId))
            }
            reconcileMotorState()
        }

        override fun onTorchModeUnavailable(cameraId: String) {
            if (destroyed) return
            if (!isMotorTorch(cameraId)) return
            if (cameraId in pendingTorchDisableIds) {
                // Unavailable does not prove that the physical torch is off.
                torchStates[cameraId] = true
                scheduleTorchDisableRetry()
            } else {
                torchStates[cameraId] = false
            }
            reconcileMotorState()
        }
    }

    private val motorDeathRecipient = IBinder.DeathRecipient {
        if (!destroyed) {
            stateHandler.post {
                if (destroyed) return@post
                Log.e(TAG, "Motor HAL died")
                motor = null
                calibrationReady = false
                commandedDirection = DIRECTION_UNKNOWN
                movingDirection = DIRECTION_UNKNOWN
                settledDirection = DIRECTION_UNKNOWN
                faultDirection = DIRECTION_UNKNOWN
                clearRestartRequest()
                suppressedAbnormalUntilMillis.clear()
                staleMoveEventGuards.clear()
                scheduleReconnect()
            }
        }
    }

    private val signalDeathRecipient = IHwBinder.DeathRecipient {
        if (!destroyed) {
            stateHandler.post {
                if (destroyed) return@post
                Log.e(TAG, "Camera signal broker died")
                signal = null
                // The restarted broker may replay the same camera generation.
                lastSequence.clear()
                cameraIdsAwaitingReplay.clear()
                latestSessionId.keys.toCollection(cameraIdsAwaitingReplay)
                stateHandler.removeCallbacks(signalReplayTimeoutRunnable)
                if (cameraIdsAwaitingReplay.isNotEmpty()) {
                    stateHandler.postDelayed(
                        signalReplayTimeoutRunnable,
                        SIGNAL_REPLAY_TIMEOUT_MILLIS,
                    )
                }
                scheduleReconnect()
            }
        }
    }

    private val reconnectRunnable = Runnable { connectServices() }
    private val inputRetryRunnable = Runnable { startInputObservers() }
    private val torchDisableRetryRunnable = Runnable {
        if (destroyed || pendingTorchDisableIds.isEmpty()) return@Runnable
        disableTorch(pendingTorchDisableIds.toSet())
        reconcileMotorState()
    }

    private val downRunnable = Runnable {
        downPending = false
        if (!hasRaiseDemand() && safetyReason == SafetyReason.NONE) {
            requestMove(IMotor.DIRECTION_DOWN, IMotor.START_NORMAL)
        }
    }

    private val restartTimeoutRunnable = Runnable {
        if (!restartAfterDown) return@Runnable
        Log.w(TAG, "Motor restart timed out while retracting; clearing restart request")
        restartAfterDown = false
        reconcileMotorState()
    }

    private val safetyReleaseRunnable = Runnable { reconcileMotorState() }

    private val signalReplayTimeoutRunnable = Runnable {
        if (cameraIdsAwaitingReplay.isEmpty()) return@Runnable

        val unreplayedCameraIds = cameraIdsAwaitingReplay.toSet()
        cameraIdsAwaitingReplay.clear()
        sessions.keys.removeAll { it.cameraId in unreplayedCameraIds }
        lastSequence.keys.removeAll { it.cameraId in unreplayedCameraIds }
        lastConfigurationId.keys.removeAll { it.cameraId in unreplayedCameraIds }
        Log.w(
            TAG,
            "Camera signal replay timed out for ${unreplayedCameraIds.sorted()}; " +
                "discarding stale sessions",
        )
        reconcileMotorState()
    }

    override fun onCreate() {
        super.onCreate()

        stateThread = HandlerThread("CameraMotorCoordinator")
        stateThread.start()
        stateHandler = Handler(stateThread.looper)
        cameraManager = requireNotNull(getSystemService(CameraManager::class.java)) {
            "CameraManager unavailable"
        }
        sensorManager = requireNotNull(getSystemService(SensorManager::class.java)) {
            "SensorManager unavailable"
        }
        torchCameraIds = findTorchCameraIds()

        stateHandler.post { connectServices() }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        destroyed = true
        if (::stateHandler.isInitialized) {
            stateHandler.post {
                stateHandler.removeCallbacksAndMessages(null)
                try {
                    signal?.unregisterListener(signalListener)
                } catch (e: Exception) {
                    Log.w(TAG, "Unable to unregister camera listener", e)
                }
                unlinkSignalDeathRecipient()
                unlinkMotorDeathRecipient()
                if (torchCallbackRegistered) {
                    try {
                        cameraManager.unregisterTorchCallback(torchCallback)
                    } catch (e: RuntimeException) {
                        Log.w(TAG, "Unable to unregister torch callback", e)
                    }
                    torchCallbackRegistered = false
                }
                fallHandler?.stop()
                stateThread.quitSafely()
            }
        }
        mainHandler.removeCallbacksAndMessages(null)
        activeDialog?.dismiss()
        activeDialog = null
        super.onDestroy()
    }

    private fun connectServices() {
        if (destroyed) return

        if (motor == null && !connectMotor()) {
            scheduleReconnect()
            return
        }

        if (!calibrationReady) {
            val service = motor ?: run {
                scheduleReconnect()
                return
            }
            try {
                service.initializeCalibration()
                calibrationReady = true
                settledDirection = when (service.getPosition()) {
                    IMotor.POSITION_UP -> IMotor.DIRECTION_UP
                    IMotor.POSITION_DOWN -> IMotor.DIRECTION_DOWN
                    else -> DIRECTION_UNKNOWN
                }
                commandedDirection = settledDirection
                movingDirection = DIRECTION_UNKNOWN
                faultDirection = DIRECTION_UNKNOWN
                suppressedAbnormalUntilMillis.clear()
                Log.i(TAG, "Motor calibration initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Unable to initialize motor calibration", e)
                unlinkMotorDeathRecipient()
                motor = null
                calibrationReady = false
                commandedDirection = DIRECTION_UNKNOWN
                movingDirection = DIRECTION_UNKNOWN
                settledDirection = DIRECTION_UNKNOWN
                faultDirection = DIRECTION_UNKNOWN
                clearRestartRequest()
                suppressedAbnormalUntilMillis.clear()
                staleMoveEventGuards.clear()
                scheduleReconnect()
                return
            }
        }

        startInputObservers()
        if (signal == null && !connectSignal()) {
            scheduleReconnect()
        }
        reconcileMotorState()
    }

    private fun connectMotor(): Boolean {
        val binder = ServiceManager.checkService(MOTOR_SERVICE) ?: return false
        val service = IMotor.Stub.asInterface(binder) ?: return false
        return try {
            binder.linkToDeath(motorDeathRecipient, 0)
            motor = service
            true
        } catch (e: Exception) {
            Log.w(TAG, "Motor HAL disappeared while connecting", e)
            motor = null
            false
        }
    }

    private fun connectSignal(): Boolean {
        return try {
            val service = ICameraSignal.getService(SIGNAL_INSTANCE, false) ?: return false
            if (!service.asBinder().linkToDeath(signalDeathRecipient, SIGNAL_DEATH_COOKIE)) {
                return false
            }
            service.registerListener(signalListener)
            signal = service
            Log.i(TAG, "Registered with camera signal broker")
            true
        } catch (e: Exception) {
            Log.w(TAG, "Camera signal broker disappeared while connecting", e)
            signal = null
            false
        }
    }

    private fun scheduleReconnect() {
        if (destroyed) return
        stateHandler.removeCallbacks(reconnectRunnable)
        stateHandler.postDelayed(reconnectRunnable, RECONNECT_DELAY_MILLIS)
    }

    private fun startInputObservers() {
        if (destroyed) return
        stateHandler.removeCallbacks(inputRetryRunnable)
        var retryNeeded = false

        if (!torchCallbackRegistered) {
            try {
                cameraManager.registerTorchCallback(torchCallback, stateHandler)
                torchCallbackRegistered = true
            } catch (e: RuntimeException) {
                retryNeeded = true
                Log.e(TAG, "Unable to register torch callback; retrying", e)
            }
        }

        val detector = fallHandler ?: FallHandler(sensorManager, stateHandler, ::handleFall).also {
            fallHandler = it
        }
        if (!detector.start()) retryNeeded = true

        if (retryNeeded) {
            stateHandler.postDelayed(inputRetryRunnable, INPUT_RETRY_DELAY_MILLIS)
        }
    }

    private fun handleCameraState(
        cameraId: Int,
        state: Int,
        sessionId: Long,
        sequence: Long,
        configurationId: Int,
    ) {
        val latestSession = latestSessionId[cameraId]
        if (latestSession != null && sessionId < latestSession) return
        if (latestSession == null || sessionId > latestSession) {
            latestSessionId[cameraId] = sessionId
            sessions.keys.removeAll { it.cameraId == cameraId && it.sessionId != sessionId }
            lastSequence.keys.removeAll { it.cameraId == cameraId && it.sessionId != sessionId }
            lastConfigurationId.keys.removeAll {
                it.cameraId == cameraId && it.sessionId != sessionId
            }
        }
        val key = SessionKey(cameraId, sessionId)
        val previousSequence = lastSequence[key] ?: Long.MIN_VALUE
        if (sequence <= previousSequence) return
        lastSequence[key] = sequence

        val previousState = sessions[key]
        val previousConfigurationId = lastConfigurationId[key]
        val replayedAfterBrokerRestart = cameraId in cameraIdsAwaitingReplay

        cameraIdsAwaitingReplay.remove(cameraId)
        if (cameraIdsAwaitingReplay.isEmpty()) {
            stateHandler.removeCallbacks(signalReplayTimeoutRunnable)
        }

        if (state == CameraState.IDLE) {
            sessions.keys.removeAll { it.cameraId == cameraId }
            lastConfigurationId.keys.removeAll { it.cameraId == cameraId }
        } else {
            sessions[key] = state
            lastConfigurationId[key] = configurationId
        }

        if (state != CameraState.IDLE) cancelPendingDown()
        if (!replayedAfterBrokerRestart &&
            requiresRestart(previousState, state, previousConfigurationId, configurationId)
        ) {
            restartAfterDown = true
            stateHandler.removeCallbacks(restartTimeoutRunnable)
            stateHandler.postDelayed(restartTimeoutRunnable, RESTART_TIMEOUT_MILLIS)
        }
        reconcileMotorState()
    }

    private fun requiresRestart(
        previousState: Int?,
        state: Int,
        previousConfigurationId: Int?,
        configurationId: Int,
    ): Boolean {
        val videoToFlash = previousState == CameraState.REAR_VIDEO && isRearFlashState(state)
        val flashToVideo = isRearFlashState(previousState) && state == CameraState.REAR_VIDEO
        val videoConfigurationChanged = previousState == CameraState.REAR_VIDEO &&
            state == CameraState.REAR_VIDEO &&
            previousConfigurationId != configurationId
        return videoToFlash || flashToVideo || videoConfigurationChanged
    }

    private fun isRearFlashState(state: Int?): Boolean =
        state == CameraState.REAR_AUTO_FLASH || state == CameraState.REAR_ALWAYS_FLASH

    private fun reconcileMotorState() {
        if (destroyed) return
        if (!calibrationReady || motor == null) return

        maybeReleaseSafetyLatch()
        if (safetyReason != SafetyReason.NONE) {
            clearRestartRequest()
            cancelPendingDown()
            pendingTorchRestoreIds = emptySet()
            requestMove(IMotor.DIRECTION_DOWN, IMotor.START_FORCE)
            return
        }

        if (restartAfterDown) {
            cancelPendingDown()
            val fullyDown = settledDirection == IMotor.DIRECTION_DOWN &&
                movingDirection == DIRECTION_UNKNOWN
            if (fullyDown) {
                clearRestartRequest()
                if (hasRaiseDemand()) {
                    requestMove(IMotor.DIRECTION_UP, IMotor.START_NORMAL)
                }
            } else {
                requestMove(IMotor.DIRECTION_DOWN, IMotor.START_NORMAL)
            }
            return
        }

        if (hasRaiseDemand()) {
            cancelPendingDown()
            requestMove(IMotor.DIRECTION_UP, IMotor.START_NORMAL)
        } else {
            scheduleDown()
        }
    }

    private fun hasRaiseDemand(): Boolean {
        val cameraNeedsMotor = sessions.values.any { state ->
            state == CameraState.FRONT_CAMERA ||
                state == CameraState.REAR_AUTO_FLASH ||
                state == CameraState.REAR_ALWAYS_FLASH ||
                state == CameraState.REAR_VIDEO
        }
        return cameraNeedsMotor || torchStates.values.any { it } || pendingTorchRestoreIds.isNotEmpty()
    }

    private fun clearRestartRequest() {
        restartAfterDown = false
        stateHandler.removeCallbacks(restartTimeoutRunnable)
    }

    private fun scheduleDown() {
        val alreadyGoingDown = commandedDirection == IMotor.DIRECTION_DOWN &&
            (movingDirection == IMotor.DIRECTION_DOWN ||
                settledDirection == IMotor.DIRECTION_DOWN)
        if (downPending || alreadyGoingDown) return
        downPending = true
        stateHandler.postDelayed(downRunnable, DOWN_DEBOUNCE_MILLIS)
    }

    private fun cancelPendingDown() {
        if (!downPending) return
        stateHandler.removeCallbacks(downRunnable)
        downPending = false
    }

    private fun requestMove(direction: Int, startMode: Int, force: Boolean = false) {
        if (destroyed) return
        if (!calibrationReady) return
        if (!force && faultDirection == direction) return
        val alreadyFollowingCommand = commandedDirection == direction &&
            (movingDirection == direction || settledDirection == direction)
        if (!force && alreadyFollowingCommand) {
            if (direction == IMotor.DIRECTION_UP) restorePendingTorchIfMotorUp()
            return
        }

        val service = motor ?: run {
            scheduleReconnect()
            return
        }
        val interruptedDirection = when {
            movingDirection != DIRECTION_UNKNOWN && movingDirection != direction -> movingDirection
            commandedDirection != DIRECTION_UNKNOWN &&
                commandedDirection != direction &&
                commandedDirection != settledDirection -> commandedDirection
            else -> null
        }
        val previousDirection = when {
            movingDirection != DIRECTION_UNKNOWN && movingDirection != direction -> movingDirection
            commandedDirection != DIRECTION_UNKNOWN && commandedDirection != direction ->
                commandedDirection
            settledDirection != DIRECTION_UNKNOWN && settledDirection != direction -> settledDirection
            else -> null
        }
        try {
            service.move(direction, startMode)
            suppressedAbnormalUntilMillis.remove(direction)
            staleMoveEventGuards.remove(direction)
            if (interruptedDirection != null) {
                suppressedAbnormalUntilMillis[interruptedDirection] =
                    SystemClock.elapsedRealtime() + ABNORMAL_SUPPRESSION_MILLIS
            }
            if (previousDirection != null) suppressStaleMoveEvents(previousDirection)
            commandedDirection = direction
            syncMotorStateAfterMove(service, direction)
            Log.i(TAG, "Motor request direction=$direction mode=$startMode")
        } catch (e: Exception) {
            Log.e(TAG, "Motor request failed", e)
            unlinkMotorDeathRecipient()
            motor = null
            calibrationReady = false
            commandedDirection = DIRECTION_UNKNOWN
            movingDirection = DIRECTION_UNKNOWN
            settledDirection = DIRECTION_UNKNOWN
            faultDirection = DIRECTION_UNKNOWN
            clearRestartRequest()
            suppressedAbnormalUntilMillis.clear()
            staleMoveEventGuards.clear()
            scheduleReconnect()
        }
    }

    private fun syncMotorStateAfterMove(service: IMotor, requestedDirection: Int) {
        movingDirection = when (service.getMoveState()) {
            MOTOR_MOVE_STATE_UP -> IMotor.DIRECTION_UP
            MOTOR_MOVE_STATE_DOWN -> IMotor.DIRECTION_DOWN
            else -> DIRECTION_UNKNOWN
        }
        if (movingDirection != DIRECTION_UNKNOWN) {
            settledDirection = DIRECTION_UNKNOWN
            return
        }

        settledDirection = when (service.getPosition()) {
            IMotor.POSITION_UP -> IMotor.DIRECTION_UP
            IMotor.POSITION_DOWN -> IMotor.DIRECTION_DOWN
            else -> DIRECTION_UNKNOWN
        }
        if (requestedDirection == IMotor.DIRECTION_UP) restorePendingTorchIfMotorUp()
    }

    private fun restorePendingTorchIfMotorUp() {
        if (safetyReason != SafetyReason.NONE ||
            settledDirection != IMotor.DIRECTION_UP ||
            pendingTorchRestoreIds.isEmpty()
        ) {
            return
        }

        val restoreIds = pendingTorchRestoreIds
        pendingTorchRestoreIds = emptySet()
        restoreTorch(restoreIds)
    }

    private fun handleFall() {
        if (destroyed) return
        if (safetyReason != SafetyReason.NONE) {
            val releaseWasPending = safetyReleaseRequested
            engageSafetyLatch(SafetyReason.FALL, enabledTorchIds())
            disableTorch()
            if (releaseWasPending) showFallDialog()
            return
        }

        val position = try {
            motor?.getPosition() ?: IMotor.POSITION_UNKNOWN
        } catch (e: Exception) {
            IMotor.POSITION_UNKNOWN
        }
        val motorExposed = hasRaiseDemand() ||
            commandedDirection == IMotor.DIRECTION_UP ||
            position != IMotor.POSITION_DOWN
        if (!motorExposed) return

        val enabledTorchIds = enabledTorchIds()
        engageSafetyLatch(SafetyReason.FALL, enabledTorchIds)
        disableTorch()
        showFallDialog()
    }

    private fun engageSafetyLatch(
        reason: SafetyReason,
        restoreTorchIds: Set<String> = emptySet(),
    ) {
        val resetForNewFall = reason == SafetyReason.FALL && safetyReleaseRequested
        if (safetyReason == SafetyReason.NONE || resetForNewFall) {
            safetyReason = reason
            safetyLatchedAtMillis = SystemClock.elapsedRealtime()
            safetyReleaseRequested = false
            raiseAfterSafetyRelease = false
            safetyTorchIds = safetyTorchIds + restoreTorchIds
        }
        faultDirection = DIRECTION_UNKNOWN
        pendingTorchRestoreIds = emptySet()
        cancelPendingDown()
        stateHandler.removeCallbacks(safetyReleaseRunnable)
        val elapsed = SystemClock.elapsedRealtime() - safetyLatchedAtMillis
        val releaseDelay = (SAFETY_MINIMUM_LATCH_MILLIS - elapsed).coerceAtLeast(0L)
        stateHandler.postDelayed(safetyReleaseRunnable, releaseDelay)
        requestMove(
            IMotor.DIRECTION_DOWN,
            IMotor.START_FORCE,
            force = true,
        )
    }

    private fun requestSafetyRelease() {
        if (safetyReason == SafetyReason.NONE) return
        safetyReleaseRequested = true
        raiseAfterSafetyRelease = true
        reconcileMotorState()
    }

    private fun requestSafetyDismiss() {
        if (safetyReason == SafetyReason.NONE) return
        safetyReleaseRequested = true
        raiseAfterSafetyRelease = false
        reconcileMotorState()
    }

    private fun maybeReleaseSafetyLatch() {
        if (safetyReason == SafetyReason.NONE) return
        if (!safetyReleaseRequested) {
            if (safetyReason == SafetyReason.FALL || hasRaiseDemand()) return
        } else if (!raiseAfterSafetyRelease && hasRaiseDemand()) {
            return
        }

        val elapsed = SystemClock.elapsedRealtime() - safetyLatchedAtMillis
        val remaining = SAFETY_MINIMUM_LATCH_MILLIS - elapsed
        if (remaining > 0L) {
            stateHandler.removeCallbacks(safetyReleaseRunnable)
            stateHandler.postDelayed(safetyReleaseRunnable, remaining)
            return
        }

        Log.i(TAG, "Clearing ${safetyReason.name.lowercase()} safety latch")
        val shouldRaise = raiseAfterSafetyRelease
        val torchIdsToRestore = safetyTorchIds
        safetyReason = SafetyReason.NONE
        safetyReleaseRequested = false
        raiseAfterSafetyRelease = false
        safetyTorchIds = emptySet()
        stateHandler.removeCallbacks(safetyReleaseRunnable)
        if (shouldRaise) {
            pendingTorchRestoreIds = pendingTorchRestoreIds + torchIdsToRestore
            requestMove(IMotor.DIRECTION_UP, IMotor.START_NORMAL, force = true)
        }
    }

    private fun handleMotorEvent(scanCode: Int, eventTimeNanos: Long) {
        if (destroyed) return
        val eventAgeNanos = SystemClock.uptimeMillis() * NANOS_PER_MILLISECOND - eventTimeNanos
        if (eventAgeNanos < -MOTOR_EVENT_MAX_FUTURE_NANOS) {
            Log.w(TAG, "Ignoring future motor event $scanCode ageNanos=$eventAgeNanos")
            return
        }
        if (eventAgeNanos > MOTOR_EVENT_MAX_AGE_NANOS) {
            Log.w(TAG, "Ignoring expired motor event $scanCode ageNanos=$eventAgeNanos")
            return
        }
        val previous = lastMotorEventByCode[scanCode] ?: Long.MIN_VALUE
        if (eventTimeNanos <= previous) return
        lastMotorEventByCode[scanCode] = eventTimeNanos
        val eventDirection = when (scanCode) {
            MOTOR_EVENT_UP,
            MOTOR_EVENT_UP_ABNORMAL,
            MOTOR_EVENT_UP_NORMAL -> IMotor.DIRECTION_UP
            MOTOR_EVENT_DOWN,
            MOTOR_EVENT_DOWN_ABNORMAL,
            MOTOR_EVENT_DOWN_NORMAL -> IMotor.DIRECTION_DOWN
            else -> DIRECTION_UNKNOWN
        }
        if (eventDirection != DIRECTION_UNKNOWN &&
            shouldSuppressStaleMoveEvent(eventDirection, eventTimeNanos)
        ) {
            Log.i(TAG, "Ignoring stale motor event $scanCode for direction=$eventDirection")
            return
        }

        when (scanCode) {
            MOTOR_EVENT_MANUAL_TO_UP -> {
                faultDirection = DIRECTION_UNKNOWN
                commandedDirection = IMotor.DIRECTION_UP
                movingDirection = IMotor.DIRECTION_UP
                settledDirection = DIRECTION_UNKNOWN
                reconcileMotorState()
            }
            MOTOR_EVENT_MANUAL_TO_DOWN -> {
                faultDirection = DIRECTION_UNKNOWN
                commandedDirection = IMotor.DIRECTION_DOWN
                movingDirection = IMotor.DIRECTION_DOWN
                settledDirection = DIRECTION_UNKNOWN
                engageSafetyLatch(SafetyReason.MANUAL_PRESS)
                disableTorch()
                goHome()
                showManualPressDialog()
            }
            MOTOR_EVENT_UP -> handleMoveStarted(IMotor.DIRECTION_UP)
            MOTOR_EVENT_UP_NORMAL -> handleMoveCompleted(IMotor.DIRECTION_UP)
            MOTOR_EVENT_DOWN -> handleMoveStarted(IMotor.DIRECTION_DOWN)
            MOTOR_EVENT_DOWN_NORMAL -> handleMoveCompleted(IMotor.DIRECTION_DOWN)
            MOTOR_EVENT_UP_ABNORMAL -> handleMoveAbnormal(IMotor.DIRECTION_UP)
            MOTOR_EVENT_DOWN_ABNORMAL -> handleMoveAbnormal(IMotor.DIRECTION_DOWN)
        }
    }

    private fun handleMoveStarted(direction: Int) {
        if (direction == IMotor.DIRECTION_UP && commandedDirection != direction) {
            Log.i(TAG, "Ignoring unsolicited upward motor event")
            return
        }
        val unsolicitedSafetyRetraction = direction == IMotor.DIRECTION_DOWN &&
            commandedDirection != IMotor.DIRECTION_DOWN &&
            suppressedAbnormalUntilMillis[IMotor.DIRECTION_DOWN]
                ?.let { SystemClock.elapsedRealtime() <= it } != true &&
            safetyReason == SafetyReason.NONE
        commandedDirection = direction
        movingDirection = direction
        settledDirection = DIRECTION_UNKNOWN
        if (unsolicitedSafetyRetraction) {
            Log.w(TAG, "Unsolicited motor retraction; treating it as a safety event")
            engageSafetyLatch(SafetyReason.FALL, enabledTorchIds())
            disableTorch()
            showFallDialog()
            return
        }
        reconcileMotorState()
    }

    private fun handleMoveCompleted(direction: Int) {
        if (commandedDirection != direction) {
            Log.i(TAG, "Ignoring completion for non-commanded direction=$direction")
            return
        }
        val expectedPosition = if (direction == IMotor.DIRECTION_UP) {
            IMotor.POSITION_UP
        } else {
            IMotor.POSITION_DOWN
        }
        val actualPosition = try {
            motor?.getPosition() ?: IMotor.POSITION_UNKNOWN
        } catch (e: Exception) {
            Log.w(TAG, "Unable to verify motor completion direction=$direction", e)
            IMotor.POSITION_UNKNOWN
        }
        if (actualPosition != expectedPosition) {
            Log.i(
                TAG,
                "Ignoring completion for direction=$direction at position=$actualPosition",
            )
            return
        }
        commandedDirection = direction
        if (movingDirection == direction) movingDirection = DIRECTION_UNKNOWN
        settledDirection = direction
        if (faultDirection == direction) faultDirection = DIRECTION_UNKNOWN
        suppressedAbnormalUntilMillis.remove(direction)

        if (direction == IMotor.DIRECTION_UP) {
            restorePendingTorchIfMotorUp()
        }
        reconcileMotorState()
    }

    private fun handleMoveAbnormal(direction: Int) {
        if (consumeSuppressedAbnormal(direction)) {
            Log.i(TAG, "Ignoring expected abnormal event for interrupted direction=$direction")
            if (movingDirection == direction) movingDirection = DIRECTION_UNKNOWN
            if (settledDirection == direction) settledDirection = DIRECTION_UNKNOWN
            reconcileMotorState()
            return
        }
        if (commandedDirection != direction && movingDirection != direction) {
            Log.i(TAG, "Ignoring abnormal event for non-commanded direction=$direction")
            return
        }

        if (movingDirection == direction) movingDirection = DIRECTION_UNKNOWN
        settledDirection = DIRECTION_UNKNOWN
        commandedDirection = DIRECTION_UNKNOWN
        faultDirection = direction
        clearRestartRequest()
        if (direction == IMotor.DIRECTION_UP) {
            val torchIdsToRestore = pendingTorchRestoreIds + enabledTorchIds()
            pendingTorchRestoreIds = emptySet()
            disableTorch()
            showMotorCannotGoUpDialog(torchIdsToRestore)
        } else {
            disableTorch()
            showMotorCannotGoDownDialog()
        }
    }

    private fun consumeSuppressedAbnormal(direction: Int): Boolean {
        val deadline = suppressedAbnormalUntilMillis.remove(direction) ?: return false
        return SystemClock.elapsedRealtime() <= deadline
    }

    private fun suppressStaleMoveEvents(direction: Int) {
        staleMoveEventGuards[direction] = StaleMoveEventGuard(
            SystemClock.uptimeMillis() * NANOS_PER_MILLISECOND,
            SystemClock.elapsedRealtime() + STALE_MOVE_EVENT_SUPPRESSION_MILLIS,
        )
    }

    private fun shouldSuppressStaleMoveEvent(direction: Int, eventTimeNanos: Long): Boolean {
        val guard = staleMoveEventGuards[direction] ?: return false
        if (SystemClock.elapsedRealtime() > guard.expiresAtMillis) {
            staleMoveEventGuards.remove(direction)
            return false
        }
        if (commandedDirection == direction || movingDirection == direction) {
            staleMoveEventGuards.remove(direction)
            return false
        }
        if (eventTimeNanos > guard.cutoffEventTimeNanos) {
            staleMoveEventGuards.remove(direction)
            return false
        }
        return true
    }

    private fun enabledTorchIds(): Set<String> =
        torchStates.filterValues { it }.keys.toSet()

    private fun disableTorch(cameraIds: Set<String> = enabledTorchIds()) {
        for (cameraId in cameraIds) {
            try {
                cameraManager.setTorchMode(cameraId, false)
                torchStates[cameraId] = false
                pendingTorchDisableIds.remove(cameraId)
            } catch (e: CameraAccessException) {
                Log.w(TAG, "Unable to disable torch $cameraId", e)
                torchStates[cameraId] = true
                pendingTorchDisableIds.add(cameraId)
            } catch (e: SecurityException) {
                Log.w(TAG, "Not permitted to disable torch $cameraId", e)
                torchStates[cameraId] = true
                pendingTorchDisableIds.add(cameraId)
            }
        }
        if (pendingTorchDisableIds.isEmpty()) {
            cancelTorchDisableRetryIfIdle()
        } else {
            scheduleTorchDisableRetry()
        }
    }

    private fun restoreTorch(cameraIds: Set<String>) {
        for (cameraId in cameraIds) {
            pendingTorchDisableIds.remove(cameraId)
            try {
                cameraManager.setTorchMode(cameraId, true)
                torchStates[cameraId] = true
            } catch (e: CameraAccessException) {
                Log.w(TAG, "Unable to restore torch $cameraId", e)
            } catch (e: SecurityException) {
                Log.w(TAG, "Not permitted to restore torch $cameraId", e)
            }
        }
        cancelTorchDisableRetryIfIdle()
    }

    private fun scheduleTorchDisableRetry() {
        if (destroyed) return
        stateHandler.removeCallbacks(torchDisableRetryRunnable)
        stateHandler.postDelayed(torchDisableRetryRunnable, TORCH_DISABLE_RETRY_MILLIS)
    }

    private fun cancelTorchDisableRetryIfIdle() {
        if (pendingTorchDisableIds.isEmpty()) {
            stateHandler.removeCallbacks(torchDisableRetryRunnable)
        }
    }

    private fun showFallDialog() {
        showDialog(replaceExisting = true) {
            AlertDialog.Builder(this)
                .setTitle(R.string.free_fall_detected_title)
                .setMessage(R.string.free_fall_detected_message)
                .setNegativeButton(R.string.raise_the_camera) { _, _ ->
                    stateHandler.post { requestSafetyRelease() }
                }
                .setPositiveButton(R.string.close) { _, _ ->
                    goHome()
                    stateHandler.post { requestSafetyDismiss() }
                }
                .create()
        }
    }

    private fun showMotorCannotGoUpDialog(torchIdsToRestore: Set<String>) {
        showDialog {
            AlertDialog.Builder(this)
                .setTitle(R.string.warning)
                .setMessage(R.string.motor_cannot_go_up_message)
                .setNegativeButton(R.string.retry) { _, _ ->
                    stateHandler.post {
                        if (safetyReason == SafetyReason.NONE) {
                            faultDirection = DIRECTION_UNKNOWN
                            pendingTorchRestoreIds = pendingTorchRestoreIds + torchIdsToRestore
                            requestMove(IMotor.DIRECTION_UP, IMotor.START_FORCE, force = true)
                        }
                    }
                }
                .setPositiveButton(R.string.close) { _, _ ->
                    stateHandler.post { engageSafetyLatch(SafetyReason.USER_CLOSED) }
                    goHome()
                }
                .create()
        }
    }

    private fun showMotorCannotGoDownDialog() {
        showDialog {
            AlertDialog.Builder(this)
                .setTitle(R.string.warning)
                .setMessage(R.string.motor_cannot_go_down_message)
                .setPositiveButton(R.string.retry) { _, _ ->
                    stateHandler.post {
                        faultDirection = DIRECTION_UNKNOWN
                        requestMove(IMotor.DIRECTION_DOWN, IMotor.START_FORCE, force = true)
                    }
                }
                .create()
        }
    }

    private fun showManualPressDialog() {
        showDialog {
            AlertDialog.Builder(this)
                .setTitle(R.string.warning)
                .setMessage(R.string.motor_press_message)
                .setPositiveButton(android.R.string.ok, null)
                .create()
        }
    }

    private fun showDialog(
        replaceExisting: Boolean = false,
        createDialog: () -> AlertDialog,
    ) {
        if (destroyed) return
        mainHandler.post {
            if (destroyed) return@post
            if (activeDialog?.isShowing == true) {
                if (!replaceExisting) return@post
                activeDialog?.dismiss()
            }

            val dialog = createDialog()
            activeDialog = dialog
            dialog.setCancelable(false)
            dialog.setCanceledOnTouchOutside(false)
            dialog.setOnDismissListener {
                if (activeDialog === dialog) activeDialog = null
            }
            dialog.window?.setType(WindowManager.LayoutParams.TYPE_SYSTEM_ALERT)
            dialog.show()
        }
    }

    private fun goHome() {
        if (destroyed) return
        mainHandler.post {
            if (destroyed) return@post
            val intent = Intent(Intent.ACTION_MAIN).apply {
                addCategory(Intent.CATEGORY_HOME)
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            startActivity(intent)
        }
    }

    private fun findTorchCameraIds(): Set<String> {
        return try {
            cameraManager.cameraIdList.filterTo(mutableSetOf()) { cameraId ->
                val characteristics = cameraManager.getCameraCharacteristics(cameraId)
                characteristics.get(CameraCharacteristics.LENS_FACING) ==
                    CameraCharacteristics.LENS_FACING_BACK &&
                    characteristics.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
            }.ifEmpty { setOf(DEFAULT_TORCH_CAMERA_ID) }
        } catch (e: CameraAccessException) {
            Log.w(TAG, "Unable to enumerate torch cameras", e)
            setOf(DEFAULT_TORCH_CAMERA_ID)
        } catch (e: SecurityException) {
            Log.w(TAG, "Not permitted to enumerate torch cameras", e)
            setOf(DEFAULT_TORCH_CAMERA_ID)
        }
    }

    private fun isMotorTorch(cameraId: String): Boolean {
        return cameraId in torchCameraIds
    }

    private fun unlinkMotorDeathRecipient() {
        motor?.asBinder()?.let { binder ->
            runCatching { binder.unlinkToDeath(motorDeathRecipient, 0) }
        }
    }

    private fun unlinkSignalDeathRecipient() {
        signal?.asBinder()?.let { binder ->
            runCatching { binder.unlinkToDeath(signalDeathRecipient) }
        }
    }

    private data class SessionKey(val cameraId: Int, val sessionId: Long)

    private data class StaleMoveEventGuard(
        val cutoffEventTimeNanos: Long,
        val expiresAtMillis: Long,
    )

    private enum class SafetyReason {
        NONE,
        FALL,
        MANUAL_PRESS,
        USER_CLOSED,
    }

    private companion object {
        const val TAG = "OPlusCameraHelper"
        const val DEFAULT_TORCH_CAMERA_ID = "0"
        const val DIRECTION_UNKNOWN = -1
        const val NANOS_PER_MILLISECOND = 1_000_000L
        const val MOTOR_EVENT_MAX_AGE_NANOS = 2_500L * NANOS_PER_MILLISECOND
        const val MOTOR_EVENT_MAX_FUTURE_NANOS = 250L * NANOS_PER_MILLISECOND
        const val RECONNECT_DELAY_MILLIS = 1_000L
        const val SIGNAL_REPLAY_TIMEOUT_MILLIS = 20_000L
        const val INPUT_RETRY_DELAY_MILLIS = 5_000L
        const val DOWN_DEBOUNCE_MILLIS = 2_000L
        const val RESTART_TIMEOUT_MILLIS = 5_000L
        const val STALE_MOVE_EVENT_SUPPRESSION_MILLIS = 2_500L
        const val TORCH_DISABLE_RETRY_MILLIS = 1_000L
        const val SAFETY_MINIMUM_LATCH_MILLIS = 2_500L
        const val ABNORMAL_SUPPRESSION_MILLIS = 10_000L

        const val MOTOR_MOVE_STATE_UP = 1
        const val MOTOR_MOVE_STATE_DOWN = 2

        const val SIGNAL_INSTANCE = "default"
        const val SIGNAL_DEATH_COOKIE = 1L
        val MOTOR_SERVICE = "${IMotor.DESCRIPTOR}/default"

        const val MOTOR_EVENT_MANUAL_TO_UP = 183
        const val MOTOR_EVENT_MANUAL_TO_DOWN = 184
        const val MOTOR_EVENT_UP = 185
        const val MOTOR_EVENT_UP_ABNORMAL = 186
        const val MOTOR_EVENT_UP_NORMAL = 187
        const val MOTOR_EVENT_DOWN = 188
        const val MOTOR_EVENT_DOWN_ABNORMAL = 189
        const val MOTOR_EVENT_DOWN_NORMAL = 190
    }
}
