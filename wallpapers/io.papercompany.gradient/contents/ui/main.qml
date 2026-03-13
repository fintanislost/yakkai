/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root

    property color manualStartColor: configuration.StartColor ?? "#0b1f33"
    property color manualEndColor: configuration.EndColor ?? "#4d7cff"
    property int baseAngle: configuration.Angle ?? 125
    property bool animate: configuration.Animate ?? true
    property int animationDuration: configuration.AnimationDuration ?? 30
    property int driftDegrees: configuration.DriftDegrees ?? 10
    property int vignetteStrength: configuration.VignetteStrength ?? 18
    property bool useTimeOfDay: configuration.UseTimeOfDay ?? false
    property int dayStartHour: configuration.DayStartHour ?? 7
    property int nightStartHour: configuration.NightStartHour ?? 19
    property int transitionMinutes: configuration.TransitionMinutes ?? 60
    property color dayStartColor: configuration.DayStartColor ?? "#87b8ff"
    property color dayEndColor: configuration.DayEndColor ?? "#f6d365"
    property color nightStartColor: configuration.NightStartColor ?? "#0b1f33"
    property color nightEndColor: configuration.NightEndColor ?? "#4d7cff"
    property real animationPhase: 0
    property int clockTick: 0
    readonly property real effectiveAngle: baseAngle + (animate ? Math.sin(animationPhase * Math.PI * 2) * driftDegrees : 0)
    readonly property real dayFactor: {
        clockTick
        return useTimeOfDay ? currentDayFactor(new Date()) : 1
    }
    readonly property color effectiveStartColor: useTimeOfDay ? mixColors(nightStartColor, dayStartColor, dayFactor) : manualStartColor
    readonly property color effectiveEndColor: useTimeOfDay ? mixColors(nightEndColor, dayEndColor, dayFactor) : manualEndColor

    function normalizedMinutes(value) {
        const fullDay = 24 * 60

        return ((value % fullDay) + fullDay) % fullDay
    }

    function progressWithinArc(value, start, duration) {
        if (duration <= 0) {
            return -1
        }

        const offset = normalizedMinutes(value - start)

        return offset <= duration ? offset / duration : -1
    }

    function isWithinArc(value, start, end) {
        const offset = normalizedMinutes(value - start)
        const span = normalizedMinutes(end - start)

        return offset < span
    }

    function mixColors(first, second, factor) {
        const t = Math.max(0, Math.min(1, factor))

        return Qt.rgba(
            first.r + (second.r - first.r) * t,
            first.g + (second.g - first.g) * t,
            first.b + (second.b - first.b) * t,
            first.a + (second.a - first.a) * t
        )
    }

    function currentDayFactor(now) {
        const minutes = now.getHours() * 60 + now.getMinutes() + now.getSeconds() / 60
        const dayStartMinutes = normalizedMinutes(dayStartHour * 60)
        const nightStartMinutes = normalizedMinutes(nightStartHour * 60)
        const blendMinutes = Math.max(0, transitionMinutes)
        const dayTransition = progressWithinArc(minutes, dayStartMinutes, blendMinutes)

        if (blendMinutes === 0) {
            return isWithinArc(minutes, dayStartMinutes, nightStartMinutes) ? 1 : 0
        }

        if (dayTransition >= 0) {
            return dayTransition
        }

        const nightTransition = progressWithinArc(minutes, nightStartMinutes, blendMinutes)
        if (nightTransition >= 0) {
            return 1 - nightTransition
        }

        return isWithinArc(minutes, normalizedMinutes(dayStartMinutes + blendMinutes), nightStartMinutes) ? 1 : 0
    }

    function repaint() {
        canvas.requestPaint()
        root.accentColorChanged()
    }

    onManualStartColorChanged: repaint()
    onManualEndColorChanged: repaint()
    onBaseAngleChanged: repaint()
    onDriftDegreesChanged: repaint()
    onVignetteStrengthChanged: repaint()
    onAnimationPhaseChanged: canvas.requestPaint()
    onUseTimeOfDayChanged: repaint()
    onDayStartHourChanged: repaint()
    onNightStartHourChanged: repaint()
    onTransitionMinutesChanged: repaint()
    onDayStartColorChanged: repaint()
    onDayEndColorChanged: repaint()
    onNightStartColorChanged: repaint()
    onNightEndColorChanged: repaint()
    onEffectiveStartColorChanged: repaint()
    onEffectiveEndColorChanged: repaint()
    onAnimateChanged: {
        if (!animate) {
            animationPhase = 0
        }
        repaint()
    }

    NumberAnimation on animationPhase {
        from: 0
        to: 1
        duration: Math.max(5, root.animationDuration) * 1000
        loops: Animation.Infinite
        running: root.animate
    }

    Timer {
        interval: 30000
        repeat: true
        running: root.useTimeOfDay
        triggeredOnStart: true
        onTriggered: root.clockTick += 1
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        Component.onCompleted: requestPaint()

        onPaint: {
            const context = getContext("2d")
            const centerX = width / 2
            const centerY = height / 2
            const radians = root.effectiveAngle * Math.PI / 180
            const halfDiagonal = Math.sqrt(width * width + height * height) / 2
            const offsetX = Math.cos(radians) * halfDiagonal
            const offsetY = Math.sin(radians) * halfDiagonal
            const gradient = context.createLinearGradient(
                centerX - offsetX,
                centerY - offsetY,
                centerX + offsetX,
                centerY + offsetY
            )

            gradient.addColorStop(0, root.effectiveStartColor)
            gradient.addColorStop(1, root.effectiveEndColor)

            context.clearRect(0, 0, width, height)
            context.fillStyle = gradient
            context.fillRect(0, 0, width, height)

            if (root.vignetteStrength > 0) {
                const vignette = context.createRadialGradient(
                    centerX,
                    centerY,
                    Math.min(width, height) * 0.1,
                    centerX,
                    centerY,
                    halfDiagonal
                )
                const vignetteOpacity = Math.min(0.8, root.vignetteStrength / 100)

                vignette.addColorStop(0, "rgba(0, 0, 0, 0.0)")
                vignette.addColorStop(0.65, "rgba(0, 0, 0, 0.0)")
                vignette.addColorStop(1, "rgba(0, 0, 0, " + vignetteOpacity + ")")

                context.fillStyle = vignette
                context.fillRect(0, 0, width, height)
            }
        }
    }
}
