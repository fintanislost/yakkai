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

    // Resolved backend type based on selected type + source availability
    readonly property string resolvedType: {
        // Trust the selected type if its source is set
        if (selectedType === "video" && videoSource.length > 0) return "video"
        if (selectedType === "web" && webSource.length > 0) return "web"
        if (selectedType === "scene" && sceneSource.length > 0) return "scene"
        // Fallback: detect from whichever source is available
        if (sceneSource.length > 0) return "scene"
        if (webSource.length > 0) return "web"
        if (videoSource.length > 0) return "video"
        return selectedType || "scene"
    }

    readonly property bool isScene: resolvedType === "scene"
    readonly property bool isVideo: resolvedType === "video"
    readonly property bool isWeb: resolvedType === "web"
}
