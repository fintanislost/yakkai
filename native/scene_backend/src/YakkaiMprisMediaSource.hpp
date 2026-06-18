#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQml/qqmlregistration.h>

#include "MprisMediaPayload.hpp"

class YakkaiMprisMediaSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool available READ available NOTIFY mediaChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY mediaChanged)
    Q_PROPERTY(QString activeService READ activeService NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaJson READ mediaJson NOTIFY mediaChanged)
    Q_PROPERTY(QString runtimeMediaJson READ runtimeMediaJson NOTIFY runtimeMediaChanged)
    Q_PROPERTY(QString diagnosticText READ diagnosticText NOTIFY diagnosticTextChanged)

public:
    explicit YakkaiMprisMediaSource(QObject* parent = nullptr);

    bool enabled() const;
    void setEnabled(bool value);

    bool available() const;
    bool playing() const;
    QString activeService() const;
    QString mediaJson() const;
    QString runtimeMediaJson() const;
    QString diagnosticText() const;

    Q_INVOKABLE void refresh();

#ifdef YAKKAI_ENABLE_MPRIS_SOURCE_TEST_API
    void publishStateForTest(const yakkai::mpris::PlayerState& state);
    void publishUnavailableForTest(const QString& diagnostic);
#endif

signals:
    void enabledChanged();
    void mediaChanged();
    void runtimeMediaChanged();
    void diagnosticTextChanged();

private:
    struct StableSignature {
        QString service;
        bool available = false;
        bool playing = false;
        QString title;
        QString artist;
        QString album;
        QString albumArtPath;
        qint64 durationUsec = 0;
        QString playbackStatus;

        bool operator==(const StableSignature& other) const = default;
    };

    void updateTimer();
    StableSignature stableSignatureForState(const yakkai::mpris::PlayerState& state) const;
    void publishUnavailable(const QString& diagnostic);
    void publishState(const QString& mediaJson, const StableSignature& signature, const QString& diagnostic);
    void setDiagnosticText(const QString& value);

    QTimer m_pollTimer;
    bool m_enabled = false;
    bool m_available = false;
    bool m_playing = false;
    QString m_activeService;
    QString m_mediaJson;
    QString m_runtimeMediaJson;
    QString m_diagnosticText;
    StableSignature m_stableSignature;
};
