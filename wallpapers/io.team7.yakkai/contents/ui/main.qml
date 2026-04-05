/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root

    readonly property string logPrefix: "[Yakkai]"

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
    property string playlistsJson: configuration.PlaylistsJson ?? ""
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

    property int contentMode: configuration.ContentMode ?? 0
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
    property url wallpaperEngineVideoSource: resolvedVideoSource(configuration.WEVideoSource ?? "")
    property string wallpaperEngineVideoProjectTitle: configuration.WEVideoProjectTitle ?? ""
    property url wallpaperEngineWebSource: resolvedVideoSource(configuration.WEWebSource ?? "")
    property string wallpaperEngineWebProjectTitle: configuration.WEWebProjectTitle ?? ""
    property string wallpaperEngineWebPropertiesJson: configuration.WEWebPropertiesJson ?? ""
    property url wallpaperEngineSceneSource: resolvedVideoSource(configuration.WESceneSource ?? "")
    property string wallpaperEngineSceneProjectTitle: configuration.WESceneProjectTitle ?? ""
    property string wallpaperEngineSceneSourceKind: configuration.WESceneSourceKind ?? ""
    property string wallpaperEngineScenePropertiesJson: configuration.WEScenePropertiesJson ?? ""
    property bool wallpaperEngineSceneExperimentalEnabled: configuration.WESceneExperimentalEnabled ?? false
    property bool wallpaperEngineSceneMouseInput: configuration.WESceneMouseInput ?? false
    property string wallpaperEngineLibraryPath: configuration.WEVideoLibraryPath ?? ""
    property int videoFillMode: configuration.VideoFillMode ?? 0
    property bool videoMuted: configuration.VideoMuted ?? true

    readonly property bool localVideoMode: contentMode === 1
    readonly property bool wallpaperEngineVideoMode: contentMode === 2 || (contentMode === 7 && umbrellaResolvedType === "video")
    readonly property bool wallpaperEngineWebMode: contentMode === 3 || (contentMode === 7 && umbrellaResolvedType === "web")
    readonly property bool legacySceneNativeMode: contentMode === 4 && wallpaperEngineSceneExperimentalEnabled
    readonly property bool wallpaperEngineSceneMode: contentMode === 4 && !legacySceneNativeMode
    readonly property bool wallpaperEngineSceneNativeMode: contentMode === 5 || legacySceneNativeMode || contentMode === 6 || (contentMode === 7 && umbrellaResolvedType === "scene")
    readonly property bool playlistMode_: contentMode === 6
    readonly property bool videoMode: localVideoMode || wallpaperEngineVideoMode
    readonly property bool webMode: wallpaperEngineWebMode
    readonly property bool sceneMode: wallpaperEngineSceneMode
    readonly property bool sceneNativeMode: wallpaperEngineSceneNativeMode

    UmbrellaMode {
        id: umbrellaMode
        selectedType: configuration.UmbrellaSelectedType || "scene"
        sceneSource: wallpaperEngineSceneSource.toString()
        videoSource: wallpaperEngineVideoSource.toString()
        webSource: wallpaperEngineWebSource.toString()
    }
    readonly property string umbrellaResolvedType: contentMode === 7 ? umbrellaMode.resolvedType : ""
    onUmbrellaResolvedTypeChanged: {
        if (contentMode === 7) {
            log("umbrella resolved type: " + umbrellaResolvedType)
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

    onVideoSourceChanged: log("videoSource=" + String(videoSource))
    onWallpaperEngineVideoSourceChanged: log("wallpaperEngineVideoSource=" + String(wallpaperEngineVideoSource))
    onWallpaperEngineWebSourceChanged: log("wallpaperEngineWebSource=" + String(wallpaperEngineWebSource))
    onWallpaperEngineSceneSourceChanged: log("wallpaperEngineSceneSource=" + String(wallpaperEngineSceneSource))
    onWallpaperEngineSceneExperimentalEnabledChanged: log("wallpaperEngineSceneExperimentalEnabled=" + wallpaperEngineSceneExperimentalEnabled)
    onWallpaperEngineSceneMouseInputChanged: log("wallpaperEngineSceneMouseInput=" + wallpaperEngineSceneMouseInput)
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

        if (root.videoMode) {
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
            return
        }

        if (root.webMode) {
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

        if (root.sceneMode || root.sceneNativeMode) {
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
                return root.sceneNativeMode
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

    Component.onCompleted: {
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
}
