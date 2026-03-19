/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root

    readonly property string logPrefix: "[Paper Gradient]"

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
    property bool wallpaperEngineSceneExperimentalEnabled: configuration.WESceneExperimentalEnabled ?? false
    property bool wallpaperEngineSceneMouseInput: configuration.WESceneMouseInput ?? false
    property string wallpaperEngineLibraryPath: configuration.WEVideoLibraryPath ?? ""
    property int videoFillMode: configuration.VideoFillMode ?? 0
    property bool videoMuted: configuration.VideoMuted ?? true

    readonly property bool localVideoMode: contentMode === 1
    readonly property bool wallpaperEngineVideoMode: contentMode === 2
    readonly property bool wallpaperEngineWebMode: contentMode === 3
    readonly property bool legacySceneNativeMode: contentMode === 4 && wallpaperEngineSceneExperimentalEnabled
    readonly property bool wallpaperEngineSceneMode: contentMode === 4 && !legacySceneNativeMode
    readonly property bool wallpaperEngineSceneNativeMode: contentMode === 5 || legacySceneNativeMode
    readonly property bool videoMode: localVideoMode || wallpaperEngineVideoMode
    readonly property bool webMode: wallpaperEngineWebMode
    readonly property bool sceneMode: wallpaperEngineSceneMode
    readonly property bool sceneNativeMode: wallpaperEngineSceneNativeMode
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

    onContentModeChanged: log("contentMode=" + contentMode + " videoMode=" + videoMode + " webMode=" + webMode + " sceneMode=" + sceneMode + " sceneNativeMode=" + sceneNativeMode)
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
                            : qsTr("The selected Paper Gradient content mode could not be initialized.")))
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
