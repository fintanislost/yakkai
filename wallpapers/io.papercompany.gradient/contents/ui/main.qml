/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root

    property color startColor: configuration.StartColor ?? "#0b1f33"
    property color endColor: configuration.EndColor ?? "#4d7cff"
    property int baseAngle: configuration.Angle ?? 125
    property bool animate: configuration.Animate ?? true
    property int animationDuration: configuration.AnimationDuration ?? 30
    property int driftDegrees: configuration.DriftDegrees ?? 10
    property int vignetteStrength: configuration.VignetteStrength ?? 18
    property real animationPhase: 0
    readonly property real effectiveAngle: baseAngle + (animate ? Math.sin(animationPhase * Math.PI * 2) * driftDegrees : 0)

    function repaint() {
        canvas.requestPaint()
        root.accentColorChanged()
    }

    onStartColorChanged: repaint()
    onEndColorChanged: repaint()
    onBaseAngleChanged: repaint()
    onDriftDegreesChanged: repaint()
    onVignetteStrengthChanged: repaint()
    onAnimationPhaseChanged: canvas.requestPaint()
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

            gradient.addColorStop(0, root.startColor)
            gradient.addColorStop(1, root.endColor)

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
