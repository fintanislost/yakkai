#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace
{
constexpr int MaxCaptureSequenceFrames = 3600;
constexpr int CaptureExitDrainMs = 500;

struct CaptureRequest
{
    int timeMs = 0;
    QString fileName;
};

enum CaptureStatus
{
    CaptureSuccess = 0,
    CaptureNullImage = 4,
    CaptureSaveFailed = 5,
};

std::optional<int> parseNonNegativeInt(const QString& value)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed < 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::vector<CaptureRequest>> parseCaptureTimes(const QString& value)
{
    std::vector<CaptureRequest> captures;
    const QStringList parts = value.split(',', Qt::KeepEmptyParts);
    for (const QString& rawPart : parts) {
        const QString trimmed = rawPart.trimmed();
        if (trimmed.isEmpty()) {
            return std::nullopt;
        }
        const std::optional<int> parsed = parseNonNegativeInt(trimmed);
        if (!parsed) {
            return std::nullopt;
        }
        captures.push_back(CaptureRequest{*parsed, QStringLiteral("frame-%1ms.png").arg(*parsed, 8, 10, QLatin1Char('0'))});
    }
    std::sort(captures.begin(), captures.end(), [](const CaptureRequest& left, const CaptureRequest& right) {
        return left.timeMs < right.timeMs;
    });
    const auto duplicate = std::adjacent_find(captures.begin(), captures.end(), [](const CaptureRequest& left, const CaptureRequest& right) {
        return left.timeMs == right.timeMs;
    });
    if (duplicate != captures.end()) {
        return std::nullopt;
    }
    return captures;
}

std::optional<std::vector<CaptureRequest>> parseCaptureSequence(const QString& value)
{
    const QStringList parts = value.split(':');
    if (parts.size() != 3) {
        return std::nullopt;
    }

    const std::optional<int> startMs = parseNonNegativeInt(parts.at(0).trimmed());
    const std::optional<int> frameCount = parseNonNegativeInt(parts.at(1).trimmed());
    const std::optional<int> intervalMs = parseNonNegativeInt(parts.at(2).trimmed());
    if (!startMs || !frameCount || !intervalMs || *frameCount == 0 || *intervalMs == 0) {
        return std::nullopt;
    }
    if (*frameCount > MaxCaptureSequenceFrames) {
        return std::nullopt;
    }

    std::vector<CaptureRequest> captures;
    captures.reserve(static_cast<size_t>(*frameCount));
    for (int index = 0; index < *frameCount; ++index) {
        const qint64 timeMs64 = static_cast<qint64>(*startMs) + static_cast<qint64>(index) * static_cast<qint64>(*intervalMs);
        if (timeMs64 > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        const int timeMs = static_cast<int>(timeMs64);
        captures.push_back(CaptureRequest{timeMs, QStringLiteral("frame-%1.png").arg(index, 4, 10, QLatin1Char('0'))});
    }
    return captures;
}

std::optional<QSize> parseWindowSize(const QString& value)
{
    const QStringList parts = value.trimmed().toLower().split('x', Qt::KeepEmptyParts);
    if (parts.size() != 2) {
        return std::nullopt;
    }

    const std::optional<int> width = parseNonNegativeInt(parts.at(0).trimmed());
    const std::optional<int> height = parseNonNegativeInt(parts.at(1).trimmed());
    if (!width || !height || *width == 0 || *height == 0) {
        return std::nullopt;
    }
    return QSize(*width, *height);
}

std::optional<QString> parsePositiveIdList(const QString& value)
{
    std::vector<int> ids;
    const QStringList parts = value.split(',', Qt::KeepEmptyParts);
    for (const QString& rawPart : parts) {
        const QString trimmed = rawPart.trimmed();
        if (trimmed.isEmpty()) {
            return std::nullopt;
        }
        const std::optional<int> parsed = parseNonNegativeInt(trimmed);
        if (!parsed || *parsed == 0) {
            return std::nullopt;
        }
        if (std::find(ids.begin(), ids.end(), *parsed) == ids.end()) {
            ids.push_back(*parsed);
        }
    }
    if (ids.empty()) {
        return std::nullopt;
    }

    QStringList normalized;
    for (int id : ids) {
        normalized.push_back(QString::number(id));
    }
    return normalized.join(QLatin1Char(','));
}

CaptureStatus saveWindowCapture(QQuickWindow* window, const QString& absolutePath)
{
    const QImage image = window->grabWindow();
    qInfo() << "yakkai_scene_harness: capture size=" << image.size()
            << "devicePixelRatio=" << image.devicePixelRatio()
            << "path=" << absolutePath;
    if (image.isNull()) {
        qWarning() << "yakkai_scene_harness: capture image is null";
        return CaptureNullImage;
    }

    if (!image.save(absolutePath)) {
        qWarning() << "yakkai_scene_harness: failed to save capture to" << absolutePath;
        return CaptureSaveFailed;
    }

    return CaptureSuccess;
}

void requestCaptureExit(QQuickWindow* window, int status)
{
    bool invokedShutdownHook = false;
    if (window != nullptr) {
        invokedShutdownHook = QMetaObject::invokeMethod(window, "prepareForCaptureExit", Qt::DirectConnection);
    }

    qInfo() << "yakkai_scene_harness: capture exit requested status=" << status
            << "shutdownHook=" << invokedShutdownHook
            << "drainMs=" << CaptureExitDrainMs;

    QTimer::singleShot(CaptureExitDrainMs, QCoreApplication::instance(), [status]() {
        QCoreApplication::exit(status);
    });
}

int fillModeFromString(const QString& fillMode)
{
    if (fillMode == QStringLiteral("fit")) {
        return 1;
    }

    if (fillMode == QStringLiteral("stretch")) {
        return 2;
    }

    return 0;
}

QString backendQmlFile(const QString& qmlDir, const QString& backend)
{
    if (backend == QStringLiteral("paper")) {
        return QDir(qmlDir).filePath(QStringLiteral("YakkaiSceneViewerHarness.qml"));
    }

    return QDir(qmlDir).filePath(QStringLiteral("SystemSceneViewerHarness.qml"));
}
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("yakkai_scene_harness"));
    QCoreApplication::setOrganizationName(QStringLiteral("Team7"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone Yakkai scene harness"));
    parser.addHelpOption();

    QCommandLineOption backendOption(
        QStringList{QStringLiteral("backend")},
        QStringLiteral("Select the backend to load: system or paper."),
        QStringLiteral("backend"),
        QStringLiteral("system")
    );
    QCommandLineOption sourceOption(
        QStringList{QStringLiteral("source")},
        QStringLiteral("Absolute path to scene.json or scene.pkg."),
        QStringLiteral("path")
    );
    QCommandLineOption assetsOption(
        QStringList{QStringLiteral("assets")},
        QStringLiteral("Absolute path to wallpaper_engine/assets."),
        QStringLiteral("path")
    );
    QCommandLineOption fillOption(
        QStringList{QStringLiteral("fill")},
        QStringLiteral("Initial fill mode: crop, fit, or stretch."),
        QStringLiteral("mode"),
        QStringLiteral("crop")
    );
    QCommandLineOption windowSizeOption(
        QStringList{QStringLiteral("window-size")},
        QStringLiteral("Harness window size WIDTHxHEIGHT."),
        QStringLiteral("size"),
        QStringLiteral("1600x900")
    );
    QCommandLineOption mouseOption(
        QStringList{QStringLiteral("mouse")},
        QStringLiteral("Enable mouse and hover input.")
    );
    QCommandLineOption unmutedOption(
        QStringList{QStringLiteral("unmuted")},
        QStringLiteral("Start with audio enabled.")
    );
    QCommandLineOption hideInfoOverlayOption(
        QStringList{QStringLiteral("hide-info-overlay")},
        QStringLiteral("Hide the harness info overlay.")
    );
    QCommandLineOption captureOption(
        QStringList{QStringLiteral("capture")},
        QStringLiteral("Save a window capture to the given path, then exit."),
        QStringLiteral("path")
    );
    QCommandLineOption captureDelayOption(
        QStringList{QStringLiteral("capture-delay-ms")},
        QStringLiteral("Delay before capturing the window."),
        QStringLiteral("ms"),
        QStringLiteral("2500")
    );
    QCommandLineOption captureDirOption(
        QStringList{QStringLiteral("capture-dir")},
        QStringLiteral("Save multiple window captures into the given directory."),
        QStringLiteral("path")
    );
    QCommandLineOption captureTimesOption(
        QStringList{QStringLiteral("capture-times-ms")},
        QStringLiteral("Comma-separated capture timestamps in milliseconds, used with --capture-dir."),
        QStringLiteral("times")
    );
    QCommandLineOption captureSequenceOption(
        QStringList{QStringLiteral("capture-sequence")},
        QStringLiteral("Capture sequence START_MS:FRAME_COUNT:INTERVAL_MS, used with --capture-dir."),
        QStringLiteral("sequence")
    );
    QCommandLineOption debugEffectCapturesOption(
        QStringList{QStringLiteral("debug-effect-captures")},
        QStringLiteral("Write effect input/output/final-publish debug captures and manifest into the given directory. Only supported by --backend paper."),
        QStringLiteral("path")
    );
    QCommandLineOption debugEffectProbeLayersOption(
        QStringList{QStringLiteral("debug-effect-probe-layers")},
        QStringLiteral("Layer IDs whose stripped puppet mixed effect chains should be rendered only for debug capture. Requires --debug-effect-captures."),
        QStringLiteral("ids")
    );

    parser.addOption(backendOption);
    parser.addOption(sourceOption);
    parser.addOption(assetsOption);
    parser.addOption(fillOption);
    parser.addOption(windowSizeOption);
    parser.addOption(mouseOption);
    parser.addOption(unmutedOption);
    parser.addOption(hideInfoOverlayOption);
    parser.addOption(captureOption);
    parser.addOption(captureDelayOption);
    parser.addOption(captureDirOption);
    parser.addOption(captureTimesOption);
    parser.addOption(captureSequenceOption);
    parser.addOption(debugEffectCapturesOption);
    parser.addOption(debugEffectProbeLayersOption);
    parser.process(app);

    const QString qmlDir = QStringLiteral(YAKKAI_SCENE_HARNESS_QML_DIR);
    const QString buildQmlImportDir = QStringLiteral(YAKKAI_SCENE_HARNESS_BUILD_QML_IMPORT_DIR);
    const QString backend = parser.value(backendOption).trimmed().toLower();
    const QString rawSourcePath = parser.value(sourceOption).trimmed();
    const QString rawAssetsPath = parser.value(assetsOption).trimmed();
    const QString sourcePath = rawSourcePath.isEmpty() ? QString() : QFileInfo(rawSourcePath).absoluteFilePath();
    const QString assetsPath = rawAssetsPath.isEmpty() ? QString() : QFileInfo(rawAssetsPath).absoluteFilePath();
    const QString fillMode = parser.value(fillOption).trimmed().toLower();
    const QString windowSizeValue = parser.value(windowSizeOption).trimmed();
    const QString capturePath = parser.value(captureOption).trimmed();
    const int captureDelayMs = parser.value(captureDelayOption).toInt();
    const QString captureDirPath = parser.value(captureDirOption).trimmed();
    const QString captureTimesValue = parser.value(captureTimesOption).trimmed();
    const QString captureSequenceValue = parser.value(captureSequenceOption).trimmed();
    const QString debugEffectCapturesPath = parser.value(debugEffectCapturesOption).trimmed();
    const QString debugEffectProbeLayersValue = parser.value(debugEffectProbeLayersOption).trimmed();
    const bool debugEffectCapturesRequested = !debugEffectCapturesPath.isEmpty();
    const bool debugEffectProbeLayersRequested = !debugEffectProbeLayersValue.isEmpty();
    const QString debugEffectCapturesDir =
        debugEffectCapturesRequested ? QFileInfo(debugEffectCapturesPath).absoluteFilePath() : QString();
    const QString debugEffectCaptureCommand = app.arguments().join(QLatin1Char(' '));
    const std::optional<QString> debugEffectProbeLayers =
        debugEffectProbeLayersRequested ? parsePositiveIdList(debugEffectProbeLayersValue) : std::optional<QString>(QString());
    const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
    std::optional<std::vector<CaptureRequest>> parsedCaptures;
    const std::optional<QSize> windowSize = parseWindowSize(windowSizeValue);

    if (debugEffectCapturesRequested && backend != QStringLiteral("paper")) {
        qWarning() << "yakkai_scene_harness: --debug-effect-captures requires --backend paper";
        return 2;
    }
    if (debugEffectProbeLayersRequested && !debugEffectCapturesRequested) {
        qWarning() << "yakkai_scene_harness: --debug-effect-probe-layers requires --debug-effect-captures";
        return 2;
    }
    if (!debugEffectProbeLayers) {
        qWarning() << "yakkai_scene_harness: invalid --debug-effect-probe-layers value" << debugEffectProbeLayersValue;
        return 2;
    }
    if (debugEffectCapturesRequested && !QDir().mkpath(debugEffectCapturesDir)) {
        qWarning() << "yakkai_scene_harness: failed to create debug effect capture directory" << debugEffectCapturesDir;
        return 5;
    }
    if (!windowSize) {
        qWarning() << "yakkai_scene_harness: invalid --window-size" << windowSizeValue;
        return 2;
    }
    if (!capturePath.isEmpty() && multiCaptureRequested) {
        qWarning() << "yakkai_scene_harness: --capture cannot be combined with --capture-dir, --capture-times-ms, or --capture-sequence";
        return 2;
    }
    if (multiCaptureRequested && captureDirPath.isEmpty()) {
        qWarning() << "yakkai_scene_harness: --capture-dir is required for multi-capture";
        return 2;
    }
    if (!captureTimesValue.isEmpty() && !captureSequenceValue.isEmpty()) {
        qWarning() << "yakkai_scene_harness: use either --capture-times-ms or --capture-sequence, not both";
        return 2;
    }
    if (!captureTimesValue.isEmpty()) {
        parsedCaptures = parseCaptureTimes(captureTimesValue);
    } else if (!captureSequenceValue.isEmpty()) {
        parsedCaptures = parseCaptureSequence(captureSequenceValue);
    }
    if (multiCaptureRequested && (!parsedCaptures || parsedCaptures->empty())) {
        qWarning() << "yakkai_scene_harness: invalid multi-capture schedule";
        return 2;
    }

    if (!capturePath.isEmpty() || multiCaptureRequested) {
        app.setQuitOnLastWindowClosed(false);
    }

    QQmlApplicationEngine engine;
    QObject::connect(&app, &QGuiApplication::aboutToQuit, []() {
        qInfo() << "yakkai_scene_harness: aboutToQuit";
    });
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, []() {
        qInfo() << "yakkai_scene_harness: lastWindowClosed";
    });
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            qWarning().noquote() << "yakkai_scene_harness: qml warning:" << warning.toString();
        }
    });
    engine.addImportPath(buildQmlImportDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackend"), backend);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessSource"), QUrl::fromLocalFile(sourcePath));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessAssetsPath"), assetsPath);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessFillModeValue"), fillModeFromString(fillMode));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessWindowWidth"), windowSize->width());
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessWindowHeight"), windowSize->height());
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMouseInput"), parser.isSet(mouseOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMuted"), !parser.isSet(unmutedOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessShowInfoOverlay"), !parser.isSet(hideInfoOverlayOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackendQmlFile"), backendQmlFile(qmlDir, backend));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessQmlDir"), qmlDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCapturesPath"), debugEffectCapturesDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectCaptureCommand"), debugEffectCaptureCommand);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessDebugEffectProbeLayers"), *debugEffectProbeLayers);

    const QUrl mainQml = QUrl::fromLocalFile(QDir(qmlDir).filePath(QStringLiteral("Main.qml")));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            qWarning() << "yakkai_scene_harness: objectCreationFailed";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection
    );
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [](QObject* object, const QUrl& url) {
        qInfo() << "yakkai_scene_harness: objectCreated" << url << (object ? "ok" : "null");

        auto* window = qobject_cast<QWindow*>(object);
        if (!window) {
            return;
        }

        qInfo() << "yakkai_scene_harness: rootWindow initial visible=" << window->isVisible()
                << "size=" << window->size();
        QObject::connect(window, &QWindow::visibleChanged, window, [window](bool visible) {
            qInfo() << "yakkai_scene_harness: rootWindow visibleChanged" << visible;
        });
        QObject::connect(window, &QWindow::widthChanged, window, [window]() {
            qInfo() << "yakkai_scene_harness: rootWindow widthChanged" << window->width();
        });
        QObject::connect(window, &QWindow::heightChanged, window, [window]() {
            qInfo() << "yakkai_scene_harness: rootWindow heightChanged" << window->height();
        });
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [&app, capturePath, captureDelayMs, captureDirPath, captureTimesValue, captureSequenceValue, parsedCaptures](QObject* object, const QUrl&) {
        const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
        if (capturePath.isEmpty() && !multiCaptureRequested) {
            return;
        }

        auto* quickWindow = qobject_cast<QQuickWindow*>(object);
        if (!quickWindow) {
            qWarning() << "yakkai_scene_harness: capture requested but root object is not a QQuickWindow";
            QCoreApplication::exit(2);
            return;
        }

        QPointer<QQuickWindow> guardedWindow(quickWindow);

        if (!capturePath.isEmpty()) {
            const QString absoluteCapturePath = QFileInfo(capturePath).absoluteFilePath();
            QTimer::singleShot(std::max(captureDelayMs, 0), &app, [guardedWindow, absoluteCapturePath]() {
                if (!guardedWindow) {
                    qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
                    QCoreApplication::exit(3);
                    return;
                }
                requestCaptureExit(guardedWindow, saveWindowCapture(guardedWindow, absoluteCapturePath));
            });
            return;
        }

        if (!parsedCaptures || parsedCaptures->empty()) {
            qWarning() << "yakkai_scene_harness: invalid multi-capture schedule";
            QCoreApplication::exit(2);
            return;
        }

        const QString absoluteCaptureDir = QFileInfo(captureDirPath).absoluteFilePath();
        if (!QDir().mkpath(absoluteCaptureDir)) {
            qWarning() << "yakkai_scene_harness: failed to create capture directory" << absoluteCaptureDir;
            QCoreApplication::exit(5);
            return;
        }
        auto remaining = std::make_shared<int>(static_cast<int>(parsedCaptures->size()));
        auto failed = std::make_shared<bool>(false);

        for (const CaptureRequest& capture : *parsedCaptures) {
            const QString absolutePath = QDir(absoluteCaptureDir).filePath(capture.fileName);
            QTimer::singleShot(capture.timeMs, &app, [guardedWindow, absolutePath, remaining, failed]() {
                if (!guardedWindow) {
                    qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
                    *failed = true;
                } else if (saveWindowCapture(guardedWindow, absolutePath) != CaptureSuccess) {
                    *failed = true;
                }

                *remaining -= 1;
                if (*remaining == 0) {
                    requestCaptureExit(guardedWindow, *failed ? 5 : 0);
                }
            });
        }
    });
    engine.load(mainQml);

    QTimer::singleShot(0, &app, [&engine]() {
        qInfo() << "yakkai_scene_harness: rootObjects after load" << engine.rootObjects().size();
    });

    const int appStatus = app.exec();
    if (appStatus != 0 || !debugEffectCapturesRequested) {
        return appStatus;
    }

    const QString manifestPath = QDir(debugEffectCapturesDir).filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        qWarning() << "yakkai_scene_harness: debug effect manifest was not written" << manifestPath;
        return 6;
    }

    const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll());
    if (!manifest.isObject() ||
        manifest.object().value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
        qWarning() << "yakkai_scene_harness: debug effect manifest reports failure" << manifestPath;
        return 6;
    }

    return 0;
}
