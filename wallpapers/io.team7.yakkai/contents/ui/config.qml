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
    wideMode: false

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
    property string cfg_WEWebProjectPath
    property string cfg_WEWebProjectTitle
    property string cfg_WEWebSource
    property string cfg_WEWebPropertiesJson
    property string cfg_WESceneProjectPath
    property string cfg_WESceneProjectTitle
    property string cfg_WESceneSource
    property string cfg_WESceneSourceKind
    property string cfg_WEScenePropertiesJson
    property bool cfg_WESceneExperimentalEnabled
    property bool cfg_WESceneMouseInput
    property string cfg_PlaylistsJson
    property int cfg_ActivePlaylistIndex

    property var playlistData: {
        if (!cfg_PlaylistsJson || cfg_PlaylistsJson.length === 0) return { playlists: [] }
        try { return JSON.parse(cfg_PlaylistsJson) } catch(e) { return { playlists: [] } }
    }
    property var activePlaylist: {
        const idx = cfg_ActivePlaylistIndex
        const pls = playlistData.playlists || []
        return (idx >= 0 && idx < pls.length) ? pls[idx] : null
    }
    readonly property bool playlistContentMode: cfg_ContentMode === 6

    function savePlaylistData() {
        cfg_PlaylistsJson = JSON.stringify(playlistData)
    }

    function playlistCreate(name) {
        const data = JSON.parse(JSON.stringify(playlistData))
        data.playlists.push({ name: name, mode: 0, interval: 30, items: [] })
        cfg_PlaylistsJson = JSON.stringify(data)
        cfg_ActivePlaylistIndex = data.playlists.length - 1
    }

    function playlistDelete(index) {
        const data = JSON.parse(JSON.stringify(playlistData))
        data.playlists.splice(index, 1)
        cfg_PlaylistsJson = JSON.stringify(data)
        if (cfg_ActivePlaylistIndex >= data.playlists.length)
            cfg_ActivePlaylistIndex = data.playlists.length - 1
    }

    function playlistRename(index, newName) {
        const data = JSON.parse(JSON.stringify(playlistData))
        if (index >= 0 && index < data.playlists.length) {
            data.playlists[index].name = newName
            cfg_PlaylistsJson = JSON.stringify(data)
        }
    }

    function playlistSetMode(mode) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = cfg_ActivePlaylistIndex
        if (idx >= 0 && idx < data.playlists.length) {
            data.playlists[idx].mode = mode
            cfg_PlaylistsJson = JSON.stringify(data)
        }
    }

    function playlistSetInterval(mins) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = cfg_ActivePlaylistIndex
        if (idx >= 0 && idx < data.playlists.length) {
            data.playlists[idx].interval = mins
            cfg_PlaylistsJson = JSON.stringify(data)
        }
    }

    function playlistAddFromPicker(pickerItem) {
        if (!pickerItem) return
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = cfg_ActivePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        data.playlists[idx].items.push({
            title: pickerItem.title || "",
            sceneSource: pickerItem.sourcePath || "",
            sceneProjectPath: pickerItem.projectPath || "",
            sceneProjectTitle: pickerItem.title || "",
            sceneSourceKind: pickerItem.sourceKind || "",
            scenePropertiesJson: "",
            previewPath: pickerItem.previewPath || "",
            workshopId: pickerItem.workshopId || "",
            scheduleTime: "00:00"
        })
        cfg_PlaylistsJson = JSON.stringify(data)
    }

    function playlistRemoveItem(itemIndex) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = cfg_ActivePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        data.playlists[idx].items.splice(itemIndex, 1)
        cfg_PlaylistsJson = JSON.stringify(data)
    }

    function playlistMoveItemUp(itemIndex) {
        if (itemIndex <= 0) return
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = cfg_ActivePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        const items = data.playlists[idx].items
        const tmp = items[itemIndex - 1]
        items[itemIndex - 1] = items[itemIndex]
        items[itemIndex] = tmp
        cfg_PlaylistsJson = JSON.stringify(data)
    }

    function playlistSetItemTime(itemIndex, time) {
        const data = JSON.parse(JSON.stringify(playlistData))
        const idx = cfg_ActivePlaylistIndex
        if (idx < 0 || idx >= data.playlists.length) return
        data.playlists[idx].items[itemIndex].scheduleTime = time
        cfg_PlaylistsJson = JSON.stringify(data)
    }

    property var scenePropertyModel: []

    property var wallpaperEngineVideoItems: []
    property var wallpaperEngineWebItems: []
    property var wallpaperEngineSceneItems: []
    property bool wallpaperEngineScanRunning: false
    property string wallpaperEngineScanType: ""
    property string wallpaperEngineScanStatus: ""
    property string wallpaperEngineScanError: ""
    property bool startupComplete: false

    readonly property bool gradientContentMode: cfg_ContentMode === 0
    readonly property bool localVideoContentMode: cfg_ContentMode === 1
    readonly property bool wallpaperEngineVideoContentMode: cfg_ContentMode === 2
    readonly property bool wallpaperEngineWebContentMode: cfg_ContentMode === 3
    readonly property bool wallpaperEngineSceneContentMode: cfg_ContentMode === 4
    readonly property bool wallpaperEngineSceneNativeContentMode: cfg_ContentMode === 5
    readonly property bool umbrellaContentMode: cfg_ContentMode === 7
    readonly property bool wallpaperEngineAnySceneContentMode: wallpaperEngineSceneContentMode || wallpaperEngineSceneNativeContentMode || playlistContentMode
    readonly property bool wallpaperEngineContentMode: wallpaperEngineVideoContentMode || wallpaperEngineWebContentMode || wallpaperEngineAnySceneContentMode || playlistContentMode || umbrellaContentMode
    // Umbrella needs its own scanner type — not always "scene"
    readonly property string umbrellaCurrentType: cfg_UmbrellaSelectedType || "scene"
    readonly property bool playbackVideoContentMode: localVideoContentMode || wallpaperEngineVideoContentMode
    readonly property bool scenePlaybackContentMode: wallpaperEngineSceneNativeContentMode
    readonly property bool mediaSizingContentMode: playbackVideoContentMode || scenePlaybackContentMode
    readonly property bool audioContentMode: localVideoContentMode || wallpaperEngineVideoContentMode || wallpaperEngineWebContentMode || scenePlaybackContentMode
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
    readonly property var currentWallpaperEngineItems: {
        if (umbrellaContentMode) {
            // Combine all types, tagging each with its source type
            const tagged = []
            for (const item of wallpaperEngineSceneItems)
                tagged.push(Object.assign({}, item, { weType: "scene" }))
            for (const item of wallpaperEngineVideoItems)
                tagged.push(Object.assign({}, item, { weType: "video" }))
            for (const item of wallpaperEngineWebItems)
                tagged.push(Object.assign({}, item, { weType: "web" }))
            tagged.sort((a, b) => (a.title || "").localeCompare(b.title || ""))
            return tagged
        }
        return wallpaperEngineAnySceneContentMode
            ? wallpaperEngineSceneItems
            : (wallpaperEngineWebContentMode ? wallpaperEngineWebItems : wallpaperEngineVideoItems)
    }
    readonly property string currentWallpaperEngineType: umbrellaContentMode
        ? "scene"  // umbrella scans all types via umbrellaScanQueue, this is just the initial type
        : (wallpaperEngineAnySceneContentMode
            ? "scene"
            : (wallpaperEngineWebContentMode ? "web" : "video"))
    readonly property var selectedWallpaperEngineProject: {
        let savedPath = ""
        if (umbrellaContentMode) {
            // Check all types to find the active selection
            savedPath = root.cfg_WESceneProjectPath || root.cfg_WEVideoProjectPath || root.cfg_WEWebProjectPath
        } else {
            savedPath = wallpaperEngineAnySceneContentMode
                ? root.cfg_WESceneProjectPath
                : (wallpaperEngineWebContentMode ? root.cfg_WEWebProjectPath : root.cfg_WEVideoProjectPath)
        }
        if (!savedPath || savedPath.length === 0) return null
        const items = currentWallpaperEngineItems
        for (let i = 0; i < items.length; i++) {
            if (items[i].projectPath === savedPath) return items[i]
        }
        return null
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

    function syncSceneModeFlags() {
        root.cfg_WESceneExperimentalEnabled = root.wallpaperEngineSceneNativeContentMode
    }

    function migrateLegacySceneMode() {
        if (root.cfg_ContentMode === 4 && root.cfg_WESceneExperimentalEnabled) {
            root.cfg_ContentMode = 5
        } else {
            syncSceneModeFlags()
        }
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

    function buildScenePropertyModel() {
        if (!root.cfg_WEScenePropertiesJson || root.cfg_WEScenePropertiesJson.length === 0) {
            root.scenePropertyModel = []
            return
        }
        try {
            const props = JSON.parse(root.cfg_WEScenePropertiesJson)
            const model = []
            const keys = Object.keys(props).sort((a, b) => {
                const oa = props[a].order ?? props[a].index ?? 0
                const ob = props[b].order ?? props[b].index ?? 0
                return oa - ob
            })
            for (const key of keys) {
                const p = props[key]
                if (!p || typeof p !== "object") continue
                const ptype = p.type ?? ""
                // Skip non-interactive properties (text labels, dividers)
                if (ptype === "text" || ptype === "textinput" || ptype === "") continue
                const label = (p.text ?? key)
                    .replace(/<[^>]*>/g, "")  // strip HTML tags
                    .replace(/&nbsp;?/gi, " ")
                    .replace(/&amp;?/gi, "&")
                    .replace(/🔘/g, "")
                    .replace(/\s+/g, " ")
                    .trim()
                    .substring(0, 50)
                if (label.length === 0) continue
                const entry = { key: key, label: label, type: ptype, value: p.value }
                if (ptype === "bool") {
                    entry.value = !!p.value
                } else if (ptype === "slider") {
                    entry.min = p.min ?? 0
                    entry.max = p.max ?? 100
                    entry.step = p.step ?? 1
                    entry.value = p.value ?? 0
                } else if (ptype === "combo" && p.options) {
                    entry.options = p.options
                    entry.value = p.value ?? (p.options[0]?.value ?? "")
                } else if (ptype === "color") {
                    entry.value = p.value ?? "#ffffff"
                } else {
                    continue
                }
                model.push(entry)
            }
            root.scenePropertyModel = model
        } catch (e) {
            root.scenePropertyModel = []
        }
    }

    function updateSceneProperty(key, value) {
        try {
            const props = JSON.parse(root.cfg_WEScenePropertiesJson || "{}")
            if (props[key]) {
                props[key].value = value
            }
            root.cfg_WEScenePropertiesJson = JSON.stringify(props)
            // Update the model entry in place
            const newModel = root.scenePropertyModel.slice()
            for (let i = 0; i < newModel.length; i++) {
                if (newModel[i].key === key) {
                    newModel[i] = Object.assign({}, newModel[i], { value: value })
                    break
                }
            }
            root.scenePropertyModel = newModel
        } catch (e) {}
    }

    function clearWallpaperEngineSelection(projectType) {
        if (projectType === "web") {
            root.cfg_WEWebProjectPath = ""
            root.cfg_WEWebProjectTitle = ""
            root.cfg_WEWebSource = ""
            root.cfg_WEWebPropertiesJson = ""
        } else if (projectType === "scene") {
            root.cfg_WESceneProjectPath = ""
            root.cfg_WESceneProjectTitle = ""
            root.cfg_WESceneSource = ""
            root.cfg_WESceneSourceKind = ""
            root.cfg_WEScenePropertiesJson = ""
        } else {
            root.cfg_WEVideoProjectPath = ""
            root.cfg_WEVideoProjectTitle = ""
            root.cfg_WEVideoSource = ""
        }

        wallpaperEngineProjectComboBox.currentIndex = -1
    }

    property string cfg_UmbrellaSelectedType

    function applyWallpaperEngineSelection(index) {
        const item = currentWallpaperEngineItems[index]

        if (!item) {
            return
        }

        // In umbrella mode, use the item's tagged type
        const itemType = item.weType || currentWallpaperEngineType

        if (umbrellaContentMode) {
            root.cfg_UmbrellaSelectedType = itemType
        }

        if (itemType === "web") {
            root.cfg_WEWebProjectPath = item.projectPath
            root.cfg_WEWebProjectTitle = item.title
            root.cfg_WEWebSource = item.sourcePath
            root.cfg_WEWebPropertiesJson = item.propertiesJson ?? "{}"
            return
        }

        if (itemType === "scene") {
            root.cfg_WESceneProjectPath = item.projectPath
            root.cfg_WESceneProjectTitle = item.title
            root.cfg_WESceneSource = item.sourcePath
            root.cfg_WESceneSourceKind = item.sourceKind ?? ""
            root.cfg_WEScenePropertiesJson = item.propertiesJson ?? "{}"
            root.cfg_WESceneExperimentalEnabled = true
            return
        }

        root.cfg_WEVideoProjectPath = item.projectPath
        root.cfg_WEVideoProjectTitle = item.title
        root.cfg_WEVideoSource = item.sourcePath
    }

    function syncWallpaperEngineSelection(projectType) {
        const items = projectType === "web"
            ? wallpaperEngineWebItems
            : (projectType === "scene" ? wallpaperEngineSceneItems : wallpaperEngineVideoItems)

        if (items.length === 0) {
            wallpaperEngineProjectComboBox.currentIndex = -1
            return
        }

        const projectPath = String(
            projectType === "web"
                ? root.cfg_WEWebProjectPath ?? ""
                : (projectType === "scene" ? root.cfg_WESceneProjectPath ?? "" : root.cfg_WEVideoProjectPath ?? "")
        )
        const sourcePath = String(
            projectType === "web"
                ? root.cfg_WEWebSource ?? ""
                : (projectType === "scene" ? root.cfg_WESceneSource ?? "" : root.cfg_WEVideoSource ?? "")
        )
        let index = items.findIndex(function(item) {
            return item.projectPath === projectPath
        })

        if (index < 0 && sourcePath.length > 0) {
            index = items.findIndex(function(item) {
                return item.sourcePath === sourcePath
            })
        }

        if (index < 0) {
            index = 0
        }

        wallpaperEngineProjectComboBox.currentIndex = index
        applyWallpaperEngineSelection(index)
    }

    function wallpaperEngineScannerPath() {
        return localPath(Qt.resolvedUrl("../tools/we_video_scan.py"))
    }

    property var umbrellaScanQueue: []

    function startWallpaperEngineScan() {
        const libraryPath = localPath(root.cfg_WEVideoLibraryPath)

        if (libraryPath.length === 0 || wallpaperEngineScanRunning) {
            return
        }

        if (umbrellaContentMode) {
            // Scan all types sequentially
            umbrellaScanQueue = ["scene", "video", "web"]
            wallpaperEngineScanStatus = qsTr("Scanning Steam library for all wallpaper types…")
            startNextUmbrellaScan()
            return
        }

        const projectType = currentWallpaperEngineType
        wallpaperEngineScanRunning = true
        wallpaperEngineScanType = projectType
        wallpaperEngineScanError = ""
        wallpaperEngineScanStatus = projectType === "web"
            ? qsTr("Scanning Steam library for Wallpaper Engine web projects…")
            : (projectType === "scene"
                ? qsTr("Scanning Steam library for Wallpaper Engine scene projects…")
                : qsTr("Scanning Steam library for Wallpaper Engine video projects…"))

        const command = "python3 "
            + shellQuote(wallpaperEngineScannerPath())
            + " "
            + shellQuote(libraryPath)
            + " "
            + shellQuote(projectType)

        wallpaperEngineScanner.exec(command)
    }

    function startNextUmbrellaScan() {
        if (umbrellaScanQueue.length === 0) {
            wallpaperEngineScanRunning = false
            wallpaperEngineScanStatus = qsTr("Found %1 scene, %2 video, %3 web wallpapers")
                .arg(wallpaperEngineSceneItems.length)
                .arg(wallpaperEngineVideoItems.length)
                .arg(wallpaperEngineWebItems.length)
            return
        }
        const nextType = umbrellaScanQueue.shift()
        wallpaperEngineScanRunning = true
        wallpaperEngineScanType = nextType
        wallpaperEngineScanError = ""

        const command = "python3 "
            + shellQuote(wallpaperEngineScannerPath())
            + " "
            + shellQuote(localPath(root.cfg_WEVideoLibraryPath))
            + " "
            + shellQuote(nextType)

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

        if (wallpaperEngineScanType === "web") {
            wallpaperEngineWebItems = payload.items ?? []
        } else if (wallpaperEngineScanType === "scene") {
            wallpaperEngineSceneItems = payload.items ?? []
        } else {
            wallpaperEngineVideoItems = payload.items ?? []
        }

        if (payload.errors && payload.errors.length > 0) {
            wallpaperEngineScanError = payload.errors.join(" ")
        } else {
            wallpaperEngineScanError = ""
        }

        const itemCount = wallpaperEngineScanType === "web"
            ? wallpaperEngineWebItems.length
            : (wallpaperEngineScanType === "scene" ? wallpaperEngineSceneItems.length : wallpaperEngineVideoItems.length)
        wallpaperEngineScanStatus = wallpaperEngineScanType === "web"
            ? qsTr("Found %1 Wallpaper Engine web wallpapers.").arg(itemCount)
            : (wallpaperEngineScanType === "scene"
                ? qsTr("Found %1 Wallpaper Engine scene wallpapers.").arg(itemCount)
                : qsTr("Found %1 Wallpaper Engine video wallpapers.").arg(itemCount))

        // Continue umbrella scan queue if more types to scan
        if (umbrellaScanQueue.length > 0) {
            startNextUmbrellaScan()
            return
        }

        if (itemCount > 0 && !umbrellaContentMode) {
            syncWallpaperEngineSelection(wallpaperEngineScanType)
        }
    }

    onCfg_ContentModeChanged: {
        if (!startupComplete) {
            return
        }

        syncSceneModeFlags()

        if (wallpaperEngineContentMode
                && root.cfg_WEVideoLibraryPath.length > 0
                && !wallpaperEngineScanRunning) {
            if (currentWallpaperEngineItems.length === 0) {
                startWallpaperEngineScan()
            } else if (!umbrellaContentMode) {
                syncWallpaperEngineSelection(currentWallpaperEngineType)
            }
        }
    }

    onCfg_WEScenePropertiesJsonChanged: {
        if (startupComplete) buildScenePropertyModel()
    }

    Component.onCompleted: {
        migrateLegacySceneMode()
        startupComplete = true
        buildScenePropertyModel()

        if (root.cfg_WEVideoLibraryPath.length > 0 && wallpaperEngineContentMode) {
            startWallpaperEngineScan()
        }
    }

    QQC2.ComboBox {
        id: contentModeComboBox
        Kirigami.FormData.label: qsTr("Content:")
        model: [
            qsTr("Gradient"),
            qsTr("Video"),
            qsTr("WE Video"),
            qsTr("WE Web"),
            qsTr("WE Scene (diagnostics)"),
            qsTr("WE Scene (native)"),
            qsTr("Playlist"),
            qsTr("All Wallpapers")
        ]
        currentIndex: root.cfg_ContentMode

        onActivated: root.cfg_ContentMode = currentIndex
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

    Rectangle {
        Kirigami.FormData.label: qsTr("Preview:")
        visible: root.gradientContentMode
        Layout.fillWidth: true
        height: 80
        radius: 4
        border.color: Kirigami.Theme.disabledTextColor
        border.width: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0.0
                color: root.manualMode ? root.cfg_StartColor : root.cfg_DayStartColor
            }
            GradientStop {
                position: 1.0
                color: root.manualMode ? root.cfg_EndColor : root.cfg_DayEndColor
            }
        }
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
        Layout.maximumWidth: 460
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
        Layout.maximumWidth: 460
        visible: root.wallpaperEngineContentMode

        RowLayout {
            Layout.fillWidth: true

            QQC2.Button {
                text: root.cfg_WEVideoLibraryPath.length > 0 ? qsTr("Change…") : qsTr("Select Steam library…")
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
                enabled: root.cfg_WEVideoLibraryPath.length > 0
                    || root.cfg_WEVideoProjectPath.length > 0
                    || root.cfg_WEWebProjectPath.length > 0
                    || root.cfg_WESceneProjectPath.length > 0

                onClicked: {
                    root.cfg_WEVideoLibraryPath = ""
                    root.wallpaperEngineVideoItems = []
                    root.wallpaperEngineWebItems = []
                    root.wallpaperEngineSceneItems = []
                    root.wallpaperEngineScanStatus = ""
                    root.wallpaperEngineScanError = ""
                    root.clearWallpaperEngineSelection("video")
                    root.clearWallpaperEngineSelection("web")
                    root.clearWallpaperEngineSelection("scene")
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

    ColumnLayout {
        Kirigami.FormData.label: qsTr("Wallpaper:")
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        visible: root.wallpaperEngineContentMode
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: wallpaperSearchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search wallpapers…")
            visible: root.currentWallpaperEngineItems.length > 6
        }

        GridView {
            id: wallpaperGrid
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(cellHeight * Math.ceil(count / columns), 320)
            Layout.maximumHeight: 320
            clip: true

            readonly property int columns: Math.max(1, Math.floor(width / 140))
            cellWidth: width / columns
            cellHeight: 110

            model: {
                const items = root.currentWallpaperEngineItems
                const query = wallpaperSearchField.text.toLowerCase()
                if (query.length === 0) return items
                return items.filter(item => {
                    const title = (item.title || "").toLowerCase()
                    const wid = (item.workshopId || "").toLowerCase()
                    const wtype = (item.weType || "").toLowerCase()
                    return title.includes(query) || wid.includes(query) || wtype.includes(query)
                })
            }

            delegate: Item {
                width: wallpaperGrid.cellWidth
                height: wallpaperGrid.cellHeight
                required property var modelData
                required property int index

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: 4
                    color: {
                        if (root.playlistContentMode) {
                            // In playlist mode, highlight items already in the playlist
                            const items = root.activePlaylist ? (root.activePlaylist.items || []) : []
                            const inPlaylist = items.some(it => it.sceneProjectPath === modelData.projectPath)
                            return inPlaylist ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3)
                                : (hoverArea.containsMouse ? Kirigami.Theme.hoverColor : "transparent")
                        }
                        const sel = root.selectedWallpaperEngineProject
                        const isSel = sel && sel.projectPath === modelData.projectPath
                        return isSel ? Kirigami.Theme.highlightColor : (hoverArea.containsMouse ? Kirigami.Theme.hoverColor : "transparent")
                    }
                    border.color: {
                        if (root.playlistContentMode) {
                            const items = root.activePlaylist ? (root.activePlaylist.items || []) : []
                            const inPlaylist = items.some(it => it.sceneProjectPath === modelData.projectPath)
                            return inPlaylist ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                        }
                        const sel = root.selectedWallpaperEngineProject
                        const isSel = sel && sel.projectPath === modelData.projectPath
                        return isSel ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                    }
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 2
                        spacing: 1

                        Image {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            source: modelData.previewPath ? ("file://" + modelData.previewPath) : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: modelData.previewPath && modelData.previewPath.length > 0

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                visible: parent.status === Image.Error || !parent.visible
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Kirigami.Theme.backgroundColor
                            visible: !modelData.previewPath || modelData.previewPath.length === 0

                            QQC2.Label {
                                anchors.centerIn: parent
                                text: "?"
                                opacity: 0.3
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: modelData.title || ""
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: {
                                const wid = modelData.workshopId || ""
                                const wtype = modelData.weType || ""
                                if (wtype && root.umbrellaContentMode)
                                    return wid ? (wid + " · " + wtype) : wtype
                                return wid
                            }
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: 0.5
                            visible: text.length > 0
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.playlistContentMode && root.activePlaylist) {
                                // In playlist mode, add to playlist
                                root.playlistAddFromPicker(modelData)
                            } else {
                                // Normal mode, select wallpaper
                                const items = root.currentWallpaperEngineItems
                                for (let i = 0; i < items.length; i++) {
                                    if (items[i].projectPath === modelData.projectPath) {
                                        root.applyWallpaperEngineSelection(i)
                                        break
                                    }
                                }
                            }
                        }
                    }

                    QQC2.ToolTip.visible: hoverArea.containsMouse
                    QQC2.ToolTip.text: modelData.title || ""
                    QQC2.ToolTip.delay: 500
                }
            }
        }

        QQC2.Label {
            text: root.currentWallpaperEngineItems.length === 0
                ? qsTr("Select a Steam library, scan, and choose a wallpaper.")
                : qsTr("%1 wallpapers found").arg(root.currentWallpaperEngineItems.length)
            opacity: 0.7
            visible: wallpaperGrid.count === 0 || root.currentWallpaperEngineItems.length > 0
        }
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
        visible: root.mediaSizingContentMode

        onActivated: root.cfg_VideoFillMode = currentIndex
    }

    QQC2.CheckBox {
        Kirigami.FormData.label: qsTr("Input:")
        text: qsTr("Enable scene mouse and hover input")
        checked: root.cfg_WESceneMouseInput
        visible: root.scenePlaybackContentMode

        onToggled: root.cfg_WESceneMouseInput = checked
    }

    QQC2.CheckBox {
        Kirigami.FormData.label: qsTr("Audio:")
        text: qsTr("Mute audio")
        checked: root.cfg_VideoMuted
        visible: root.audioContentMode

        onToggled: root.cfg_VideoMuted = checked
    }

    // Scene property editor — shows user-configurable properties from the WE scene
    Kirigami.Separator {
        visible: root.wallpaperEngineSceneNativeContentMode && scenePropertyRepeater.count > 0
    }

    QQC2.Label {
        Kirigami.FormData.label: qsTr("Scene properties:")
        text: qsTr("Adjust settings exposed by the wallpaper author.")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        visible: root.wallpaperEngineSceneNativeContentMode && scenePropertyRepeater.count > 0
    }

    Repeater {
        id: scenePropertyRepeater
        model: root.scenePropertyModel
        delegate: ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 460
            required property var modelData
            visible: root.wallpaperEngineSceneNativeContentMode
            spacing: 2

            QQC2.Label {
                text: modelData.label
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.8
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            QQC2.CheckBox {
                visible: modelData.type === "bool"
                text: modelData.value ? qsTr("On") : qsTr("Off")
                checked: modelData.type === "bool" ? !!modelData.value : false
                onToggled: root.updateSceneProperty(modelData.key, checked)
            }

            RowLayout {
                visible: modelData.type === "slider"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                QQC2.Slider {
                    from: modelData.min ?? 0
                    to: modelData.max ?? 1
                    stepSize: modelData.step ?? 0.01
                    value: modelData.type === "slider" ? (modelData.value ?? 0) : 0
                    Layout.fillWidth: true
                    Layout.maximumWidth: 250
                    onMoved: root.updateSceneProperty(modelData.key, value)
                }
                QQC2.Label {
                    text: modelData.type === "slider" ? Number(modelData.value ?? 0).toFixed(2) : ""
                    Layout.minimumWidth: 44
                }
            }

            QQC2.ComboBox {
                visible: modelData.type === "combo"
                Layout.fillWidth: true
                Layout.maximumWidth: 300
                model: (modelData.options ?? []).map(o => o.label)
                currentIndex: {
                    if (modelData.type !== "combo") return 0
                    const val = String(modelData.value)
                    const opts = modelData.options ?? []
                    for (let i = 0; i < opts.length; i++) {
                        if (String(opts[i].value) === val) return i
                    }
                    return 0
                }
                onActivated: {
                    const opts = modelData.options ?? []
                    if (currentIndex >= 0 && currentIndex < opts.length)
                        root.updateSceneProperty(modelData.key, opts[currentIndex].value)
                }
            }
        }
    }

    // ---- Playlist section (visible when content mode is Playlist) ----
    Kirigami.Separator {
        visible: root.playlistContentMode
    }

    RowLayout {
        Kirigami.FormData.label: qsTr("Playlist:")
        visible: root.playlistContentMode
        Layout.fillWidth: true
        Layout.maximumWidth: 460

        QQC2.ComboBox {
            id: playlistSelector
            Layout.fillWidth: true
            model: (root.playlistData.playlists || []).map(p => p.name)
            currentIndex: root.cfg_ActivePlaylistIndex
            onActivated: root.cfg_ActivePlaylistIndex = currentIndex
        }

        QQC2.Button {
            text: qsTr("New")
            onClicked: {
                playlistNameInput.text = qsTr("Playlist %1").arg((root.playlistData.playlists || []).length + 1)
                playlistNameDialog.mode = "create"
                playlistNameDialog.open()
            }
        }

        QQC2.Button {
            text: qsTr("Rename")
            enabled: root.activePlaylist !== null
            onClicked: {
                playlistNameInput.text = root.activePlaylist.name
                playlistNameDialog.mode = "rename"
                playlistNameDialog.open()
            }
        }

        QQC2.Button {
            text: qsTr("Delete")
            enabled: root.activePlaylist !== null
            onClicked: playlistDeleteDialog.open()
        }
    }

    QQC2.ComboBox {
        Kirigami.FormData.label: qsTr("Cycle:")
        visible: root.playlistContentMode && root.activePlaylist
        model: [qsTr("Sequential"), qsTr("Random"), qsTr("Scheduled")]
        currentIndex: root.activePlaylist ? (root.activePlaylist.mode || 0) : 0
        onActivated: root.playlistSetMode(currentIndex)
    }

    QQC2.SpinBox {
        Kirigami.FormData.label: qsTr("Interval (min):")
        visible: root.playlistContentMode && root.activePlaylist && (root.activePlaylist.mode || 0) !== 2
        from: 1
        to: 1440
        stepSize: 5
        editable: true
        value: root.activePlaylist ? (root.activePlaylist.interval || 30) : 30
        onValueModified: root.playlistSetInterval(value)
    }

    QQC2.Label {
        text: qsTr("Add wallpapers from the picker below, then set a time for each.")
        visible: root.playlistContentMode && root.activePlaylist && (root.activePlaylist.mode || 0) === 2
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        opacity: 0.7
    }

    // Playlist items list
    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        visible: root.playlistContentMode && root.activePlaylist && (root.activePlaylist.items || []).length > 0
        spacing: 4

        Repeater {
            model: root.activePlaylist ? (root.activePlaylist.items || []) : []
            delegate: RowLayout {
                Layout.fillWidth: true
                required property var modelData
                required property int index
                spacing: Kirigami.Units.smallSpacing

                Image {
                    source: (modelData.previewPath || "").length > 0 ? ("file://" + modelData.previewPath) : ""
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 32
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: (modelData.previewPath || "").length > 0
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        text: (index + 1) + ". " + (modelData.title || qsTr("Untitled"))
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: modelData.workshopId || ""
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        opacity: 0.5
                        visible: (modelData.workshopId || "").length > 0
                    }
                }

                QQC2.TextField {
                    visible: root.activePlaylist && (root.activePlaylist.mode || 0) === 2
                    Layout.preferredWidth: 60
                    text: modelData.scheduleTime || "00:00"
                    placeholderText: "HH:MM"
                    onEditingFinished: root.playlistSetItemTime(index, text)
                }

                QQC2.ToolButton {
                    icon.name: "go-up"
                    enabled: index > 0
                    onClicked: root.playlistMoveItemUp(index)
                }

                QQC2.ToolButton {
                    icon.name: "edit-delete"
                    onClicked: root.playlistRemoveItem(index)
                }
            }
        }
    }

    QQC2.Label {
        text: root.activePlaylist
            ? qsTr("Select wallpapers below to add to \"%1\":").arg(root.activePlaylist.name)
            : qsTr("Create a playlist first, then add wallpapers to it.")
        visible: root.playlistContentMode
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        opacity: 0.7
    }

    // Playlist name dialog
    QQC2.Dialog {
        id: playlistNameDialog
        property string mode: "create"
        title: mode === "create" ? qsTr("New playlist") : qsTr("Rename playlist")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        modal: true
        anchors.centerIn: parent
        contentItem: QQC2.TextField {
            id: playlistNameInput
            placeholderText: qsTr("Playlist name")
        }
        onAccepted: {
            if (mode === "create") root.playlistCreate(playlistNameInput.text)
            else root.playlistRename(root.cfg_ActivePlaylistIndex, playlistNameInput.text)
        }
    }

    // Playlist delete confirmation
    QQC2.Dialog {
        id: playlistDeleteDialog
        title: qsTr("Delete playlist")
        standardButtons: QQC2.Dialog.Yes | QQC2.Dialog.No
        modal: true
        anchors.centerIn: parent
        contentItem: QQC2.Label {
            text: root.activePlaylist
                ? qsTr("Are you sure you want to delete \"%1\"? This cannot be undone.").arg(root.activePlaylist.name)
                : ""
            wrapMode: Text.WordWrap
        }
        onAccepted: root.playlistDelete(root.cfg_ActivePlaylistIndex)
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
            root.wallpaperEngineWebItems = []
            root.wallpaperEngineSceneItems = []

            if (changed) {
                root.clearWallpaperEngineSelection("video")
                root.clearWallpaperEngineSelection("web")
                root.clearWallpaperEngineSelection("scene")
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
