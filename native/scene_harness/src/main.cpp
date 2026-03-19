#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>

namespace
{
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
        return QDir(qmlDir).filePath(QStringLiteral("PaperSceneViewerHarness.qml"));
    }

    return QDir(qmlDir).filePath(QStringLiteral("SystemSceneViewerHarness.qml"));
}
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("paper_scene_harness"));
    QCoreApplication::setOrganizationName(QStringLiteral("Papercompany"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone Paper Company scene harness"));
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
    QCommandLineOption mouseOption(
        QStringList{QStringLiteral("mouse")},
        QStringLiteral("Enable mouse and hover input.")
    );
    QCommandLineOption unmutedOption(
        QStringList{QStringLiteral("unmuted")},
        QStringLiteral("Start with audio enabled.")
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

    parser.addOption(backendOption);
    parser.addOption(sourceOption);
    parser.addOption(assetsOption);
    parser.addOption(fillOption);
    parser.addOption(mouseOption);
    parser.addOption(unmutedOption);
    parser.addOption(captureOption);
    parser.addOption(captureDelayOption);
    parser.process(app);

    const QString qmlDir = QStringLiteral(PAPER_SCENE_HARNESS_QML_DIR);
    const QString buildQmlImportDir = QStringLiteral(PAPER_SCENE_HARNESS_BUILD_QML_IMPORT_DIR);
    const QString backend = parser.value(backendOption).trimmed().toLower();
    const QString rawSourcePath = parser.value(sourceOption).trimmed();
    const QString rawAssetsPath = parser.value(assetsOption).trimmed();
    const QString sourcePath = rawSourcePath.isEmpty() ? QString() : QFileInfo(rawSourcePath).absoluteFilePath();
    const QString assetsPath = rawAssetsPath.isEmpty() ? QString() : QFileInfo(rawAssetsPath).absoluteFilePath();
    const QString fillMode = parser.value(fillOption).trimmed().toLower();
    const QString capturePath = parser.value(captureOption).trimmed();
    const int captureDelayMs = parser.value(captureDelayOption).toInt();

    if (!capturePath.isEmpty()) {
        app.setQuitOnLastWindowClosed(false);
    }

    QQmlApplicationEngine engine;
    QObject::connect(&app, &QGuiApplication::aboutToQuit, []() {
        qInfo() << "paper_scene_harness: aboutToQuit";
    });
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, []() {
        qInfo() << "paper_scene_harness: lastWindowClosed";
    });
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            qWarning().noquote() << "paper_scene_harness: qml warning:" << warning.toString();
        }
    });
    engine.addImportPath(buildQmlImportDir);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackend"), backend);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessSource"), QUrl::fromLocalFile(sourcePath));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessAssetsPath"), assetsPath);
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessFillModeValue"), fillModeFromString(fillMode));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMouseInput"), parser.isSet(mouseOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessMuted"), !parser.isSet(unmutedOption));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessBackendQmlFile"), backendQmlFile(qmlDir, backend));
    engine.rootContext()->setContextProperty(QStringLiteral("sceneHarnessQmlDir"), qmlDir);

    const QUrl mainQml = QUrl::fromLocalFile(QDir(qmlDir).filePath(QStringLiteral("Main.qml")));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            qWarning() << "paper_scene_harness: objectCreationFailed";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection
    );
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [](QObject* object, const QUrl& url) {
        qInfo() << "paper_scene_harness: objectCreated" << url << (object ? "ok" : "null");

        auto* window = qobject_cast<QWindow*>(object);
        if (!window) {
            return;
        }

        qInfo() << "paper_scene_harness: rootWindow initial visible=" << window->isVisible()
                << "size=" << window->size();
        QObject::connect(window, &QWindow::visibleChanged, window, [window](bool visible) {
            qInfo() << "paper_scene_harness: rootWindow visibleChanged" << visible;
        });
        QObject::connect(window, &QWindow::widthChanged, window, [window]() {
            qInfo() << "paper_scene_harness: rootWindow widthChanged" << window->width();
        });
        QObject::connect(window, &QWindow::heightChanged, window, [window]() {
            qInfo() << "paper_scene_harness: rootWindow heightChanged" << window->height();
        });
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [&app, capturePath, captureDelayMs](QObject* object, const QUrl&) {
        if (capturePath.isEmpty()) {
            return;
        }

        auto* quickWindow = qobject_cast<QQuickWindow*>(object);
        if (!quickWindow) {
            qWarning() << "paper_scene_harness: capture requested but root object is not a QQuickWindow";
            QCoreApplication::exit(2);
            return;
        }

        const QString absoluteCapturePath = QFileInfo(capturePath).absoluteFilePath();
        QPointer<QQuickWindow> guardedWindow(quickWindow);
        QTimer::singleShot(std::max(captureDelayMs, 0), &app, [guardedWindow, absoluteCapturePath]() {
            if (!guardedWindow) {
                qWarning() << "paper_scene_harness: capture window was destroyed before capture";
                QCoreApplication::exit(3);
                return;
            }

            const QImage image = guardedWindow->grabWindow();
            qInfo() << "paper_scene_harness: capture size=" << image.size()
                    << "path=" << absoluteCapturePath;
            if (image.isNull()) {
                qWarning() << "paper_scene_harness: capture image is null";
                QCoreApplication::exit(4);
                return;
            }

            if (!image.save(absoluteCapturePath)) {
                qWarning() << "paper_scene_harness: failed to save capture to" << absoluteCapturePath;
                QCoreApplication::exit(5);
                return;
            }

            QCoreApplication::exit(0);
        });
    });
    engine.load(mainQml);

    QTimer::singleShot(0, &app, [&engine]() {
        qInfo() << "paper_scene_harness: rootObjects after load" << engine.rootObjects().size();
    });

    return app.exec();
}
