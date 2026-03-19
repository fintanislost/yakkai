#include "PaperSceneViewer.hpp"

#include <QtGui/QtEvents>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#if PAPER_SCENE_USE_VENDORED_BACKEND

PaperSceneViewer::PaperSceneViewer(QQuickItem* parent)
    : scenebackend::SceneObject(parent)
{
    qInfo().noquote()
        << "[Paper Gradient] scene viewer build=2026-03-18 experimental-model-fallback";
    setBackendStatus(QStringLiteral("Vendored Paper Company scene backend initialized."));
    connect(this,
            &scenebackend::SceneObject::sourceChanged,
            this,
            [this]() {
                refreshSceneSupportStatus();
            });
    connect(this,
            &scenebackend::SceneObject::firstFrame,
            this,
            [this]() {
                refreshSceneSupportStatus();
                if (m_unsupportedModelObjectCount > 0) {
                    setBackendStatus(
                        QStringLiteral("Vendored Paper Company scene backend rendered a first frame while using the experimental model fallback for %1 model object(s). Output may still be incomplete.")
                            .arg(m_unsupportedModelObjectCount));
                } else {
                    setBackendStatus(QStringLiteral("Vendored Paper Company scene backend rendered a first frame."));
                }
            });
    applyMouseInput();
}

bool PaperSceneViewer::mouseInputEnabled() const
{
    return m_mouseInputEnabled;
}

void PaperSceneViewer::setMouseInputEnabled(bool value)
{
    if (m_mouseInputEnabled == value) {
        return;
    }

    m_mouseInputEnabled = value;
    applyMouseInput();
    emit mouseInputEnabledChanged();
}

bool PaperSceneViewer::validationLayersEnabled() const
{
    return m_validationLayersEnabled;
}

void PaperSceneViewer::setValidationLayersEnabled(bool value)
{
    if (m_validationLayersEnabled == value) {
        return;
    }

    m_validationLayersEnabled = value;
    if (value) {
        enableVulkanValid();
        setBackendStatus(QStringLiteral("Vendored Paper Company scene backend will request Vulkan validation layers on the next renderer initialization."));
    } else {
        setBackendStatus(QStringLiteral("Vendored Paper Company scene backend validation layers are disabled."));
    }
    emit validationLayersEnabledChanged();
}

QString PaperSceneViewer::backendStatus() const
{
    return m_backendStatus;
}

void PaperSceneViewer::requestGraphvizDump()
{
    enableGenGraphviz();
    setBackendStatus(QStringLiteral("Vendored Paper Company scene backend will request a Graphviz dump on the next renderer compilation."));
}

void PaperSceneViewer::applyMouseInput()
{
    setAcceptMouse(m_mouseInputEnabled);
    setAcceptHover(m_mouseInputEnabled);
}

void PaperSceneViewer::refreshSceneSupportStatus()
{
    m_unsupportedModelObjectCount = 0;

    const QUrl currentSource = source();
    if (!currentSource.isLocalFile()) {
        return;
    }

    const QString localPath = currentSource.toLocalFile();
    if (!localPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        return;
    }

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    const QJsonValue objectsValue = document.object().value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        return;
    }

    int supportedDrawableObjectCount = 0;
    int unsupportedModelObjectCount = 0;
    const QJsonArray objects = objectsValue.toArray();
    auto hasNonNullKey = [](const QJsonObject& object, QStringView key) {
        const QJsonValue value = object.value(key);
        return !value.isUndefined() && !value.isNull();
    };
    for (const QJsonValue& objectValue : objects) {
        if (!objectValue.isObject()) {
            continue;
        }

        const QJsonObject object = objectValue.toObject();
        if (hasNonNullKey(object, QStringLiteral("image"))) {
            ++supportedDrawableObjectCount;
        } else if (hasNonNullKey(object, QStringLiteral("particle"))) {
            ++supportedDrawableObjectCount;
        } else if (hasNonNullKey(object, QStringLiteral("model"))) {
            ++unsupportedModelObjectCount;
        }
    }

    m_unsupportedModelObjectCount = unsupportedModelObjectCount;
    if (unsupportedModelObjectCount > 0) {
        if (supportedDrawableObjectCount > 0) {
            setBackendStatus(
                QStringLiteral("Vendored Paper Company scene backend detected %1 model object(s) and will use an experimental static fallback for them. Output may be incomplete.")
                    .arg(unsupportedModelObjectCount));
        } else {
            setBackendStatus(
                QStringLiteral("Vendored Paper Company scene backend is using an experimental static fallback for %1 model object(s). Output may still be blank or incomplete.")
                    .arg(unsupportedModelObjectCount));
        }
    }
}

void PaperSceneViewer::setBackendStatus(const QString& value)
{
    if (m_backendStatus == value) {
        return;
    }

    m_backendStatus = value;
    emit backendStatusChanged();
}

#else

PaperSceneViewer::PaperSceneViewer(QQuickItem* parent)
    : QQuickItem(parent)
{
    setBackendStatus(QStringLiteral("Scene backend scaffold only. No native renderer is wired yet."));
    updateMouseAcceptance();
}

QUrl PaperSceneViewer::source() const
{
    return m_source;
}

void PaperSceneViewer::setSource(const QUrl& value)
{
    if (m_source == value) {
        return;
    }

    m_source = value;
    emit sourceChanged();
}

QUrl PaperSceneViewer::assets() const
{
    return m_assets;
}

void PaperSceneViewer::setAssets(const QUrl& value)
{
    if (m_assets == value) {
        return;
    }

    m_assets = value;
    emit assetsChanged();
}

int PaperSceneViewer::fps() const
{
    return m_fps;
}

void PaperSceneViewer::setFps(int value)
{
    if (m_fps == value) {
        return;
    }

    m_fps = value;
    emit fpsChanged();
}

PaperSceneViewer::FillMode PaperSceneViewer::fillMode() const
{
    return m_fillMode;
}

void PaperSceneViewer::setFillMode(FillMode value)
{
    if (m_fillMode == value) {
        return;
    }

    m_fillMode = value;
    emit fillModeChanged();
}

float PaperSceneViewer::speed() const
{
    return m_speed;
}

void PaperSceneViewer::setSpeed(float value)
{
    if (qFuzzyCompare(m_speed, value)) {
        return;
    }

    m_speed = value;
    emit speedChanged();
}

float PaperSceneViewer::volume() const
{
    return m_volume;
}

void PaperSceneViewer::setVolume(float value)
{
    if (qFuzzyCompare(m_volume, value)) {
        return;
    }

    m_volume = value;
    emit volumeChanged();
}

bool PaperSceneViewer::muted() const
{
    return m_muted;
}

void PaperSceneViewer::setMuted(bool value)
{
    if (m_muted == value) {
        return;
    }

    m_muted = value;
    emit mutedChanged();
}

bool PaperSceneViewer::mouseInputEnabled() const
{
    return m_mouseInputEnabled;
}

void PaperSceneViewer::setMouseInputEnabled(bool value)
{
    if (m_mouseInputEnabled == value) {
        return;
    }

    m_mouseInputEnabled = value;
    updateMouseAcceptance();
    emit mouseInputEnabledChanged();
}

bool PaperSceneViewer::validationLayersEnabled() const
{
    return m_validationLayersEnabled;
}

void PaperSceneViewer::setValidationLayersEnabled(bool value)
{
    if (m_validationLayersEnabled == value) {
        return;
    }

    m_validationLayersEnabled = value;
    setBackendStatus(
        value
            ? QStringLiteral("Scene backend scaffold only. Validation layers were requested for the future native renderer.")
            : QStringLiteral("Scene backend scaffold only. Validation layers are disabled.")
    );
    emit validationLayersEnabledChanged();
}

QString PaperSceneViewer::backendStatus() const
{
    return m_backendStatus;
}

void PaperSceneViewer::requestGraphvizDump()
{
    setBackendStatus(QStringLiteral("Scene backend scaffold only. Graphviz dump requested but no native renderer is wired yet."));
}

void PaperSceneViewer::updateMouseAcceptance()
{
    setAcceptedMouseButtons(m_mouseInputEnabled ? Qt::LeftButton : Qt::NoButton);
    setAcceptHoverEvents(m_mouseInputEnabled);
}

void PaperSceneViewer::setBackendStatus(const QString& value)
{
    if (m_backendStatus == value) {
        return;
    }

    m_backendStatus = value;
    emit backendStatusChanged();
}

#endif
