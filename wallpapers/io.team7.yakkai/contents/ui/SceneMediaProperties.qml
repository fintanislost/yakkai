/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQml

QtObject {
    property string scenePropertiesJson: "{}"
    property string mediaJson: "{}"
    property bool mediaIntegrationEnabled: false
    readonly property string mergedScenePropertiesJson: mergeSceneAndMedia(scenePropertiesJson, mediaJson, mediaIntegrationEnabled)

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

    function mergeSceneAndMedia(sceneJson, mediaPropertiesJson, integrationEnabled) {
        const merged = parseObject(sceneJson)
        delete merged.__yakkaiMedia

        if (!integrationEnabled) {
            return JSON.stringify(merged)
        }

        const media = parseObject(mediaPropertiesJson)
        if (media.__yakkaiMedia !== null
                && typeof media.__yakkaiMedia === "object"
                && !Array.isArray(media.__yakkaiMedia)) {
            merged.__yakkaiMedia = media.__yakkaiMedia
        }

        return JSON.stringify(merged)
    }
}
