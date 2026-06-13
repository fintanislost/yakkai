#pragma once

#include <QtQuick/QQuickFramebufferObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QElapsedTimer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QHoverEvent>

#include "SceneWallpaper.hpp"

Q_DECLARE_LOGGING_CATEGORY(wekdeScene)

namespace scenebackend
{

class SceneObject : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QUrl assets READ assets WRITE setAssets)
    Q_PROPERTY(QString scenePropertiesJson READ scenePropertiesJson WRITE setScenePropertiesJson NOTIFY scenePropertiesJsonChanged)
    Q_PROPERTY(QString debugEffectCapturesPath READ debugEffectCapturesPath WRITE setDebugEffectCapturesPath NOTIFY debugEffectCapturesPathChanged)
    Q_PROPERTY(QString debugEffectCaptureCommand READ debugEffectCaptureCommand WRITE setDebugEffectCaptureCommand NOTIFY debugEffectCaptureCommandChanged)
    Q_PROPERTY(int debugEffectCaptureDelayMs READ debugEffectCaptureDelayMs WRITE setDebugEffectCaptureDelayMs NOTIFY debugEffectCaptureDelayMsChanged)
    Q_PROPERTY(QString debugEffectCaptureLayers READ debugEffectCaptureLayers WRITE setDebugEffectCaptureLayers NOTIFY debugEffectCaptureLayersChanged)
    Q_PROPERTY(QString debugEffectProbeLayers READ debugEffectProbeLayers WRITE setDebugEffectProbeLayers NOTIFY debugEffectProbeLayersChanged)
    Q_PROPERTY(QString debugEffectProbeHighRiskLayers READ debugEffectProbeHighRiskLayers WRITE setDebugEffectProbeHighRiskLayers NOTIFY debugEffectProbeHighRiskLayersChanged)
    Q_PROPERTY(QString debugEffectProbeChannelMapSlots READ debugEffectProbeChannelMapSlots WRITE setDebugEffectProbeChannelMapSlots NOTIFY debugEffectProbeChannelMapSlotsChanged)
    Q_PROPERTY(QString debugEffectProbeMaxEffects READ debugEffectProbeMaxEffects WRITE setDebugEffectProbeMaxEffects NOTIFY debugEffectProbeMaxEffectsChanged)
    Q_PROPERTY(QString debugPuppetEffectFinalMesh READ debugPuppetEffectFinalMesh WRITE setDebugPuppetEffectFinalMesh NOTIFY debugPuppetEffectFinalMeshChanged)
    Q_PROPERTY(bool debugPuppetEffectRouteOnly READ debugPuppetEffectRouteOnly WRITE setDebugPuppetEffectRouteOnly NOTIFY debugPuppetEffectRouteOnlyChanged)
    Q_PROPERTY(QString debugPuppetAnimationLayerOverrides READ debugPuppetAnimationLayerOverrides WRITE setDebugPuppetAnimationLayerOverrides NOTIFY debugPuppetAnimationLayerOverridesChanged)
    Q_PROPERTY(QString debugLayerVisibilityOverrides READ debugLayerVisibilityOverrides WRITE setDebugLayerVisibilityOverrides NOTIFY debugLayerVisibilityOverridesChanged)
    Q_PROPERTY(QString debugMousePosition READ debugMousePosition WRITE setDebugMousePosition NOTIFY debugMousePositionChanged)
    Q_PROPERTY(QString debugMouseTimeline READ debugMouseTimeline WRITE setDebugMouseTimeline NOTIFY debugMouseTimelineChanged)
    Q_PROPERTY(bool debugInteractiveMouse READ debugInteractiveMouse WRITE setDebugInteractiveMouse NOTIFY debugInteractiveMouseChanged)
    Q_PROPERTY(bool mouseDiagnosticsEnabled READ mouseDiagnosticsEnabled WRITE setMouseDiagnosticsEnabled NOTIFY mouseDiagnosticsEnabledChanged)
    Q_PROPERTY(QString lastMouseDiagnostic READ lastMouseDiagnostic NOTIFY lastMouseDiagnosticChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(int fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    Q_PROPERTY(float speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted)
public:
    constexpr static std::string_view CACHE_DIR { "wescene-renderer" };
    static std::string                GetDefaultCachePath();

    enum FillMode
    {
        STRETCH,
        ASPECTFIT,
        ASPECTCROP
    };
    Q_ENUM(FillMode)

    QUrl source() const;
    QUrl assets() const;
    QString scenePropertiesJson() const;
    QString debugEffectCapturesPath() const;
    QString debugEffectCaptureCommand() const;
    int debugEffectCaptureDelayMs() const;
    QString debugEffectCaptureLayers() const;
    QString debugEffectProbeLayers() const;
    QString debugEffectProbeHighRiskLayers() const;
    QString debugEffectProbeChannelMapSlots() const;
    QString debugEffectProbeMaxEffects() const;
    QString debugPuppetEffectFinalMesh() const;
    bool debugPuppetEffectRouteOnly() const;
    QString debugPuppetAnimationLayerOverrides() const;
    QString debugLayerVisibilityOverrides() const;
    QString debugMousePosition() const;
    QString debugMouseTimeline() const;
    bool debugInteractiveMouse() const;
    bool mouseDiagnosticsEnabled() const;
    QString lastMouseDiagnostic() const;
    void setSource(const QUrl& source);
    void setAssets(const QUrl& assets);
    void setScenePropertiesJson(const QString& value);
    void setDebugEffectCapturesPath(const QString& value);
    void setDebugEffectCaptureCommand(const QString& value);
    void setDebugEffectCaptureDelayMs(int value);
    void setDebugEffectCaptureLayers(const QString& value);
    void setDebugEffectProbeLayers(const QString& value);
    void setDebugEffectProbeHighRiskLayers(const QString& value);
    void setDebugEffectProbeChannelMapSlots(const QString& value);
    void setDebugEffectProbeMaxEffects(const QString& value);
    void setDebugPuppetEffectFinalMesh(const QString& value);
    void setDebugPuppetEffectRouteOnly(bool value);
    void setDebugPuppetAnimationLayerOverrides(const QString& value);
    void setDebugLayerVisibilityOverrides(const QString& value);
    void setDebugMousePosition(const QString& value);
    void setDebugMouseTimeline(const QString& value);
    void setDebugInteractiveMouse(bool value);
    void setMouseDiagnosticsEnabled(bool value);

    int   fps() const;
    int   fillMode() const;
    float speed() const;
    float volume() const;
    bool  muted() const;

    void setFps(int);
    void setFillMode(int);
    void setSpeed(float);
    void setVolume(float);
    void setMuted(bool);

    // debug
    bool vulkanValid() const;
    void enableVulkanValid();
    void enableGenGraphviz();

    Q_INVOKABLE void setAcceptMouse(bool);
    Q_INVOKABLE void setAcceptHover(bool);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;

public slots:
    void resizeFb();
    void play();
    void pause();

signals:
    void sourceChanged();
    void scenePropertiesJsonChanged();
    void debugEffectCapturesPathChanged();
    void debugEffectCaptureCommandChanged();
    void debugEffectCaptureDelayMsChanged();
    void debugEffectCaptureLayersChanged();
    void debugEffectProbeLayersChanged();
    void debugEffectProbeHighRiskLayersChanged();
    void debugEffectProbeChannelMapSlotsChanged();
    void debugEffectProbeMaxEffectsChanged();
    void debugPuppetEffectFinalMeshChanged();
    void debugPuppetEffectRouteOnlyChanged();
    void debugPuppetAnimationLayerOverridesChanged();
    void debugLayerVisibilityOverridesChanged();
    void debugMousePositionChanged();
    void debugMouseTimelineChanged();
    void debugInteractiveMouseChanged();
    void mouseDiagnosticsEnabledChanged();
    void lastMouseDiagnosticChanged();
    void fpsChanged();
    void fillModeChanged();
    void speedChanged();
    void volumeChanged();
    void firstFrame();
    void backendError(QString message);

private:
    QUrl m_source;
    QUrl m_assets;
    QString m_scenePropertiesJson;
    QString m_debugEffectCapturesPath;
    QString m_debugEffectCaptureCommand;
    int m_debugEffectCaptureDelayMs { 0 };
    QString m_debugEffectCaptureLayers;
    QString m_debugEffectProbeLayers;
    QString m_debugEffectProbeHighRiskLayers;
    QString m_debugEffectProbeChannelMapSlots;
    QString m_debugEffectProbeMaxEffects;
    QString m_debugPuppetEffectFinalMesh;
    bool m_debugPuppetEffectRouteOnly { false };
    QString m_debugPuppetAnimationLayerOverrides;
    QString m_debugLayerVisibilityOverrides;
    QString m_debugMousePosition;
    QString m_debugMouseTimeline;
    bool m_debugInteractiveMouse { false };
    bool m_mouseDiagnosticsEnabled { false };
    QString m_lastMouseDiagnostic;
    QElapsedTimer m_mouseDiagnosticLogTimer;

    int   m_fps { 15 };
    int   m_fillMode { FillMode::ASPECTCROP };
    float m_speed { 1.0f };
    float m_volume { 1.0f };
    bool  m_muted { false };

public:
    static void on_update(void* ctx);

    explicit SceneObject(QQuickItem* parent = nullptr);
    virtual ~SceneObject();

private:
    void setScenePropertyQurl(std::string_view, QUrl);
    void reportBackendError(const QString& message);
    void recordMouseDiagnostic(double normalizedX, double normalizedY, const QString& source);
    bool m_inited { false };
    bool m_enable_valid { false };
    bool m_reportedBackendError { false };

    std::shared_ptr<wallpaper::SceneWallpaper> m_scene { nullptr };

protected:
    QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*);
};

} // namespace scenebackend
