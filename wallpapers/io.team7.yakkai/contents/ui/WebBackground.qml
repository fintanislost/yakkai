/*
    SPDX-FileCopyrightText: 2026 Team7

    SPDX-License-Identifier: MIT
*/

import QtQuick
import QtWebEngine

Item {
    id: root

    property url webSource: ""
    property bool muted: true
    property string emptyMessage: qsTr("Select a Wallpaper Engine web wallpaper in the wallpaper settings.")
    property string projectTitle: ""
    property string userPropertiesJson: "{}"
    property bool pageLoaded: false
    property bool propertiesSent: false
    property string loadErrorText: ""
    readonly property string logPrefix: "[Yakkai]"
    readonly property var generalProperties: ({ fps: 24 })
    readonly property var parsedUserProperties: parseUserProperties(userPropertiesJson)
    readonly property string statusText: {
        if (webSource.toString().length === 0) {
            return emptyMessage
        }

        if (loadErrorText.length > 0) {
            return loadErrorText
        }

        if (webView.loading) {
            return qsTr("Loading Wallpaper Engine web wallpaper…")
        }

        if (!pageLoaded) {
            return qsTr("Waiting for the Wallpaper Engine page to finish loading…")
        }

        return ""
    }
    readonly property string diagnosticText: {
        if (statusText.length === 0 && pageLoaded) {
            return ""
        }

        return qsTr("Title: %1 | URL: %2 | Properties sent: %3")
            .arg(projectTitle.length > 0 ? projectTitle : qsTr("Unknown"))
            .arg(String(webSource))
            .arg(propertiesSent ? qsTr("yes") : qsTr("no"))
    }

    function log(message) {
        console.log(logPrefix + " " + message)
    }

    function parseUserProperties(source) {
        const text = String(source ?? "")

        if (text.length === 0) {
            return {}
        }

        try {
            const parsed = JSON.parse(text)
            return parsed && typeof parsed === "object" ? parsed : {}
        } catch (error) {
            log("failed to parse WE web properties JSON: " + error)
            return {}
        }
    }

    function pushProperties() {
        if (!pageLoaded) {
            return
        }

        const generalJson = JSON.stringify(generalProperties)
        const userJson = JSON.stringify(parsedUserProperties)
        const script = `
            (function() {
                if (!window.__paperGradientSetProperties) {
                    return false;
                }

                window.__paperGradientSetProperties(${generalJson}, ${userJson});
                return true;
            })();
        `

        webView.runJavaScript(script, function(result) {
            propertiesSent = result === true

            if (propertiesSent) {
                log("sent WE web property payload for " + projectTitle)
            } else {
                log("WE web property shim was not ready when payload was sent")
            }
        })
    }

    onWebSourceChanged: {
        pageLoaded = false
        propertiesSent = false
        loadErrorText = ""
        log("webSource=" + String(webSource))
    }
    onUserPropertiesJsonChanged: {
        propertiesSent = false

        if (pageLoaded) {
            pushProperties()
        }
    }
    onMutedChanged: log("web audio muted=" + muted)

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    WebEngineView {
        id: webView
        anchors.fill: parent
        url: root.webSource
        audioMuted: root.muted
        activeFocusOnPress: false

        Component.onCompleted: {
            settings.fullscreenSupportEnabled = true
            settings.autoLoadIconsForPage = false
            settings.printElementBackgrounds = false
            settings.playbackRequiresUserGesture = false
            settings.pdfViewerEnabled = false
            settings.showScrollBars = false
            settings.localContentCanAccessRemoteUrls = true
            settings.webGLEnabled = true
            settings.accelerated2dCanvasEnabled = true
            settings.allowGeolocationOnInsecureOrigins = true

            userScripts.insert([
                {
                    injectionPoint: WebEngineScript.DocumentCreation,
                    worldId: WebEngineScript.MainWorld,
                    name: "PaperGradientCompat",
                    sourceCode: `
                        (function() {
                            function createSignal() {
                                const listeners = [];

                                return {
                                    connect: function(listener) {
                                        if (typeof listener === "function" && listeners.indexOf(listener) === -1) {
                                            listeners.push(listener);
                                        }
                                    },
                                    disconnect: function(listener) {
                                        const index = listeners.indexOf(listener);
                                        if (index >= 0) {
                                            listeners.splice(index, 1);
                                        }
                                    },
                                    emit: function(payload) {
                                        listeners.slice().forEach(function(listener) {
                                            try {
                                                listener(payload);
                                            } catch (error) {
                                                console.error("[Yakkai] WE web listener failed", error);
                                            }
                                        });
                                    }
                                };
                            }

                            const signalGeneral = createSignal();
                            const signalUser = createSignal();
                            const signalAudio = createSignal();

                            if (!window.wpeQml) {
                                window.wpeQml = {
                                    loaded: true,
                                    sigGeneralProperties: signalGeneral,
                                    sigUserProperties: signalUser,
                                    sigAudio: signalAudio
                                };
                            } else {
                                window.wpeQml.loaded = true;
                                if (!window.wpeQml.sigGeneralProperties) {
                                    window.wpeQml.sigGeneralProperties = signalGeneral;
                                }
                                if (!window.wpeQml.sigUserProperties) {
                                    window.wpeQml.sigUserProperties = signalUser;
                                }
                                if (!window.wpeQml.sigAudio) {
                                    window.wpeQml.sigAudio = signalAudio;
                                }
                            }

                            window.__paperGradientLatestProperties = {
                                general: {},
                                user: {}
                            };

                            window.wallpaperRegisterAudioListener = function(listener) {
                                window.wpeQml.sigAudio.connect(listener);
                            };

                            function deliverToPropertyListener() {
                                const propertyListener = window.wallpaperPropertyListener;
                                if (!propertyListener) {
                                    return false;
                                }

                                if (propertyListener.applyGeneralProperties) {
                                    propertyListener.applyGeneralProperties(window.__paperGradientLatestProperties.general);
                                }

                                if (propertyListener.applyUserProperties) {
                                    propertyListener.applyUserProperties(window.__paperGradientLatestProperties.user);
                                }

                                return true;
                            }

                            window.__paperGradientSetProperties = function(generalProperties, userProperties) {
                                window.__paperGradientLatestProperties.general = generalProperties || {};
                                window.__paperGradientLatestProperties.user = userProperties || {};
                                window.wpeQml.sigGeneralProperties.emit(window.__paperGradientLatestProperties.general);
                                window.wpeQml.sigUserProperties.emit(window.__paperGradientLatestProperties.user);
                                return deliverToPropertyListener();
                            };

                            let bindTries = 0;
                            const bindTimer = setInterval(function() {
                                bindTries += 1;
                                if (deliverToPropertyListener() || bindTries >= 40) {
                                    clearInterval(bindTimer);
                                }
                            }, 250);

                            document.addEventListener("DOMContentLoaded", function() {
                                deliverToPropertyListener();
                            }, { once: true });

                            if (document.body) {
                                document.body.ondragstart = function() { return false; };
                            } else {
                                document.addEventListener("DOMContentLoaded", function() {
                                    if (document.body) {
                                        document.body.ondragstart = function() { return false; };
                                    }
                                }, { once: true });
                            }
                        })();
                    `
                }
            ])

            root.log("WE web component completed webgl="
                + settings.webGLEnabled
                + " canvas2d="
                + settings.accelerated2dCanvasEnabled)
        }

        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebEngineView.LoadStartedStatus) {
                root.pageLoaded = false
                root.propertiesSent = false
                root.loadErrorText = ""
                root.log("WE web loading started url=" + String(root.webSource))
                return
            }

            if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
                root.pageLoaded = true
                root.loadErrorText = ""
                root.log("WE web load succeeded title=" + root.projectTitle)
                root.pushProperties()
                return
            }

            if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                root.pageLoaded = false
                root.propertiesSent = false
                root.loadErrorText = loadRequest.errorString && loadRequest.errorString.length > 0
                    ? loadRequest.errorString
                    : qsTr("QtWebEngine could not load this Wallpaper Engine web wallpaper.")
                root.log("WE web load failed error=" + root.loadErrorText)
            }
        }

        onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
            root.log("WE web console level=" + level + " source=" + sourceID + ":" + lineNumber + " message=" + message)
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.72, 620)
        height: statusColumn.implicitHeight + 32
        radius: 10
        color: "#66000000"
        visible: statusColumn.visible
    }

    Column {
        id: statusColumn
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.62, 580)
        spacing: 6
        visible: statusTextLabel.visible || diagnosticLabel.visible
    }

    Text {
        id: statusTextLabel
        parent: statusColumn
        width: statusColumn.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        text: root.statusText
        visible: text.length > 0
    }

    Text {
        id: diagnosticLabel
        parent: statusColumn
        width: statusColumn.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#d0d0d0"
        font.pixelSize: Math.max(11, statusTextLabel.font.pixelSize - 1)
        text: root.diagnosticText
        visible: text.length > 0
    }
}
