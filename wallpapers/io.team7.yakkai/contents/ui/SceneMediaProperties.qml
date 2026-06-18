/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQml

QtObject {
    property string scenePropertiesJson: "{}"
    property string mediaJson: "{}"
    property string runtimeMediaJson: "{}"
    property bool mediaIntegrationEnabled: false
    readonly property string mergedScenePropertiesJson: mergeSceneAndMedia(scenePropertiesJson, mediaJson, mediaIntegrationEnabled)
    readonly property string mergedRuntimeMediaJson: mediaIntegrationEnabled
        ? sanitizeMediaJson(runtimeMediaJson)
        : "{}"

    function parseObject(json) {
        try {
            const parsed = JSON.parse(json)
            if (parsed !== null && typeof parsed === "object" && !Array.isArray(parsed)) {
                return parsed
            }
        } catch (error) {
        }
        return {}
    }

    function hasMediaObject(payload) {
        return payload.__yakkaiMedia !== null
            && typeof payload.__yakkaiMedia === "object"
            && !Array.isArray(payload.__yakkaiMedia)
    }

    function sanitizeMediaJson(mediaPropertiesJson) {
        const media = parseObject(mediaPropertiesJson)
        if (hasMediaObject(media)) {
            return JSON.stringify({ "__yakkaiMedia": media.__yakkaiMedia })
        }

        return "{}"
    }

    function mergeSceneAndMedia(sceneJson, mediaPropertiesJson, integrationEnabled) {
        const merged = parseObject(sceneJson)
        delete merged.__yakkaiMedia

        if (!integrationEnabled) {
            return JSON.stringify(merged)
        }

        const media = parseObject(mediaPropertiesJson)
        if (hasMediaObject(media)) {
            merged.__yakkaiMedia = media.__yakkaiMedia
        }

        return JSON.stringify(merged)
    }
}
