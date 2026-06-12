#include "YakkaiSceneViewer.hpp"

#include <QtGui/QtEvents>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

#include <algorithm>

#if YAKKAI_SCENE_USE_VENDORED_BACKEND
#include "Policy/ModelFallbackPolicy.hpp"

YakkaiSceneViewer::YakkaiSceneViewer(QQuickItem* parent)
    : scenebackend::SceneObject(parent)
{
    qInfo().noquote()
        << "[Yakkai] scene viewer build=2026-03-18 experimental-model-fallback";
    setBackendStatus(QStringLiteral("Vendored Yakkai scene backend initialized."));
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
                const auto statusDecision = wallpaper::policy::describeModelFallbackSupport({
                    .modelObjectCount = m_unsupportedModelObjectCount,
                    .firstFrameRendered = true,
                });
                if (statusDecision.kind == wallpaper::policy::ModelFallbackStatusKind::FirstFrameRendered) {
                    setBackendStatus(
                        QStringLiteral("Vendored Yakkai scene backend rendered a first frame while using the experimental model fallback for %1 model object(s). Output may still be incomplete.")
                            .arg(m_unsupportedModelObjectCount));
                } else {
                    setBackendStatus(QStringLiteral("Vendored Yakkai scene backend rendered a first frame."));
                }
            });
    connect(this,
            &scenebackend::SceneObject::backendError,
            this,
            [this](const QString& message) {
                setBackendStatus(
                    QStringLiteral("Vendored Yakkai scene backend could not start: %1")
                        .arg(message));
            });
    applyMouseInput();
}

bool YakkaiSceneViewer::mouseInputEnabled() const
{
    return m_mouseInputEnabled;
}

void YakkaiSceneViewer::setMouseInputEnabled(bool value)
{
    if (m_mouseInputEnabled == value) {
        return;
    }

    m_mouseInputEnabled = value;
    applyMouseInput();
    emit mouseInputEnabledChanged();
}

bool YakkaiSceneViewer::validationLayersEnabled() const
{
    return m_validationLayersEnabled;
}

void YakkaiSceneViewer::setValidationLayersEnabled(bool value)
{
    if (m_validationLayersEnabled == value) {
        return;
    }

    m_validationLayersEnabled = value;
    if (value) {
        enableVulkanValid();
        setBackendStatus(QStringLiteral("Vendored Yakkai scene backend will request Vulkan validation layers on the next renderer initialization."));
    } else {
        setBackendStatus(QStringLiteral("Vendored Yakkai scene backend validation layers are disabled."));
    }
    emit validationLayersEnabledChanged();
}

QString YakkaiSceneViewer::backendStatus() const
{
    return m_backendStatus;
}

void YakkaiSceneViewer::requestGraphvizDump()
{
    enableGenGraphviz();
    setBackendStatus(QStringLiteral("Vendored Yakkai scene backend will request a Graphviz dump on the next renderer compilation."));
}

void YakkaiSceneViewer::applyMouseInput()
{
    setAcceptMouse(m_mouseInputEnabled);
    setAcceptHover(m_mouseInputEnabled);
}

void YakkaiSceneViewer::refreshSceneSupportStatus()
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
    const auto statusDecision = wallpaper::policy::describeModelFallbackSupport({
        .supportedDrawableObjectCount = supportedDrawableObjectCount,
        .modelObjectCount = unsupportedModelObjectCount,
        .firstFrameRendered = false,
    });
    switch (statusDecision.kind) {
        case wallpaper::policy::ModelFallbackStatusKind::MixedSceneDetected:
            setBackendStatus(
                QStringLiteral("Vendored Yakkai scene backend detected %1 model object(s) and will use an experimental static fallback for them. Output may be incomplete.")
                    .arg(unsupportedModelObjectCount));
            break;
        case wallpaper::policy::ModelFallbackStatusKind::ModelOnlyDetected:
            setBackendStatus(
                QStringLiteral("Vendored Yakkai scene backend is using an experimental static fallback for %1 model object(s). Output may still be blank or incomplete.")
                    .arg(unsupportedModelObjectCount));
            break;
        case wallpaper::policy::ModelFallbackStatusKind::None:
        case wallpaper::policy::ModelFallbackStatusKind::FirstFrameRendered:
            break;
    }
}

void YakkaiSceneViewer::setBackendStatus(const QString& value)
{
    if (m_backendStatus == value) {
        return;
    }

    m_backendStatus = value;
    emit backendStatusChanged();
}

#else

YakkaiSceneViewer::YakkaiSceneViewer(QQuickItem* parent)
    : QQuickItem(parent)
{
    setBackendStatus(QStringLiteral("Scene backend scaffold only. No native renderer is wired yet."));
    updateMouseAcceptance();
}

QUrl YakkaiSceneViewer::source() const
{
    return m_source;
}

void YakkaiSceneViewer::setSource(const QUrl& value)
{
    if (m_source == value) {
        return;
    }

    m_source = value;
    emit sourceChanged();
}

QUrl YakkaiSceneViewer::assets() const
{
    return m_assets;
}

void YakkaiSceneViewer::setAssets(const QUrl& value)
{
    if (m_assets == value) {
        return;
    }

    m_assets = value;
    emit assetsChanged();
}

int YakkaiSceneViewer::fps() const
{
    return m_fps;
}

void YakkaiSceneViewer::setFps(int value)
{
    if (m_fps == value) {
        return;
    }

    m_fps = value;
    emit fpsChanged();
}

YakkaiSceneViewer::FillMode YakkaiSceneViewer::fillMode() const
{
    return m_fillMode;
}

void YakkaiSceneViewer::setFillMode(FillMode value)
{
    if (m_fillMode == value) {
        return;
    }

    m_fillMode = value;
    emit fillModeChanged();
}

float YakkaiSceneViewer::speed() const
{
    return m_speed;
}

void YakkaiSceneViewer::setSpeed(float value)
{
    if (qFuzzyCompare(m_speed, value)) {
        return;
    }

    m_speed = value;
    emit speedChanged();
}

float YakkaiSceneViewer::volume() const
{
    return m_volume;
}

void YakkaiSceneViewer::setVolume(float value)
{
    if (qFuzzyCompare(m_volume, value)) {
        return;
    }

    m_volume = value;
    emit volumeChanged();
}

bool YakkaiSceneViewer::muted() const
{
    return m_muted;
}

void YakkaiSceneViewer::setMuted(bool value)
{
    if (m_muted == value) {
        return;
    }

    m_muted = value;
    emit mutedChanged();
}

bool YakkaiSceneViewer::mouseInputEnabled() const
{
    return m_mouseInputEnabled;
}

void YakkaiSceneViewer::setMouseInputEnabled(bool value)
{
    if (m_mouseInputEnabled == value) {
        return;
    }

    m_mouseInputEnabled = value;
    updateMouseAcceptance();
    emit mouseInputEnabledChanged();
}

bool YakkaiSceneViewer::validationLayersEnabled() const
{
    return m_validationLayersEnabled;
}

void YakkaiSceneViewer::setValidationLayersEnabled(bool value)
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

QString YakkaiSceneViewer::backendStatus() const
{
    return m_backendStatus;
}

QString YakkaiSceneViewer::debugEffectCapturesPath() const
{
    return m_debugEffectCapturesPath;
}

void YakkaiSceneViewer::setDebugEffectCapturesPath(const QString& value)
{
    if (m_debugEffectCapturesPath == value) {
        return;
    }

    m_debugEffectCapturesPath = value;
    emit debugEffectCapturesPathChanged();
}

QString YakkaiSceneViewer::debugEffectCaptureCommand() const
{
    return m_debugEffectCaptureCommand;
}

void YakkaiSceneViewer::setDebugEffectCaptureCommand(const QString& value)
{
    if (m_debugEffectCaptureCommand == value) {
        return;
    }

    m_debugEffectCaptureCommand = value;
    emit debugEffectCaptureCommandChanged();
}

int YakkaiSceneViewer::debugEffectCaptureDelayMs() const
{
    return m_debugEffectCaptureDelayMs;
}

void YakkaiSceneViewer::setDebugEffectCaptureDelayMs(int value)
{
    const int normalized = std::max(0, value);
    if (m_debugEffectCaptureDelayMs == normalized) {
        return;
    }

    m_debugEffectCaptureDelayMs = normalized;
    emit debugEffectCaptureDelayMsChanged();
}

QString YakkaiSceneViewer::debugEffectCaptureLayers() const
{
    return m_debugEffectCaptureLayers;
}

void YakkaiSceneViewer::setDebugEffectCaptureLayers(const QString& value)
{
    if (m_debugEffectCaptureLayers == value) {
        return;
    }

    m_debugEffectCaptureLayers = value;
    emit debugEffectCaptureLayersChanged();
}

QString YakkaiSceneViewer::debugEffectProbeLayers() const
{
    return m_debugEffectProbeLayers;
}

void YakkaiSceneViewer::setDebugEffectProbeLayers(const QString& value)
{
    if (m_debugEffectProbeLayers == value) {
        return;
    }

    m_debugEffectProbeLayers = value;
    emit debugEffectProbeLayersChanged();
}

QString YakkaiSceneViewer::debugEffectProbeHighRiskLayers() const
{
    return m_debugEffectProbeHighRiskLayers;
}

void YakkaiSceneViewer::setDebugEffectProbeHighRiskLayers(const QString& value)
{
    if (m_debugEffectProbeHighRiskLayers == value) {
        return;
    }

    m_debugEffectProbeHighRiskLayers = value;
    emit debugEffectProbeHighRiskLayersChanged();
}

QString YakkaiSceneViewer::debugEffectProbeChannelMapSlots() const
{
    return m_debugEffectProbeChannelMapSlots;
}

void YakkaiSceneViewer::setDebugEffectProbeChannelMapSlots(const QString& value)
{
    if (m_debugEffectProbeChannelMapSlots == value) {
        return;
    }

    m_debugEffectProbeChannelMapSlots = value;
    emit debugEffectProbeChannelMapSlotsChanged();
}

QString YakkaiSceneViewer::debugEffectProbeMaxEffects() const
{
    return m_debugEffectProbeMaxEffects;
}

void YakkaiSceneViewer::setDebugEffectProbeMaxEffects(const QString& value)
{
    if (m_debugEffectProbeMaxEffects == value) {
        return;
    }

    m_debugEffectProbeMaxEffects = value;
    emit debugEffectProbeMaxEffectsChanged();
}

QString YakkaiSceneViewer::debugPuppetEffectFinalMesh() const
{
    return m_debugPuppetEffectFinalMesh;
}

void YakkaiSceneViewer::setDebugPuppetEffectFinalMesh(const QString& value)
{
    if (m_debugPuppetEffectFinalMesh == value) {
        return;
    }

    m_debugPuppetEffectFinalMesh = value;
    emit debugPuppetEffectFinalMeshChanged();
}

bool YakkaiSceneViewer::debugPuppetEffectRouteOnly() const
{
    return m_debugPuppetEffectRouteOnly;
}

void YakkaiSceneViewer::setDebugPuppetEffectRouteOnly(bool value)
{
    if (m_debugPuppetEffectRouteOnly == value) {
        return;
    }

    m_debugPuppetEffectRouteOnly = value;
    emit debugPuppetEffectRouteOnlyChanged();
}

QString YakkaiSceneViewer::debugPuppetAnimationLayerOverrides() const
{
    return m_debugPuppetAnimationLayerOverrides;
}

void YakkaiSceneViewer::setDebugPuppetAnimationLayerOverrides(const QString& value)
{
    if (m_debugPuppetAnimationLayerOverrides == value) {
        return;
    }

    m_debugPuppetAnimationLayerOverrides = value;
    emit debugPuppetAnimationLayerOverridesChanged();
}

QString YakkaiSceneViewer::scenePropertiesJson() const
{
    return m_scenePropertiesJson;
}

void YakkaiSceneViewer::setScenePropertiesJson(const QString& value)
{
    if (m_scenePropertiesJson == value) {
        return;
    }

    m_scenePropertiesJson = value;
    emit scenePropertiesJsonChanged();
}

void YakkaiSceneViewer::requestGraphvizDump()
{
    setBackendStatus(QStringLiteral("Scene backend scaffold only. Graphviz dump requested but no native renderer is wired yet."));
}

void YakkaiSceneViewer::updateMouseAcceptance()
{
    setAcceptedMouseButtons(m_mouseInputEnabled ? Qt::LeftButton : Qt::NoButton);
    setAcceptHoverEvents(m_mouseInputEnabled);
}

void YakkaiSceneViewer::setBackendStatus(const QString& value)
{
    if (m_backendStatus == value) {
        return;
    }

    m_backendStatus = value;
    emit backendStatusChanged();
}

#endif
