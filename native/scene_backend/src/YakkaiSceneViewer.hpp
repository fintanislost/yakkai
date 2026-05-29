#pragma once

#include <QtCore/QUrl>
#include <QtCore/QString>
#include <QtQml/qqmlregistration.h>

#if YAKKAI_SCENE_USE_VENDORED_BACKEND
#include "SceneBackend.hpp"

class YakkaiSceneViewer : public scenebackend::SceneObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool mouseInputEnabled READ mouseInputEnabled WRITE setMouseInputEnabled NOTIFY mouseInputEnabledChanged)
    Q_PROPERTY(bool validationLayersEnabled READ validationLayersEnabled WRITE setValidationLayersEnabled NOTIFY validationLayersEnabledChanged)
    Q_PROPERTY(QString backendStatus READ backendStatus NOTIFY backendStatusChanged)

public:
    enum FillMode
    {
        Stretch = scenebackend::SceneObject::FillMode::STRETCH,
        AspectFit = scenebackend::SceneObject::FillMode::ASPECTFIT,
        AspectCrop = scenebackend::SceneObject::FillMode::ASPECTCROP
    };
    Q_ENUM(FillMode)

    explicit YakkaiSceneViewer(QQuickItem* parent = nullptr);

    bool mouseInputEnabled() const;
    void setMouseInputEnabled(bool value);

    bool validationLayersEnabled() const;
    void setValidationLayersEnabled(bool value);

    QString backendStatus() const;

    Q_INVOKABLE void requestGraphvizDump();

signals:
    void mouseInputEnabledChanged();
    void validationLayersEnabledChanged();
    void backendStatusChanged();

private:
    void applyMouseInput();
    void refreshSceneSupportStatus();
    void setBackendStatus(const QString& value);

    bool m_mouseInputEnabled = false;
    bool m_validationLayersEnabled = false;
    int m_unsupportedModelObjectCount = 0;
    QString m_backendStatus;
};

#else

#include <QtQuick/QQuickItem>

class YakkaiSceneViewer : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QUrl assets READ assets WRITE setAssets NOTIFY assetsChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(FillMode fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    Q_PROPERTY(float speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool mouseInputEnabled READ mouseInputEnabled WRITE setMouseInputEnabled NOTIFY mouseInputEnabledChanged)
    Q_PROPERTY(bool validationLayersEnabled READ validationLayersEnabled WRITE setValidationLayersEnabled NOTIFY validationLayersEnabledChanged)
    Q_PROPERTY(QString backendStatus READ backendStatus NOTIFY backendStatusChanged)
    Q_PROPERTY(QString debugEffectCapturesPath READ debugEffectCapturesPath WRITE setDebugEffectCapturesPath NOTIFY debugEffectCapturesPathChanged)
    Q_PROPERTY(QString debugEffectCaptureCommand READ debugEffectCaptureCommand WRITE setDebugEffectCaptureCommand NOTIFY debugEffectCaptureCommandChanged)
    Q_PROPERTY(QString debugEffectProbeLayers READ debugEffectProbeLayers WRITE setDebugEffectProbeLayers NOTIFY debugEffectProbeLayersChanged)
    Q_PROPERTY(QString debugEffectProbeHighRiskLayers READ debugEffectProbeHighRiskLayers WRITE setDebugEffectProbeHighRiskLayers NOTIFY debugEffectProbeHighRiskLayersChanged)

public:
    enum FillMode
    {
        Stretch = 0,
        AspectFit = 1,
        AspectCrop = 2
    };
    Q_ENUM(FillMode)

    explicit YakkaiSceneViewer(QQuickItem* parent = nullptr);

    QUrl source() const;
    void setSource(const QUrl& value);

    QUrl assets() const;
    void setAssets(const QUrl& value);

    int fps() const;
    void setFps(int value);

    FillMode fillMode() const;
    void setFillMode(FillMode value);

    float speed() const;
    void setSpeed(float value);

    float volume() const;
    void setVolume(float value);

    bool muted() const;
    void setMuted(bool value);

    bool mouseInputEnabled() const;
    void setMouseInputEnabled(bool value);

    bool validationLayersEnabled() const;
    void setValidationLayersEnabled(bool value);

    QString backendStatus() const;

    QString debugEffectCapturesPath() const;
    void setDebugEffectCapturesPath(const QString& value);

    QString debugEffectCaptureCommand() const;
    void setDebugEffectCaptureCommand(const QString& value);

    QString debugEffectProbeLayers() const;
    void setDebugEffectProbeLayers(const QString& value);

    QString debugEffectProbeHighRiskLayers() const;
    void setDebugEffectProbeHighRiskLayers(const QString& value);

    Q_INVOKABLE void requestGraphvizDump();

signals:
    void sourceChanged();
    void assetsChanged();
    void fpsChanged();
    void fillModeChanged();
    void speedChanged();
    void volumeChanged();
    void mutedChanged();
    void mouseInputEnabledChanged();
    void validationLayersEnabledChanged();
    void backendStatusChanged();
    void debugEffectCapturesPathChanged();
    void debugEffectCaptureCommandChanged();
    void debugEffectProbeLayersChanged();
    void debugEffectProbeHighRiskLayersChanged();

private:
    void updateMouseAcceptance();
    void setBackendStatus(const QString& value);

    QUrl m_source;
    QUrl m_assets;
    int m_fps = 30;
    FillMode m_fillMode = AspectCrop;
    float m_speed = 1.0f;
    float m_volume = 1.0f;
    bool m_muted = true;
    bool m_mouseInputEnabled = false;
    bool m_validationLayersEnabled = false;
    QString m_backendStatus;
    QString m_debugEffectCapturesPath;
    QString m_debugEffectCaptureCommand;
    QString m_debugEffectProbeLayers;
    QString m_debugEffectProbeHighRiskLayers;
};

#endif
