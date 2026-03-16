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
    property int videoFillMode: configuration.VideoFillMode ?? 0
    property bool videoMuted: configuration.VideoMuted ?? true

    readonly property bool localVideoMode: contentMode === 1
    readonly property bool wallpaperEngineVideoMode: contentMode === 2
    readonly property bool videoMode: localVideoMode || wallpaperEngineVideoMode
    readonly property url activeVideoSource: wallpaperEngineVideoMode ? wallpaperEngineVideoSource : videoSource
    readonly property string videoEmptyMessage: wallpaperEngineVideoMode
        ? qsTr("Select a Wallpaper Engine video in the wallpaper settings.")
        : qsTr("Select a local video file in the wallpaper settings.")
    property bool contentLoadFailed: false
    property string contentLoadErrorText: ""

    onContentModeChanged: log("contentMode=" + contentMode + " videoMode=" + videoMode)
    onVideoSourceChanged: log("videoSource=" + String(videoSource))
    onWallpaperEngineVideoSourceChanged: log("wallpaperEngineVideoSource=" + String(wallpaperEngineVideoSource))
    onVideoFillModeChanged: log("videoFillMode=" + videoFillMode)
    onVideoMutedChanged: log("videoMuted=" + videoMuted)

    Loader {
        id: contentLoader
        anchors.fill: parent
        source: root.videoMode ? "VideoBackground.qml" : "GradientBackground.qml"

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
                    : qsTr("The selected Paper Gradient content mode could not be initialized.")
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
            + " resolvedVideoSource=" + String(activeVideoSource))
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
