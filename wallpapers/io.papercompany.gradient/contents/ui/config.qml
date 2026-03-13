/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols as KQuickControls

Kirigami.FormLayout {
    id: root
    twinFormLayouts: parentLayout

    property var presets: [
        { name: qsTr("Custom") },
        { name: qsTr("Deep Sea"), startColor: "#041c32", endColor: "#2d68c4", angle: 125, animate: true, animationDuration: 36, driftDegrees: 10, vignetteStrength: 22 },
        { name: qsTr("Sunrise"), startColor: "#ff8a5b", endColor: "#ffd166", angle: 30, animate: true, animationDuration: 32, driftDegrees: 12, vignetteStrength: 10 },
        { name: qsTr("Forest"), startColor: "#102a1f", endColor: "#2c6e49", angle: 150, animate: true, animationDuration: 40, driftDegrees: 8, vignetteStrength: 24 },
        { name: qsTr("Dusk"), startColor: "#1b1f3b", endColor: "#7b2cbf", angle: 95, animate: true, animationDuration: 28, driftDegrees: 14, vignetteStrength: 20 },
        { name: qsTr("Mono"), startColor: "#161616", endColor: "#4a4a4a", angle: 90, animate: false, animationDuration: 30, driftDegrees: 0, vignetteStrength: 28 }
    ]

    property alias cfg_StartColor: startColorButton.color
    property alias cfg_EndColor: endColorButton.color
    property alias cfg_Angle: angleSpinBox.value
    property alias cfg_Animate: animateCheckBox.checked
    property alias cfg_AnimationDuration: durationSpinBox.value
    property alias cfg_DriftDegrees: driftSpinBox.value
    property alias cfg_VignetteStrength: vignetteSpinBox.value

    function applyPreset(index) {
        const preset = presets[index]

        if (!preset || index === 0) {
            return
        }

        startColorButton.color = preset.startColor
        endColorButton.color = preset.endColor
        angleSpinBox.value = preset.angle
        animateCheckBox.checked = preset.animate
        durationSpinBox.value = preset.animationDuration
        driftSpinBox.value = preset.driftDegrees
        vignetteSpinBox.value = preset.vignetteStrength
    }

    QQC2.ComboBox {
        id: presetComboBox
        model: root.presets.map(function(preset) {
            return preset.name
        })
        Kirigami.FormData.label: qsTr("Preset:")

        onActivated: root.applyPreset(currentIndex)
    }

    KQuickControls.ColorButton {
        id: startColorButton
        Kirigami.FormData.label: qsTr("Start color:")
        dialogTitle: qsTr("Select the gradient start color")
    }

    KQuickControls.ColorButton {
        id: endColorButton
        Kirigami.FormData.label: qsTr("End color:")
        dialogTitle: qsTr("Select the gradient end color")
    }

    QQC2.SpinBox {
        id: angleSpinBox
        from: 0
        to: 359
        stepSize: 1
        editable: true
        Kirigami.FormData.label: qsTr("Angle:")
    }

    QQC2.CheckBox {
        id: animateCheckBox
        text: qsTr("Enable slow drift")
        Kirigami.FormData.label: qsTr("Animation:")
    }

    QQC2.SpinBox {
        id: durationSpinBox
        from: 5
        to: 300
        stepSize: 5
        editable: true
        enabled: animateCheckBox.checked
        Kirigami.FormData.label: qsTr("Cycle (seconds):")
    }

    QQC2.SpinBox {
        id: driftSpinBox
        from: 0
        to: 45
        stepSize: 1
        editable: true
        enabled: animateCheckBox.checked
        Kirigami.FormData.label: qsTr("Drift (deg):")
    }

    QQC2.SpinBox {
        id: vignetteSpinBox
        from: 0
        to: 60
        stepSize: 1
        editable: true
        Kirigami.FormData.label: qsTr("Vignette:")
    }
}
