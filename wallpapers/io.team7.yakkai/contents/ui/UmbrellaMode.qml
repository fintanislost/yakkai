/*
    SPDX-FileCopyrightText: 2026 Team7
    SPDX-License-Identifier: MIT

    Umbrella mode — unified wallpaper picker for all WE types.
    Manages type detection, source routing, and backend selection.
*/

import QtQuick

QtObject {
    id: umbrella

    // Inputs from config
    property string selectedType: "scene"  // persisted: scene/video/web
    property string sceneSource: ""
    property string videoSource: ""
    property string webSource: ""

    // Resolved backend type — trust the persisted selection directly
    readonly property string resolvedType: selectedType || "scene"

    readonly property bool isScene: resolvedType === "scene"
    readonly property bool isVideo: resolvedType === "video"
    readonly property bool isWeb: resolvedType === "web"
}
