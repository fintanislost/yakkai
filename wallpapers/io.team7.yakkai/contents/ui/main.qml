/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtQuick.Window
import QtCore
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root

    readonly property string logPrefix: "[Yakkai]"

    Settings {
        id: sharedPlaylistSettings
        category: "io.team7.yakkai.playlists"
        property string playlistsJson: ""
        property string playlistAllJson: ""
    }

    function resolvedVideoSource(value) {
        const asText = String(value ?? "")

        if (asText.length === 0) {
            return ""
        }

        if (asText.startsWith("file://")) {
            return asText
        }

        if (asText.startsWith("/")) {
            return Qt.resolvedUrl(asText)
        }

        return asText
    }

    function log(message) {
        console.log(logPrefix + " " + message)
    }

    function joinPath(basePath, childPath) {
        const base = String(basePath ?? "").replace(/\/+$/, "")
        const child = String(childPath ?? "")

        if (base.length === 0) {
            return ""
        }

        return base + child
    }

    // Playlist runtime
    readonly property string effectivePlaylistsJson: sharedPlaylistSettings.playlistsJson.length > 0
        ? sharedPlaylistSettings.playlistsJson
        : String(configuration.PlaylistsJson || "")
    readonly property string effectivePlaylistAllJson: sharedPlaylistSettings.playlistAllJson.length > 0
        ? sharedPlaylistSettings.playlistAllJson
        : String(configuration.PlaylistAllJson || "")
    property string playlistsJson: effectivePlaylistsJson
    property int activePlaylistIndex: configuration.ActivePlaylistIndex ?? -1
    property var playlistData: {
        if (!playlistsJson || playlistsJson.length === 0) return { playlists: [] }
        try { return JSON.parse(playlistsJson) } catch(e) { return { playlists: [] } }
    }
    property var activePlaylist: {
        const idx = activePlaylistIndex
        const pls = playlistData.playlists || []
        return (idx >= 0 && idx < pls.length) ? pls[idx] : null
    }
    property bool playlistActive: contentMode === 6 && activePlaylist !== null && (activePlaylist.items || []).length > 0
    property int playlistCurrentIndex: 0

    function migrateSharedPlaylistSettings() {
        const localPlaylistsJson = String(configuration.PlaylistsJson || "")
        if (sharedPlaylistSettings.playlistsJson.length === 0 && localPlaylistsJson.length > 0)
            sharedPlaylistSettings.playlistsJson = localPlaylistsJson

        const localPlaylistAllJson = String(configuration.PlaylistAllJson || "")
        if (sharedPlaylistSettings.playlistAllJson.length === 0 && localPlaylistAllJson.length > 0)
            sharedPlaylistSettings.playlistAllJson = localPlaylistAllJson
    }

    function playlistApplyItem(item) {
        if (!item) return
        log("playlist switch: " + (item.title || "untitled"))
        if (item.sceneSource) {
            configuration.WESceneSource = item.sceneSource
            configuration.WESceneProjectPath = item.sceneProjectPath || ""
            configuration.WESceneProjectTitle = item.sceneProjectTitle || ""
            configuration.WESceneSourceKind = item.sceneSourceKind || ""
            configuration.WEScenePropertiesJson = item.scenePropertiesJson || ""
            configuration.WESceneExperimentalEnabled = true
        }
    }

    function playlistAdvance() {
        const items = activePlaylist ? (activePlaylist.items || []) : []
        if (items.length < 2) return
        const mode = activePlaylist.mode || 0
        if (mode === 1) {
            let next = Math.floor(Math.random() * items.length)
            while (next === playlistCurrentIndex && items.length > 1)
                next = Math.floor(Math.random() * items.length)
            playlistCurrentIndex = next
        } else {
            playlistCurrentIndex = (playlistCurrentIndex + 1) % items.length
        }
        playlistApplyItem(items[playlistCurrentIndex])
    }

    function playlistCheckSchedule() {
        const items = activePlaylist ? (activePlaylist.items || []) : []
        if (items.length === 0) return
        const now = new Date()
        const nowMins = now.getHours() * 60 + now.getMinutes()
        // Find the item whose schedule time is closest before now
        let bestIdx = 0
        let bestMins = -1
        for (let i = 0; i < items.length; i++) {
            const t = items[i].scheduleTime || "00:00"
            const parts = t.split(":")
            const mins = parseInt(parts[0] || "0") * 60 + parseInt(parts[1] || "0")
            if (mins <= nowMins && mins > bestMins) {
                bestMins = mins
                bestIdx = i
            }
        }
        // Handle wrap-around: if no item is before now, use the last scheduled one
        if (bestMins < 0) {
            for (let i = 0; i < items.length; i++) {
                const t = items[i].scheduleTime || "00:00"
                const parts = t.split(":")
                const mins = parseInt(parts[0] || "0") * 60 + parseInt(parts[1] || "0")
                if (mins > bestMins) { bestMins = mins; bestIdx = i }
            }
        }
        playlistCurrentIndex = bestIdx
        playlistApplyItem(items[bestIdx])
    }

    Timer {
        id: playlistCycleTimer
        interval: (root.activePlaylist ? (root.activePlaylist.interval || 30) : 30) * 60 * 1000
        repeat: true
        running: root.playlistActive && (root.activePlaylist.mode || 0) !== 2
        onTriggered: root.playlistAdvance()
    }

    Timer {
        id: playlistScheduleTimer
        interval: 60000
        repeat: true
        running: root.playlistActive && (root.activePlaylist.mode || 0) === 2
        onTriggered: root.playlistCheckSchedule()
    }

    function playlistTryApply() {
        if (contentMode !== 6) return
        const pl = activePlaylist
        if (!pl) return
        const items = pl.items || []
        if (items.length === 0) return
        const mode = pl.mode || 0
        if (mode === 2) {
            playlistCheckSchedule()
        } else {
            if (playlistCurrentIndex < 0 || playlistCurrentIndex >= items.length)
                playlistCurrentIndex = 0
            playlistApplyItem(items[playlistCurrentIndex])
        }
    }

    onContentModeChanged: {
        log("contentMode=" + contentMode + " videoMode=" + videoMode + " webMode=" + webMode + " sceneMode=" + sceneMode + " sceneNativeMode=" + sceneNativeMode)
        if (contentMode === 6) playlistTryApply()
    }

    onActivePlaylistChanged: {
        playlistCurrentIndex = 0
        playlistTryApply()
    }

    onPlaylistDataChanged: {
        if (contentMode === 6) playlistTryApply()
    }

    property bool verboseLogging: configuration.VerboseLogging ?? false
    property string postEffect: configuration.PostEffect ?? "none"
    property real crtScanline: (configuration.CrtScanline ?? 30) / 100.0
    property real crtCurvature: (configuration.CrtCurvature ?? 25) / 100.0
    property real crtAberration: (configuration.CrtAberration ?? 25) / 100.0
    property real crtVignette: (configuration.CrtVignette ?? 50) / 100.0
    property real crtPhosphor: (configuration.CrtPhosphor ?? 20) / 100.0
    property real crtBrightness: (configuration.CrtBrightness ?? 20) / 100.0
    property real crtVibrance: (configuration.CrtVibrance ?? 0) / 100.0
    property real crtZoom: (configuration.CrtZoom ?? 0) / 100.0
    property int contentMode: configuration.ContentMode ?? 7
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
    property url videoSource: resolvedVideoSource(configuration.VideoSource ?? "")
    // In playlist-all mode (8), derive sources directly from the playlist player's
    // current item rather than configuration.WE*Source. This avoids a race where
    // Plasma's config batch commit overwrites the sources with stale cfg_ values.
    readonly property bool playlistAllDriven: contentMode === 8
        && playlistAllPlayer.active && playlistAllPlayer.currentItem !== null
    readonly property var playlistAllItem: playlistAllDriven ? playlistAllPlayer.currentItem : null

    property url wallpaperEngineVideoSource: {
        if (playlistAllItem && playlistAllPlayer.currentType === "video")
            return resolvedVideoSource(playlistAllItem.videoSource || "")
        return resolvedVideoSource(configuration.WEVideoSource ?? "")
    }
    property string wallpaperEngineVideoProjectTitle: configuration.WEVideoProjectTitle ?? ""
    property url wallpaperEngineWebSource: {
        if (playlistAllItem && playlistAllPlayer.currentType === "web")
            return resolvedVideoSource(playlistAllItem.webSource || "")
        return resolvedVideoSource(configuration.WEWebSource ?? "")
    }
    property string wallpaperEngineWebProjectTitle: configuration.WEWebProjectTitle ?? ""
    property string wallpaperEngineWebPropertiesJson: configuration.WEWebPropertiesJson ?? ""
    property url wallpaperEngineSceneSource: {
        if (playlistAllItem && playlistAllPlayer.currentType === "scene")
            return resolvedVideoSource(playlistAllItem.sceneSource || "")
        return resolvedVideoSource(configuration.WESceneSource ?? "")
    }
    property string wallpaperEngineSceneProjectTitle: {
        if (playlistAllItem && playlistAllPlayer.currentType === "scene")
            return playlistAllItem.sceneProjectTitle || ""
        return configuration.WESceneProjectTitle ?? ""
    }
    property string wallpaperEngineSceneSourceKind: {
        if (playlistAllItem && playlistAllPlayer.currentType === "scene")
            return playlistAllItem.sceneSourceKind || ""
        return configuration.WESceneSourceKind ?? ""
    }
    property string wallpaperEngineScenePropertiesJson: {
        if (playlistAllItem && playlistAllPlayer.currentType === "scene")
            return playlistAllItem.propertiesJson || ""
        return configuration.WEScenePropertiesJson ?? ""
    }
    property bool wallpaperEngineSceneExperimentalEnabled: configuration.WESceneExperimentalEnabled ?? false
    property bool wallpaperEngineSceneMouseInput: configuration.WESceneMouseInput ?? false
    property bool wallpaperEngineSceneMouseDiagnosticsEnabled: configuration.WESceneMouseDiagnosticsEnabled ?? false
    property string wallpaperEngineLibraryPath: configuration.WEVideoLibraryPath ?? ""
    property int videoFillMode: configuration.VideoFillMode ?? 0
    property bool videoMuted: configuration.VideoMuted ?? true

    readonly property bool localVideoMode: contentMode === 1 && String(videoSource).length > 0
    readonly property bool wallpaperEngineVideoModeRaw: contentMode === 2 || ((contentMode === 7 || contentMode === 8) && umbrellaResolvedType === "video")
    readonly property bool wallpaperEngineVideoMode: wallpaperEngineVideoModeRaw && String(wallpaperEngineVideoSource).length > 0
    readonly property bool wallpaperEngineWebModeRaw: contentMode === 3 || ((contentMode === 7 || contentMode === 8) && umbrellaResolvedType === "web")
    readonly property bool wallpaperEngineWebMode: wallpaperEngineWebModeRaw && String(wallpaperEngineWebSource).length > 0
    readonly property bool legacySceneNativeMode: contentMode === 4 && wallpaperEngineSceneExperimentalEnabled
    readonly property bool wallpaperEngineSceneMode: contentMode === 4 && !legacySceneNativeMode
    // Gate scene-native mode on actually having a source. Loading the
    // native backend with an empty path crashes TextureNode in libyakkai_scene_backend.
    readonly property bool wallpaperEngineSceneNativeModeRaw: contentMode === 5 || legacySceneNativeMode || contentMode === 6 || ((contentMode === 7 || contentMode === 8) && umbrellaResolvedType === "scene")
    readonly property bool wallpaperEngineSceneNativeMode: wallpaperEngineSceneNativeModeRaw && String(wallpaperEngineSceneSource).length > 0
    readonly property bool playlistMode_: contentMode === 6
    readonly property bool videoMode: localVideoMode || wallpaperEngineVideoMode
    readonly property bool webMode: wallpaperEngineWebMode
    readonly property bool sceneMode: wallpaperEngineSceneMode
    readonly property bool sceneNativeMode: wallpaperEngineSceneNativeMode
    readonly property bool sceneMouseDiagnosticsActive: sceneNativeMode
        && wallpaperEngineSceneMouseInput
        && wallpaperEngineSceneMouseDiagnosticsEnabled
    property double lastSceneMouseDiagnosticLogMs: 0

    function diagnosticNumber(value) {
        return Number(value).toFixed(3)
    }

    function sceneMouseDiagnosticScreenName() {
        if (root.Window.window && root.Window.window.screen && root.Window.window.screen.name) {
            return root.Window.window.screen.name
        }

        return "unknown"
    }

    function logSceneMouseDiagnostic(localX, localY, itemWidth, itemHeight) {
        if (!sceneMouseDiagnosticsActive || itemWidth <= 0 || itemHeight <= 0) {
            return
        }

        const now = Date.now()
        if (now - lastSceneMouseDiagnosticLogMs < 250) {
            return
        }
        lastSceneMouseDiagnosticLogMs = now

        const normalizedX = Math.max(0, Math.min(1, localX / itemWidth))
        const normalizedY = Math.max(0, Math.min(1, localY / itemHeight))

        console.log(logPrefix
            + " mouse-diagnostic screen=" + sceneMouseDiagnosticScreenName()
            + " local=" + diagnosticNumber(localX) + "," + diagnosticNumber(localY)
            + " normalized=" + diagnosticNumber(normalizedX) + "," + diagnosticNumber(normalizedY)
            + " itemSize=" + Math.round(itemWidth) + "x" + Math.round(itemHeight))
    }

    UmbrellaMode {
        id: umbrellaMode
        selectedType: configuration.UmbrellaSelectedType || "scene"
        sceneSource: wallpaperEngineSceneSource.toString()
        videoSource: wallpaperEngineVideoSource.toString()
        webSource: wallpaperEngineWebSource.toString()
    }
    readonly property string umbrellaResolvedType: (contentMode === 7 || contentMode === 8)
        ? (contentMode === 8 && playlistAllPlayer.currentItem ? playlistAllPlayer.currentType : umbrellaMode.resolvedType)
        : ""
    onUmbrellaResolvedTypeChanged: {
        if (contentMode === 7 || contentMode === 8) {
            log("umbrella resolved type: " + umbrellaResolvedType)
        }
    }

    // Playlist (All) runtime
    PlaylistPlayer {
        id: playlistAllPlayer
        playlistsJson: root.effectivePlaylistAllJson
        activePlaylistIndex: parseInt(configuration.ActivePlaylistAllIndex) >= 0 ? parseInt(configuration.ActivePlaylistAllIndex) : -1
        active: contentMode === 8 && playlistsJson.length > 0 && activePlaylistIndex >= 0
        onApplyItem: function(item) {
            // Source properties are now derived reactively from currentItem —
            // no need to write to configuration.WE*Source here.
            log("playlist-all apply: " + (item.title || "?") + " type=" + (item.weType || "scene"))
        }
    }
    readonly property url activeVideoSource: wallpaperEngineVideoMode ? wallpaperEngineVideoSource : videoSource
    readonly property url activeWebSource: wallpaperEngineWebSource
    readonly property url activeSceneSource: wallpaperEngineSceneSource
    readonly property string activeSceneAssetsPath: joinPath(wallpaperEngineLibraryPath, "/steamapps/common/wallpaper_engine/assets")
    readonly property string videoEmptyMessage: wallpaperEngineVideoMode
        ? qsTr("Select a Wallpaper Engine video in the wallpaper settings.")
        : qsTr("Select a local video file in the wallpaper settings.")
    readonly property string webEmptyMessage: qsTr("Select a Wallpaper Engine web wallpaper in the wallpaper settings.")
    readonly property string sceneEmptyMessage: qsTr("Select a Wallpaper Engine scene wallpaper in the wallpaper settings.")
    property bool contentLoadFailed: false
    property string contentLoadErrorText: ""

    onPostEffectChanged: log("postEffect=" + postEffect + " active=" + postEffectActive)
    onVideoSourceChanged: log("videoSource=" + String(videoSource))
    onWallpaperEngineVideoSourceChanged: log("wallpaperEngineVideoSource=" + String(wallpaperEngineVideoSource))
    onWallpaperEngineWebSourceChanged: log("wallpaperEngineWebSource=" + String(wallpaperEngineWebSource))
    onWallpaperEngineSceneSourceChanged: log("wallpaperEngineSceneSource=" + String(wallpaperEngineSceneSource))
    onWallpaperEngineSceneExperimentalEnabledChanged: log("wallpaperEngineSceneExperimentalEnabled=" + wallpaperEngineSceneExperimentalEnabled)
    onWallpaperEngineSceneMouseInputChanged: log("wallpaperEngineSceneMouseInput=" + wallpaperEngineSceneMouseInput)
    onWallpaperEngineSceneMouseDiagnosticsEnabledChanged: log("wallpaperEngineSceneMouseDiagnosticsEnabled=" + wallpaperEngineSceneMouseDiagnosticsEnabled)
    onVideoFillModeChanged: log("videoFillMode=" + videoFillMode)
    onVideoMutedChanged: log("videoMuted=" + videoMuted)

    Loader {
        id: contentLoader
        anchors.fill: parent
        source: root.videoMode
            ? "VideoBackground.qml"
            : (root.webMode
                ? "WebBackground.qml"
                : (root.sceneNativeMode
                    ? "SceneGuard.qml"
                    : (root.sceneMode ? "ScenePlaceholder.qml" : "GradientBackground.qml")))

        onItemChanged: {
            if (item) {
                root.log("loader item changed, rebinding source=" + source)
                root.contentLoadFailed = false
                root.contentLoadErrorText = ""
                root.bindLoadedContent()
            }
        }

        onStatusChanged: {
            root.log("loader status=" + status + " source=" + source)

            if (status === Loader.Ready) {
                root.contentLoadFailed = false
                root.contentLoadErrorText = ""
                root.bindLoadedContent()
            } else if (status === Loader.Error) {
                root.contentLoadFailed = true
                root.contentLoadErrorText = root.videoMode
                    ? qsTr("Video mode could not be initialized in this Plasma session. Check QtMultimedia availability and the VM codec stack.")
                    : (root.webMode
                        ? qsTr("Wallpaper Engine Web mode could not be initialized in this Plasma session. Check QtWebEngine availability in the VM.")
                        : ((root.sceneMode || root.sceneNativeMode)
                            ? qsTr("Wallpaper Engine Scene mode could not be initialized in this Plasma session.")
                            : qsTr("The selected Yakkai content mode could not be initialized.")))
            } else if (status === Loader.Loading) {
                root.contentLoadFailed = false
                root.contentLoadErrorText = ""
            }
        }
    }

    function bindLoadedContent() {
        if (!contentLoader.item) {
            return
        }

        // Detect which component is loaded by checking for its unique properties.
        // This avoids race conditions with mode flags that depend on async config.
        const isVideo = "videoSource" in contentLoader.item
        const isWeb = "webSource" in contentLoader.item
        const isScene = "sceneSource" in contentLoader.item

        if (isVideo) {
            root.log("binding video content source=" + String(root.activeVideoSource)
                + " fill=" + root.videoFillMode
                + " muted=" + root.videoMuted)
            contentLoader.item.videoSource = Qt.binding(function() {
                return root.activeVideoSource
            })
            contentLoader.item.fillModeValue = Qt.binding(function() {
                return root.videoFillMode
            })
            contentLoader.item.muted = Qt.binding(function() {
                return root.videoMuted
            })
            contentLoader.item.emptyMessage = Qt.binding(function() {
                return root.videoEmptyMessage
            })
            contentLoader.item.verboseLogging = Qt.binding(function() {
                return root.verboseLogging
            })
            return
        }

        if (isWeb) {
            root.log("binding web content source=" + String(root.activeWebSource)
                + " muted=" + root.videoMuted
                + " title=" + root.wallpaperEngineWebProjectTitle)
            contentLoader.item.webSource = Qt.binding(function() {
                return root.activeWebSource
            })
            contentLoader.item.muted = Qt.binding(function() {
                return root.videoMuted
            })
            contentLoader.item.emptyMessage = Qt.binding(function() {
                return root.webEmptyMessage
            })
            contentLoader.item.projectTitle = Qt.binding(function() {
                return root.wallpaperEngineWebProjectTitle
            })
            contentLoader.item.userPropertiesJson = Qt.binding(function() {
                return root.wallpaperEngineWebPropertiesJson
            })
            return
        }

        if (isScene) {
            root.log("binding scene content source=" + String(root.activeSceneSource)
                + " title=" + root.wallpaperEngineSceneProjectTitle)
            contentLoader.item.sceneSource = Qt.binding(function() {
                return root.activeSceneSource
            })
            contentLoader.item.projectTitle = Qt.binding(function() {
                return root.wallpaperEngineSceneProjectTitle
            })
            contentLoader.item.sceneSourceKind = Qt.binding(function() {
                return root.wallpaperEngineSceneSourceKind
            })
            contentLoader.item.scenePropertiesJson = Qt.binding(function() {
                return root.wallpaperEngineScenePropertiesJson
            })
            contentLoader.item.assetsPath = Qt.binding(function() {
                return root.activeSceneAssetsPath
            })
            contentLoader.item.mouseInputEnabled = Qt.binding(function() {
                return root.wallpaperEngineSceneMouseInput
            })
            contentLoader.item.mouseDiagnosticsEnabled = Qt.binding(function() {
                return root.wallpaperEngineSceneMouseDiagnosticsEnabled
            })
            contentLoader.item.fillModeValue = Qt.binding(function() {
                return root.videoFillMode
            })
            contentLoader.item.muted = Qt.binding(function() {
                return root.videoMuted
            })
            contentLoader.item.emptyMessage = Qt.binding(function() {
                return root.sceneEmptyMessage
            })
            contentLoader.item.experimentalEnabled = Qt.binding(function() {
                return root.sceneNativeMode || root.contentMode === 7 || root.contentMode === 8
            })
            return
        }

        contentLoader.item.manualStartColor = Qt.binding(function() {
            return root.manualStartColor
        })
        contentLoader.item.manualEndColor = Qt.binding(function() {
            return root.manualEndColor
        })
        contentLoader.item.baseAngle = Qt.binding(function() {
            return root.baseAngle
        })
        contentLoader.item.animate = Qt.binding(function() {
            return root.animate
        })
        contentLoader.item.animationDuration = Qt.binding(function() {
            return root.animationDuration
        })
        contentLoader.item.driftDegrees = Qt.binding(function() {
            return root.driftDegrees
        })
        contentLoader.item.vignetteStrength = Qt.binding(function() {
            return root.vignetteStrength
        })
        contentLoader.item.useTimeOfDay = Qt.binding(function() {
            return root.useTimeOfDay
        })
        contentLoader.item.dayStartHour = Qt.binding(function() {
            return root.dayStartHour
        })
        contentLoader.item.nightStartHour = Qt.binding(function() {
            return root.nightStartHour
        })
        contentLoader.item.transitionMinutes = Qt.binding(function() {
            return root.transitionMinutes
        })
        contentLoader.item.dayStartColor = Qt.binding(function() {
            return root.dayStartColor
        })
        contentLoader.item.dayEndColor = Qt.binding(function() {
            return root.dayEndColor
        })
        contentLoader.item.nightStartColor = Qt.binding(function() {
            return root.nightStartColor
        })
        contentLoader.item.nightEndColor = Qt.binding(function() {
            return root.nightEndColor
        })
    }

    HoverHandler {
        id: sceneMouseDiagnosticHover
        enabled: root.sceneMouseDiagnosticsActive
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

        onPointChanged: {
            root.logSceneMouseDiagnostic(point.position.x, point.position.y, root.width, root.height)
        }
    }

    Component.onCompleted: {
        migrateSharedPlaylistSettings()
        log("wallpaper loaded contentMode=" + contentMode
            + " resolvedMediaSource=" + String(
                videoMode
                    ? activeVideoSource
                    : (webMode ? activeWebSource : ((sceneMode || sceneNativeMode) ? activeSceneSource : ""))
            ))
        playlistTryApply()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.75, 620)
        height: errorText.implicitHeight + 32
        radius: 10
        color: "#88000000"
        visible: root.contentLoadFailed
        z: 10
    }

    Text {
        id: errorText
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.65, 580)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        text: root.contentLoadErrorText
        visible: root.contentLoadFailed
        z: 11
    }

    // --- Post-processing shader pipeline ---
    // Loaded only when an effect is selected. Keeping ShaderEffectSource
    // permanently in the tree was capturing contentLoader to an FBO even
    // with live:false, breaking native Vulkan compositing.
    readonly property bool postEffectActive: postEffect !== "none"

    Loader {
        anchors.fill: parent
        active: root.postEffectActive
        sourceComponent: postEffectComponent
        z: 10
    }

    Component {
        id: postEffectComponent
        Item {
            anchors.fill: parent
            ShaderEffectSource {
                id: postEffectSource
                anchors.fill: parent
                sourceItem: contentLoader
                hideSource: true
                live: true
                visible: false
            }
            ShaderEffect {
                anchors.fill: parent
                property variant source: postEffectSource
                property real resWidth: width
                property real resHeight: height
                property real scanlineIntensity: root.crtScanline
                property real curvature: root.crtCurvature
                property real aberration: root.crtAberration
                property real vignetteStrength: root.crtVignette
                property real phosphorIntensity: root.crtPhosphor
                property real brightness: root.crtBrightness
                property real vibrance: root.crtVibrance
                property real zoom: root.crtZoom
                fragmentShader: root.postEffect === "crt" ? "shaders/crt.frag.qsb" : ""
            }
        }
    }
}
