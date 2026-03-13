/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols as KQuickControls

Kirigami.FormLayout {
    id: root
    twinFormLayouts: parentLayout

    property alias formLayout: root

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
    property int cfg_Angle
    property bool cfg_Animate
    property int cfg_AnimationDuration
    property int cfg_DriftDegrees
    property int cfg_VignetteStrength
    property bool cfg_UseTimeOfDay
    property int cfg_DayStartHour
    property int cfg_NightStartHour
    property int cfg_TransitionMinutes
    property alias cfg_DayStartColor: dayStartColorButton.color
    property alias cfg_DayEndColor: dayEndColorButton.color
    property alias cfg_NightStartColor: nightStartColorButton.color
    property alias cfg_NightEndColor: nightEndColorButton.color

    readonly property bool manualMode: !cfg_UseTimeOfDay

    function applyPreset(index) {
        const preset = presets[index]

        if (!preset || index === 0) {
            return
        }

        root.cfg_UseTimeOfDay = false
        root.cfg_StartColor = preset.startColor
        root.cfg_EndColor = preset.endColor
        root.cfg_Angle = preset.angle
        root.cfg_Animate = preset.animate
        root.cfg_AnimationDuration = preset.animationDuration
        root.cfg_DriftDegrees = preset.driftDegrees
        root.cfg_VignetteStrength = preset.vignetteStrength
    }

    QQC2.ComboBox {
        id: modeComboBox
        Kirigami.FormData.label: qsTr("Mode:")
        model: [
            qsTr("Manual gradient"),
            qsTr("Time of day")
        ]
        currentIndex: root.cfg_UseTimeOfDay ? 1 : 0

        onActivated: root.cfg_UseTimeOfDay = currentIndex === 1
    }

    QQC2.Label {
        text: root.manualMode
            ? qsTr("Choose the two colors used for the gradient all day.")
            : qsTr("Choose separate day and night palettes. The wallpaper blends between them using the local system time.")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.ComboBox {
        id: presetComboBox
        Kirigami.FormData.label: qsTr("Preset:")
        model: root.presets.map(function(preset) {
            return preset.name
        })
        visible: root.manualMode

        onActivated: root.applyPreset(currentIndex)
    }

    KQuickControls.ColorButton {
        id: startColorButton
        Kirigami.FormData.label: qsTr("Start color:")
        dialogTitle: qsTr("Select the gradient start color")
        visible: root.manualMode
    }

    KQuickControls.ColorButton {
        id: endColorButton
        Kirigami.FormData.label: qsTr("End color:")
        dialogTitle: qsTr("Select the gradient end color")
        visible: root.manualMode
    }

    QQC2.SpinBox {
        id: angleSpinBox
        Kirigami.FormData.label: qsTr("Angle:")
        from: 0
        to: 359
        stepSize: 1
        editable: true
        value: root.cfg_Angle

        onValueModified: root.cfg_Angle = value
    }

    QQC2.SpinBox {
        id: dayStartHourSpinBox
        Kirigami.FormData.label: qsTr("Day starts:")
        from: 0
        to: 23
        stepSize: 1
        editable: true
        value: root.cfg_DayStartHour
        visible: !root.manualMode

        onValueModified: root.cfg_DayStartHour = value
    }

    QQC2.SpinBox {
        id: nightStartHourSpinBox
        Kirigami.FormData.label: qsTr("Night starts:")
        from: 0
        to: 23
        stepSize: 1
        editable: true
        value: root.cfg_NightStartHour
        visible: !root.manualMode

        onValueModified: root.cfg_NightStartHour = value
    }

    QQC2.SpinBox {
        id: transitionSpinBox
        Kirigami.FormData.label: qsTr("Blend (min):")
        from: 0
        to: 180
        stepSize: 5
        editable: true
        value: root.cfg_TransitionMinutes
        visible: !root.manualMode

        onValueModified: root.cfg_TransitionMinutes = value
    }

    KQuickControls.ColorButton {
        id: dayStartColorButton
        Kirigami.FormData.label: qsTr("Day start:")
        dialogTitle: qsTr("Select the day palette start color")
        visible: !root.manualMode
    }

    KQuickControls.ColorButton {
        id: dayEndColorButton
        Kirigami.FormData.label: qsTr("Day end:")
        dialogTitle: qsTr("Select the day palette end color")
        visible: !root.manualMode
    }

    KQuickControls.ColorButton {
        id: nightStartColorButton
        Kirigami.FormData.label: qsTr("Night start:")
        dialogTitle: qsTr("Select the night palette start color")
        visible: !root.manualMode
    }

    KQuickControls.ColorButton {
        id: nightEndColorButton
        Kirigami.FormData.label: qsTr("Night end:")
        dialogTitle: qsTr("Select the night palette end color")
        visible: !root.manualMode
    }

    QQC2.CheckBox {
        id: animateCheckBox
        Kirigami.FormData.label: qsTr("Animation:")
        text: qsTr("Enable slow drift")
        checked: root.cfg_Animate

        onToggled: root.cfg_Animate = checked
    }

    QQC2.SpinBox {
        id: durationSpinBox
        Kirigami.FormData.label: qsTr("Cycle (seconds):")
        from: 5
        to: 300
        stepSize: 5
        editable: true
        enabled: animateCheckBox.checked
        value: root.cfg_AnimationDuration

        onValueModified: root.cfg_AnimationDuration = value
    }

    QQC2.SpinBox {
        id: driftSpinBox
        Kirigami.FormData.label: qsTr("Drift (deg):")
        from: 0
        to: 45
        stepSize: 1
        editable: true
        enabled: animateCheckBox.checked
        value: root.cfg_DriftDegrees

        onValueModified: root.cfg_DriftDegrees = value
    }

    QQC2.SpinBox {
        id: vignetteSpinBox
        Kirigami.FormData.label: qsTr("Vignette:")
        from: 0
        to: 60
        stepSize: 1
        editable: true
        value: root.cfg_VignetteStrength

        onValueModified: root.cfg_VignetteStrength = value
    }

    QQC2.Label {
        text: root.manualMode
            ? qsTr("Manual mode uses the two colors above for the full day.")
            : qsTr("Time-of-day mode blends from the night palette into the day palette at the configured hours using your VM or system local time.")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
