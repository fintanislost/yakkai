/*
    SPDX-FileCopyrightText: 2026 Papercompany

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtMultimedia

Item {
    id: root

    property url videoSource: ""
    property string emptyMessage: qsTr("Select a local video file in the wallpaper settings.")
    property int fillModeValue: 0
    property bool muted: true
    property int stalledTicks: 0
    property int previousPosition: -1
    property int receivedFrameCount: 0
    readonly property string logPrefix: "[Yakkai]"

    readonly property bool hasRenderedFrame: receivedFrameCount > 0
    readonly property string mediaStatusLabel: {
        switch (player.mediaStatus) {
        case MediaPlayer.NoMedia:
            return qsTr("No media")
        case MediaPlayer.LoadingMedia:
            return qsTr("Loading")
        case MediaPlayer.LoadedMedia:
            return qsTr("Loaded")
        case MediaPlayer.StalledMedia:
            return qsTr("Stalled")
        case MediaPlayer.BufferingMedia:
            return qsTr("Buffering")
        case MediaPlayer.BufferedMedia:
            return qsTr("Buffered")
        case MediaPlayer.EndOfMedia:
            return qsTr("End of media")
        case MediaPlayer.InvalidMedia:
            return qsTr("Invalid media")
        default:
            return qsTr("Unknown")
        }
    }
    readonly property string playbackStateLabel: {
        switch (player.playbackState) {
        case MediaPlayer.PlayingState:
            return qsTr("Playing")
        case MediaPlayer.PausedState:
            return qsTr("Paused")
        case MediaPlayer.StoppedState:
            return qsTr("Stopped")
        default:
            return qsTr("Unknown")
        }
    }
    readonly property string diagnosticText: {
        if (videoSource.toString().length === 0 || (statusText.length === 0 && hasRenderedFrame)) {
            return ""
        }

        const seconds = (player.position / 1000).toFixed(1)
        return qsTr("Status: %1 | Playback: %2 | Position: %3s | Frames: %4 | Has video: %5")
            .arg(mediaStatusLabel)
            .arg(playbackStateLabel)
            .arg(seconds)
            .arg(receivedFrameCount)
            .arg(player.hasVideo ? qsTr("yes") : qsTr("no"))
    }

    readonly property string statusText: {
        if (videoSource.toString().length === 0) {
            return emptyMessage
        }

        if (player.mediaStatus === MediaPlayer.NoMedia) {
            return qsTr("Waiting for QtMultimedia to load the selected video…")
        }

        if (player.mediaStatus === MediaPlayer.LoadingMedia || player.mediaStatus === MediaPlayer.BufferingMedia) {
            return qsTr("Loading video…")
        }

        if (player.error !== MediaPlayer.NoError) {
            return player.errorString.length > 0
                ? player.errorString
                : qsTr("This video could not be played in the current Plasma session.")
        }

        if (player.mediaStatus === MediaPlayer.InvalidMedia) {
            return qsTr("This file was recognized as invalid media by QtMultimedia.")
        }

        if ((player.mediaStatus === MediaPlayer.LoadedMedia || player.mediaStatus === MediaPlayer.BufferedMedia) && !player.hasVideo) {
            return qsTr("QtMultimedia loaded the file but did not expose a video track to the wallpaper.")
        }

        if (player.mediaStatus === MediaPlayer.StalledMedia) {
            return qsTr("Video playback stalled before any frames reached the wallpaper.")
        }

        if (!hasRenderedFrame && stalledTicks >= 8 && player.playbackState === MediaPlayer.PlayingState && player.position > 0) {
            return qsTr("Playback time is advancing, but no video frames are reaching the wallpaper. This points to a QtMultimedia decode or render issue in the VM.")
        }

        if (!hasRenderedFrame && stalledTicks >= 8 && player.playbackState === MediaPlayer.PlayingState) {
            return qsTr("The video started, but the first decoded frame has not arrived yet.")
        }

        if (!hasRenderedFrame && (player.mediaStatus === MediaPlayer.LoadedMedia || player.mediaStatus === MediaPlayer.BufferedMedia)) {
            return qsTr("The video is loaded. Waiting for the first decoded frame…")
        }

        return ""
    }

    readonly property int resolvedFillMode: {
        switch (fillModeValue) {
        case 1:
            return VideoOutput.PreserveAspectFit
        case 2:
            return VideoOutput.Stretch
        default:
            return VideoOutput.PreserveAspectCrop
        }
    }

    function log(message) {
        console.log(logPrefix + " " + message)
    }

    function logState(reason) {
        log(reason
            + " source=" + String(videoSource)
            + " playerSource=" + String(player.source)
            + " mediaStatus=" + mediaStatusLabel
            + " playbackState=" + playbackStateLabel
            + " position=" + player.position
            + " frames=" + receivedFrameCount
            + " hasVideo=" + player.hasVideo
            + " error=" + player.error
            + " errorString=" + player.errorString)
    }

    function syncPlayback() {
        const sourceText = videoSource.toString()

        log("syncPlayback source=" + sourceText)

        if (sourceText.length === 0) {
            player.stop()
            player.source = ""
            stalledTicks = 0
            previousPosition = -1
            receivedFrameCount = 0
            logState("cleared video source")
            return
        }

        if (player.source.toString() !== sourceText) {
            player.stop()
            player.source = ""
            stalledTicks = 0
            previousPosition = -1
            receivedFrameCount = 0
            player.source = videoSource
            logState("assigned player source")
        }

        player.play()
        logState("requested play")
    }

    onVideoSourceChanged: syncPlayback()
    onFillModeValueChanged: log("fillModeValue=" + fillModeValue + " resolvedFillMode=" + resolvedFillMode)
    onMutedChanged: log("muted=" + muted)

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: root.resolvedFillMode
    }

    Connections {
        target: videoOutput.videoSink
        ignoreUnknownSignals: true

        function onVideoFrameChanged() {
            root.receivedFrameCount += 1
            root.stalledTicks = 0

            if (root.receivedFrameCount <= 3 || root.receivedFrameCount === 10) {
                root.logState("videoFrameChanged")
            }
        }
    }

    AudioOutput {
        id: audioOutput
        muted: root.muted
        volume: 1.0
    }

    MediaPlayer {
        id: player
        loops: MediaPlayer.Infinite
        audioOutput: audioOutput
        videoOutput: videoOutput

        onSourceChanged: root.logState("player source changed")
        onPlaybackStateChanged: root.logState("playbackStateChanged")
        onMediaStatusChanged: {
            root.logState("mediaStatusChanged")

            if (source.toString().length > 0
                    && (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                    && playbackState !== MediaPlayer.PlayingState) {
                root.log("media became ready, retrying play")
                play()
            }
        }
        onPositionChanged: {
            if (position > 0 && position % 1000 < 120) {
                root.logState("positionChanged")
            }
        }
        onHasVideoChanged: root.logState("hasVideoChanged")
        onErrorOccurred: function(error, errorString) {
            root.log("errorOccurred error=" + error + " errorString=" + errorString)
        }
    }

    Timer {
        interval: 500
        repeat: true
        running: root.videoSource.toString().length > 0

        onTriggered: {
            const currentPosition = player.position
            const positionAdvanced = root.previousPosition >= 0 && currentPosition > root.previousPosition

            if (root.hasRenderedFrame) {
                root.stalledTicks = 0
            } else if (player.playbackState === MediaPlayer.PlayingState
                    || player.mediaStatus === MediaPlayer.LoadedMedia
                    || player.mediaStatus === MediaPlayer.BufferedMedia
                    || player.mediaStatus === MediaPlayer.StalledMedia) {
                root.stalledTicks += 1
            }

            if (positionAdvanced && !root.hasRenderedFrame) {
                root.stalledTicks = Math.max(root.stalledTicks, 4)
            }

            if (!root.hasRenderedFrame && (root.stalledTicks === 4 || root.stalledTicks === 8 || root.stalledTicks === 12)) {
                root.logState("stallTick=" + root.stalledTicks)
            }

            root.previousPosition = currentPosition
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.7, 560)
        height: statusColumn.implicitHeight + 32
        radius: 10
        color: "#66000000"
        visible: statusColumn.visible
    }

    Column {
        id: statusColumn
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.6, 520)
        spacing: 6
        visible: statusTextLabel.visible || diagnosticLabel.visible
    }

    Text {
        id: statusTextLabel
        width: statusColumn.width
        parent: statusColumn
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        text: root.statusText
        visible: text.length > 0
    }

    Text {
        id: diagnosticLabel
        width: statusColumn.width
        parent: statusColumn
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#d0d0d0"
        font.pixelSize: Math.max(11, statusTextLabel.font.pixelSize - 1)
        text: root.diagnosticText
        visible: text.length > 0
    }

    Component.onCompleted: {
        log("video component completed")
        syncPlayback()
        logState("component completed")
    }
}
