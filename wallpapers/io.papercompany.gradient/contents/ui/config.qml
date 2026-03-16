/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtCore
import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols as KQuickControls
import org.kde.plasma.plasma5support as Plasma5Support

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

    property int cfg_ContentMode
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
    property string cfg_VideoSource
    property int cfg_VideoFillMode
    property bool cfg_VideoMuted
    property string cfg_WEVideoLibraryPath
    property string cfg_WEVideoProjectPath
    property string cfg_WEVideoProjectTitle
    property string cfg_WEVideoSource

    property var wallpaperEngineVideoItems: []
    property bool wallpaperEngineScanRunning: false
    property string wallpaperEngineScanStatus: ""
    property string wallpaperEngineScanError: ""

    readonly property bool gradientContentMode: cfg_ContentMode === 0
    readonly property bool localVideoContentMode: cfg_ContentMode === 1
    readonly property bool wallpaperEngineVideoContentMode: cfg_ContentMode === 2
    readonly property bool videoLikeContentMode: localVideoContentMode || wallpaperEngineVideoContentMode
    readonly property bool manualMode: gradientContentMode && !cfg_UseTimeOfDay
    readonly property url defaultVideoFolder: {
        let defaultPaths = StandardPaths.standardLocations(StandardPaths.MoviesLocation)

        if (defaultPaths.length === 0) {
            defaultPaths = StandardPaths.standardLocations(StandardPaths.HomeLocation)
        }

        return defaultPaths[0]
    }
    readonly property url defaultSteamLibraryFolder: {
        const homes = StandardPaths.standardLocations(StandardPaths.HomeLocation)
        return homes.length > 0 ? homes[0] : "/"
    }
    readonly property var selectedWallpaperEngineVideo: {
        const index = wallpaperEngineVideoComboBox.currentIndex

        if (index < 0 || index >= wallpaperEngineVideoItems.length) {
            return null
        }

        return wallpaperEngineVideoItems[index]
    }

    function applyPreset(index) {
        const preset = presets[index]

        if (!preset || index === 0) {
            return
        }

        root.cfg_ContentMode = 0
        root.cfg_UseTimeOfDay = false
        root.cfg_StartColor = preset.startColor
        root.cfg_EndColor = preset.endColor
        root.cfg_Angle = preset.angle
        root.cfg_Animate = preset.animate
        root.cfg_AnimationDuration = preset.animationDuration
        root.cfg_DriftDegrees = preset.driftDegrees
        root.cfg_VignetteStrength = preset.vignetteStrength
    }

    function localPath(value) {
        const asText = String(value ?? "")

        if (asText.startsWith("file://")) {
            return decodeURIComponent(asText.substring(7))
        }

        return asText
    }

    function videoSourceLabel(source) {
        const path = localPath(source)
        return path.length > 0 ? path : qsTr("No file selected")
    }

    function shellQuote(value) {
        return "'" + String(value).replace(/'/g, "'\"'\"'") + "'"
    }

    function wallpaperEngineItemLabel(item) {
        if (!item) {
            return ""
        }

        if (item.kind === "workshop" && item.workshopId.length > 0) {
            return qsTr("%1 [Workshop %2]").arg(item.title).arg(item.workshopId)
        }

        if (item.kind === "defaultprojects") {
            return qsTr("%1 [Default]").arg(item.title)
        }

        if (item.kind === "myprojects") {
            return qsTr("%1 [My Project]").arg(item.title)
        }

        return item.title
    }

    function clearWallpaperEngineSelection() {
        root.cfg_WEVideoProjectPath = ""
        root.cfg_WEVideoProjectTitle = ""
        root.cfg_WEVideoSource = ""
        wallpaperEngineVideoComboBox.currentIndex = -1
    }

    function applyWallpaperEngineVideoSelection(index) {
        const item = wallpaperEngineVideoItems[index]

        if (!item) {
            return
        }

        root.cfg_WEVideoProjectPath = item.projectPath
        root.cfg_WEVideoProjectTitle = item.title
        root.cfg_WEVideoSource = item.sourcePath
    }

    function syncWallpaperEngineVideoSelection() {
        if (wallpaperEngineVideoItems.length === 0) {
            wallpaperEngineVideoComboBox.currentIndex = -1
            return
        }

        const projectPath = String(root.cfg_WEVideoProjectPath ?? "")
        const sourcePath = String(root.cfg_WEVideoSource ?? "")
        let index = wallpaperEngineVideoItems.findIndex(function(item) {
            return item.projectPath === projectPath
        })

        if (index < 0 && sourcePath.length > 0) {
            index = wallpaperEngineVideoItems.findIndex(function(item) {
                return item.sourcePath === sourcePath
            })
        }

        if (index < 0) {
            index = 0
        }

        wallpaperEngineVideoComboBox.currentIndex = index
        applyWallpaperEngineVideoSelection(index)
    }

    function wallpaperEngineScannerPath() {
        return localPath(Qt.resolvedUrl("../tools/we_video_scan.py"))
    }

    function startWallpaperEngineScan() {
        const libraryPath = localPath(root.cfg_WEVideoLibraryPath)

        if (libraryPath.length === 0 || wallpaperEngineScanRunning) {
            return
        }

        wallpaperEngineScanRunning = true
        wallpaperEngineScanError = ""
        wallpaperEngineScanStatus = qsTr("Scanning Steam library for Wallpaper Engine video projects…")

        const command = "python3 "
            + shellQuote(wallpaperEngineScannerPath())
            + " "
            + shellQuote(libraryPath)

        wallpaperEngineScanner.exec(command)
    }

    function handleWallpaperEngineScanResult(exitCode, stdout, stderr) {
        wallpaperEngineScanRunning = false

        if (exitCode !== 0) {
            wallpaperEngineScanError = stderr.length > 0
                ? stderr.trim()
                : qsTr("The Wallpaper Engine scan helper failed.")
            wallpaperEngineScanStatus = ""
            return
        }

        let payload = null

        try {
            payload = JSON.parse(stdout)
        } catch (error) {
            wallpaperEngineScanError = qsTr("The Wallpaper Engine scan helper returned invalid JSON.")
            wallpaperEngineScanStatus = ""
            return
        }

        wallpaperEngineVideoItems = payload.items ?? []

        if (payload.errors && payload.errors.length > 0) {
            wallpaperEngineScanError = payload.errors.join(" ")
        } else {
            wallpaperEngineScanError = ""
        }

        wallpaperEngineScanStatus = qsTr("Found %1 Wallpaper Engine video wallpapers.").arg(wallpaperEngineVideoItems.length)

        if (wallpaperEngineVideoItems.length > 0) {
            syncWallpaperEngineVideoSelection()
        } else if (root.cfg_WEVideoProjectPath.length === 0) {
            wallpaperEngineVideoComboBox.currentIndex = -1
        }
    }

    onCfg_ContentModeChanged: {
        if (wallpaperEngineVideoContentMode
                && wallpaperEngineVideoItems.length === 0
                && root.cfg_WEVideoLibraryPath.length > 0
                && !wallpaperEngineScanRunning) {
            startWallpaperEngineScan()
        }
    }

    Component.onCompleted: {
        if (root.cfg_WEVideoLibraryPath.length > 0 && root.cfg_ContentMode === 2) {
            startWallpaperEngineScan()
        }
    }

    QQC2.ComboBox {
        id: contentModeComboBox
        Kirigami.FormData.label: qsTr("Content:")
        model: [
            qsTr("Gradient"),
            qsTr("Video"),
            qsTr("Wallpaper Engine Video")
        ]
        currentIndex: root.cfg_ContentMode

        onActivated: root.cfg_ContentMode = currentIndex
    }

    QQC2.Label {
        text: root.gradientContentMode
            ? qsTr("Gradient mode renders the current Paper Gradient background, with optional manual or time-of-day palettes.")
            : (root.localVideoContentMode
                ? qsTr("Video mode plays a local video file through QtMultimedia. Actual playback depends on the codecs installed in the current Plasma system or VM.")
                : qsTr("Wallpaper Engine Video mode scans a Steam library for Wallpaper Engine video projects and reuses the Paper Gradient video backend for playback."))
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.ComboBox {
        id: modeComboBox
        Kirigami.FormData.label: qsTr("Mode:")
        model: [
            qsTr("Manual gradient"),
            qsTr("Time of day")
        ]
        currentIndex: root.cfg_UseTimeOfDay ? 1 : 0
        visible: root.gradientContentMode

        onActivated: root.cfg_UseTimeOfDay = currentIndex === 1
    }

    QQC2.Label {
        text: root.manualMode
            ? qsTr("Choose the two colors used for the gradient all day.")
            : qsTr("Choose separate day and night palettes. The wallpaper blends between them using the local system time.")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        visible: root.gradientContentMode
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
        visible: root.gradientContentMode

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
        visible: root.gradientContentMode && !root.manualMode

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
        visible: root.gradientContentMode && !root.manualMode

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
        visible: root.gradientContentMode && !root.manualMode

        onValueModified: root.cfg_TransitionMinutes = value
    }

    KQuickControls.ColorButton {
        id: dayStartColorButton
        Kirigami.FormData.label: qsTr("Day start:")
        dialogTitle: qsTr("Select the day palette start color")
        visible: root.gradientContentMode && !root.manualMode
    }

    KQuickControls.ColorButton {
        id: dayEndColorButton
        Kirigami.FormData.label: qsTr("Day end:")
        dialogTitle: qsTr("Select the day palette end color")
        visible: root.gradientContentMode && !root.manualMode
    }

    KQuickControls.ColorButton {
        id: nightStartColorButton
        Kirigami.FormData.label: qsTr("Night start:")
        dialogTitle: qsTr("Select the night palette start color")
        visible: root.gradientContentMode && !root.manualMode
    }

    KQuickControls.ColorButton {
        id: nightEndColorButton
        Kirigami.FormData.label: qsTr("Night end:")
        dialogTitle: qsTr("Select the night palette end color")
        visible: root.gradientContentMode && !root.manualMode
    }

    QQC2.CheckBox {
        id: animateCheckBox
        Kirigami.FormData.label: qsTr("Animation:")
        text: qsTr("Enable slow drift")
        checked: root.cfg_Animate
        visible: root.gradientContentMode

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
        visible: root.gradientContentMode

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
        visible: root.gradientContentMode

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
        visible: root.gradientContentMode

        onValueModified: root.cfg_VignetteStrength = value
    }

    ColumnLayout {
        Kirigami.FormData.label: qsTr("Source:")
        Layout.fillWidth: true
        visible: root.localVideoContentMode

        RowLayout {
            Layout.fillWidth: true

            QQC2.Button {
                text: root.cfg_VideoSource.length > 0 ? qsTr("Change video…") : qsTr("Select video…")
                Layout.fillWidth: true
                onClicked: videoFileDialog.open()
            }

            QQC2.Button {
                text: qsTr("Clear")
                enabled: root.cfg_VideoSource.length > 0
                onClicked: root.cfg_VideoSource = ""
            }
        }

        QQC2.Label {
            text: root.videoSourceLabel(root.cfg_VideoSource)
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            opacity: root.cfg_VideoSource.length > 0 ? 1 : 0.7
        }
    }

    ColumnLayout {
        Kirigami.FormData.label: qsTr("Library:")
        Layout.fillWidth: true
        visible: root.wallpaperEngineVideoContentMode

        RowLayout {
            Layout.fillWidth: true

            QQC2.Button {
                text: root.cfg_WEVideoLibraryPath.length > 0 ? qsTr("Change Steam library…") : qsTr("Select Steam library…")
                Layout.fillWidth: true
                enabled: !root.wallpaperEngineScanRunning
                onClicked: steamLibraryDialog.open()
            }

            QQC2.Button {
                text: root.wallpaperEngineScanRunning ? qsTr("Scanning…") : qsTr("Refresh")
                enabled: root.cfg_WEVideoLibraryPath.length > 0 && !root.wallpaperEngineScanRunning
                onClicked: root.startWallpaperEngineScan()
            }

            QQC2.Button {
                text: qsTr("Clear")
                enabled: root.cfg_WEVideoLibraryPath.length > 0 || root.cfg_WEVideoProjectPath.length > 0

                onClicked: {
                    root.cfg_WEVideoLibraryPath = ""
                    root.wallpaperEngineVideoItems = []
                    root.wallpaperEngineScanStatus = ""
                    root.wallpaperEngineScanError = ""
                    root.clearWallpaperEngineSelection()
                }
            }
        }

        QQC2.Label {
            text: root.videoSourceLabel(root.cfg_WEVideoLibraryPath)
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            opacity: root.cfg_WEVideoLibraryPath.length > 0 ? 1 : 0.7
        }

        QQC2.Label {
            text: root.wallpaperEngineScanError.length > 0
                ? root.wallpaperEngineScanError
                : root.wallpaperEngineScanStatus
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            visible: text.length > 0
            color: root.wallpaperEngineScanError.length > 0 ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
        }
    }

    QQC2.ComboBox {
        id: wallpaperEngineVideoComboBox
        Kirigami.FormData.label: qsTr("Wallpaper:")
        model: root.wallpaperEngineVideoItems.map(function(item) {
            return root.wallpaperEngineItemLabel(item)
        })
        enabled: root.wallpaperEngineVideoItems.length > 0 && !root.wallpaperEngineScanRunning
        visible: root.wallpaperEngineVideoContentMode

        onActivated: root.applyWallpaperEngineVideoSelection(currentIndex)
    }

    QQC2.Label {
        text: root.selectedWallpaperEngineVideo
            ? qsTr("Project: %1\nMedia: %2").arg(root.selectedWallpaperEngineVideo.projectPath).arg(root.selectedWallpaperEngineVideo.sourcePath)
            : qsTr("Select a Steam library, refresh the scan, and choose one of the discovered Wallpaper Engine video projects.")
        wrapMode: Text.WrapAnywhere
        Layout.fillWidth: true
        visible: root.wallpaperEngineVideoContentMode
        opacity: root.selectedWallpaperEngineVideo ? 1 : 0.7
    }

    QQC2.ComboBox {
        id: videoFillModeComboBox
        Kirigami.FormData.label: qsTr("Sizing:")
        model: [
            qsTr("Crop"),
            qsTr("Fit"),
            qsTr("Stretch")
        ]
        currentIndex: root.cfg_VideoFillMode
        visible: root.videoLikeContentMode

        onActivated: root.cfg_VideoFillMode = currentIndex
    }

    QQC2.CheckBox {
        Kirigami.FormData.label: qsTr("Audio:")
        text: qsTr("Mute soundtrack")
        checked: root.cfg_VideoMuted
        visible: root.videoLikeContentMode

        onToggled: root.cfg_VideoMuted = checked
    }

    QQC2.Label {
        text: root.gradientContentMode
            ? (root.manualMode
                ? qsTr("Manual mode uses the two colors above for the full day.")
                : qsTr("Time-of-day mode blends from the night palette into the day palette at the configured hours using your VM or system local time."))
            : (root.localVideoContentMode
                ? qsTr("Start with local MP4 or WebM files. If a video fails to play in the VM, the issue is likely the guest multimedia codec stack rather than the wallpaper package itself.")
                : qsTr("Wallpaper Engine Video mode currently supports only Wallpaper Engine projects whose project.json type is video. It uses a small Python helper in the settings page to scan the Steam library and resolve the actual media file."))
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    FileDialog {
        id: videoFileDialog
        title: qsTr("Select a video file")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Video files (*.mp4 *.webm *.mkv *.avi *.mov *.m4v)"),
            qsTr("All files (*)")
        ]
        currentFolder: root.defaultVideoFolder

        onAccepted: root.cfg_VideoSource = root.localPath(selectedFile)
    }

    FolderDialog {
        id: steamLibraryDialog
        title: qsTr("Select a Steam library folder")
        currentFolder: root.defaultSteamLibraryFolder

        onAccepted: {
            const selectedPath = root.localPath(selectedFolder)
            const changed = selectedPath !== root.cfg_WEVideoLibraryPath

            root.cfg_WEVideoLibraryPath = selectedPath
            root.wallpaperEngineScanStatus = ""
            root.wallpaperEngineScanError = ""
            root.wallpaperEngineVideoItems = []

            if (changed) {
                root.clearWallpaperEngineSelection()
            }

            root.startWallpaperEngineScan()
        }
    }

    Plasma5Support.DataSource {
        id: wallpaperEngineScanner
        engine: "executable"
        connectedSources: []

        function exec(command) {
            wallpaperEngineScanner.connectSource(command)
        }

        onNewData: function(source, data) {
            wallpaperEngineScanner.disconnectSource(source)
            root.handleWallpaperEngineScanResult(data["exit code"], data.stdout ?? "", data.stderr ?? "")
        }
    }
}
