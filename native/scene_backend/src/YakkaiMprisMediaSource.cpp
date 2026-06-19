#include "YakkaiMprisMediaSource.hpp"

#include "MprisMediaPayload.hpp"

#include <QtCore/QVariant>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>

#include <algorithm>

namespace {

constexpr int pollIntervalMs = 2000;
constexpr auto mprisServicePrefix = "org.mpris.MediaPlayer2.";
constexpr auto mprisObjectPath = "/org/mpris/MediaPlayer2";
constexpr auto mprisPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto dbusPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr int propertyCallTimeoutMs = 250;

struct MprisCandidateState {
    yakkai::mpris::PlayerState state;
    bool statusReadable = false;
    bool metadataReadable = false;
    QString statusError;
    QString metadataError;
};

QString firstArtist(const QVariant& value)
{
    if (value.canConvert<QStringList>()) {
        const QStringList artists = value.toStringList();
        if (!artists.isEmpty()) {
            return artists.first();
        }
    }

    const QVariantList artists = value.toList();
    if (!artists.isEmpty()) {
        return artists.first().toString();
    }

    return value.toString();
}

QVariant unwrapDbusVariant(const QVariant& value)
{
    if (value.canConvert<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }

    return value;
}

QVariantMap metadataFromVariant(const QVariant& value)
{
    const QVariant unwrapped = unwrapDbusVariant(value);
    if (unwrapped.canConvert<QVariantMap>()) {
        return unwrapped.toMap();
    }

    if (unwrapped.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(unwrapped.value<QDBusArgument>());
    }

    return {};
}

bool readPlayerProperty(const QString& service, const QString& property, QVariant* value, QString* errorText)
{
    QDBusInterface properties(
        service,
        QString::fromLatin1(mprisObjectPath),
        QString::fromLatin1(dbusPropertiesInterface),
        QDBusConnection::sessionBus());
    properties.setTimeout(propertyCallTimeoutMs);
    if (!properties.isValid()) {
        if (errorText != nullptr) {
            *errorText = properties.lastError().message();
        }
        return false;
    }

    const QDBusMessage replyMessage = properties.callWithArgumentList(
        QDBus::BlockWithGui,
        QStringLiteral("Get"),
        QVariantList{QString::fromLatin1(mprisPlayerInterface), property});
    const QDBusReply<QVariant> reply(replyMessage);
    if (!reply.isValid()) {
        if (errorText != nullptr) {
            *errorText = reply.error().message();
        }
        return false;
    }

    if (value != nullptr) {
        *value = unwrapDbusVariant(reply.value());
    }
    return true;
}

QStringList sortedMprisServices(const QStringList& registeredNames)
{
    QStringList candidates;
    for (const QString& name : registeredNames) {
        if (name.startsWith(QLatin1StringView(mprisServicePrefix))) {
            candidates.append(name);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const QString& left, const QString& right) {
        const int compare = QString::compare(left, right, Qt::CaseInsensitive);
        if (compare != 0) {
            return compare < 0;
        }
        return left < right;
    });
    return candidates;
}

bool isPlayingStatus(const QString& value)
{
    return value.compare(QStringLiteral("Playing"), Qt::CaseInsensitive) == 0;
}

int selectionScore(const MprisCandidateState& candidate)
{
    if (!candidate.statusReadable) {
        return -1;
    }

    int score = 0;
    if (!candidate.state.metadata.isEmpty()) {
        score += 4;
    }
    if (isPlayingStatus(candidate.state.playbackStatus)) {
        score += 2;
    }
    return score;
}

qsizetype preferredCandidateIndex(const QVector<MprisCandidateState>& candidates)
{
    qsizetype selectedIndex = -1;
    int selectedScore = -1;
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        const int score = selectionScore(candidates.at(index));
        if (score > selectedScore) {
            selectedIndex = index;
            selectedScore = score;
        }
    }
    return selectedIndex;
}

QString diagnosticForSelectedPlayer(const yakkai::mpris::PlayerState& state)
{
    if (state.service.isEmpty()) {
        return QStringLiteral("No MPRIS media player is available.");
    }

    if (state.metadata.isEmpty()) {
        return QStringLiteral("MPRIS player %1 did not provide media metadata.").arg(state.service);
    }

    return QStringLiteral("MPRIS media player selected: %1").arg(state.service);
}

} // namespace

YakkaiMprisMediaSource::YakkaiMprisMediaSource(QObject* parent)
    : QObject(parent)
    , m_mediaJson(yakkai::mpris::buildUnavailableMediaPayload())
    , m_runtimeMediaJson(yakkai::mpris::buildUnavailableMediaPayload())
    , m_diagnosticText(QStringLiteral("MPRIS media source is disabled."))
{
    m_pollTimer.setInterval(pollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &YakkaiMprisMediaSource::refresh);
}

bool YakkaiMprisMediaSource::enabled() const
{
    return m_enabled;
}

void YakkaiMprisMediaSource::setEnabled(bool value)
{
    if (m_enabled == value) {
        return;
    }

    m_enabled = value;
    updateTimer();
    if (m_enabled) {
        refresh();
    } else {
        publishUnavailable(QStringLiteral("MPRIS media source is disabled."));
    }
    emit enabledChanged();
}

bool YakkaiMprisMediaSource::available() const
{
    return m_available;
}

bool YakkaiMprisMediaSource::playing() const
{
    return m_playing;
}

QString YakkaiMprisMediaSource::activeService() const
{
    return m_activeService;
}

QString YakkaiMprisMediaSource::mediaJson() const
{
    return m_mediaJson;
}

QString YakkaiMprisMediaSource::runtimeMediaJson() const
{
    return m_runtimeMediaJson;
}

QString YakkaiMprisMediaSource::diagnosticText() const
{
    return m_diagnosticText;
}

#ifdef YAKKAI_ENABLE_MPRIS_SOURCE_TEST_API
void YakkaiMprisMediaSource::publishStateForTest(const yakkai::mpris::PlayerState& state)
{
    publishState(yakkai::mpris::buildMediaPayload(state), stableSignatureForState(state), diagnosticForSelectedPlayer(state));
}

void YakkaiMprisMediaSource::publishUnavailableForTest(const QString& diagnostic)
{
    publishUnavailable(diagnostic);
}

QString YakkaiMprisMediaSource::selectPreferredServiceForTest(
    const QVector<yakkai::mpris::PlayerState>& candidates)
{
    QVector<MprisCandidateState> selectionCandidates;
    selectionCandidates.reserve(candidates.size());
    for (const yakkai::mpris::PlayerState& candidate : candidates) {
        selectionCandidates.append({
            .state = candidate,
            .statusReadable = !candidate.service.isEmpty(),
            .metadataReadable = true,
        });
    }

    const qsizetype selectedIndex = preferredCandidateIndex(selectionCandidates);
    if (selectedIndex < 0) {
        return {};
    }
    return selectionCandidates.at(selectedIndex).state.service;
}
#endif

void YakkaiMprisMediaSource::refresh()
{
    if (!m_enabled) {
        publishUnavailable(QStringLiteral("MPRIS media source is disabled."));
        return;
    }

    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || bus.interface() == nullptr) {
        publishUnavailable(QStringLiteral("Session DBus is not available."));
        return;
    }

    const QDBusReply<QStringList> registeredNames = bus.interface()->registeredServiceNames();
    if (!registeredNames.isValid()) {
        publishUnavailable(QStringLiteral("Could not list session DBus services: %1").arg(registeredNames.error().message()));
        return;
    }

    const QStringList candidates = sortedMprisServices(registeredNames.value());
    if (candidates.isEmpty()) {
        publishUnavailable(QStringLiteral("No MPRIS media player is available."));
        return;
    }

    QVector<MprisCandidateState> selectionCandidates;
    selectionCandidates.reserve(candidates.size());
    QString lastStatusError;
    for (const QString& candidate : candidates) {
        QVariant statusValue;
        QString errorText;
        if (!readPlayerProperty(candidate, QStringLiteral("PlaybackStatus"), &statusValue, &errorText)) {
            lastStatusError = errorText;
            continue;
        }

        MprisCandidateState candidateState;
        candidateState.statusReadable = true;
        candidateState.state.service = candidate;
        candidateState.state.playbackStatus = statusValue.toString();

        QVariant metadataValue;
        QString metadataError;
        if (readPlayerProperty(candidate, QStringLiteral("Metadata"), &metadataValue, &metadataError)) {
            candidateState.metadataReadable = true;
            candidateState.state.metadata = metadataFromVariant(metadataValue);
        } else {
            candidateState.metadataError = metadataError;
        }

        selectionCandidates.append(candidateState);
        if (isPlayingStatus(candidateState.state.playbackStatus) && !candidateState.state.metadata.isEmpty()) {
            break;
        }
    }

    const qsizetype selectedIndex = preferredCandidateIndex(selectionCandidates);
    if (selectedIndex < 0) {
        publishUnavailable(QStringLiteral("No readable MPRIS media player is available: %1").arg(lastStatusError));
        return;
    }

    const MprisCandidateState selectedCandidate = selectionCandidates.at(selectedIndex);
    yakkai::mpris::PlayerState state = selectedCandidate.state;

    QVariant positionValue;
    QString propertyError;
    if (readPlayerProperty(state.service, QStringLiteral("Position"), &positionValue, &propertyError)) {
        state.positionUsec = positionValue.toLongLong();
    } else {
        state.positionUsec = 0;
    }

    QString diagnostic = diagnosticForSelectedPlayer(state);
    if (!selectedCandidate.metadataReadable && state.metadata.isEmpty()) {
        diagnostic = QStringLiteral("Could not read Metadata from %1: %2").arg(state.service, selectedCandidate.metadataError);
    }
    if (!lastStatusError.isEmpty()) {
        diagnostic = QStringLiteral("%1; one or more players could not be queried: %2")
                         .arg(diagnostic, lastStatusError);
    }
    publishState(yakkai::mpris::buildMediaPayload(state), stableSignatureForState(state), diagnostic);
}

void YakkaiMprisMediaSource::updateTimer()
{
    if (m_enabled) {
        m_pollTimer.start();
    } else {
        m_pollTimer.stop();
    }
}

YakkaiMprisMediaSource::StableSignature YakkaiMprisMediaSource::stableSignatureForState(
    const yakkai::mpris::PlayerState& state) const
{
    StableSignature signature;
    signature.service = state.service;
    signature.available = !state.service.isEmpty() && !state.metadata.isEmpty();
    signature.playing = signature.available &&
        state.playbackStatus.compare(QStringLiteral("Playing"), Qt::CaseInsensitive) == 0;
    signature.title = state.metadata.value(QStringLiteral("xesam:title")).toString();
    signature.artist = firstArtist(state.metadata.value(QStringLiteral("xesam:artist")));
    signature.album = state.metadata.value(QStringLiteral("xesam:album")).toString();
    signature.albumArtPath = yakkai::mpris::normalizeArtUrl(state.metadata.value(QStringLiteral("mpris:artUrl")).toString());
    signature.durationUsec = state.metadata.value(QStringLiteral("mpris:length")).toLongLong();
    signature.playbackStatus = state.playbackStatus.toCaseFolded();
    return signature;
}

void YakkaiMprisMediaSource::publishUnavailable(const QString& diagnostic)
{
    StableSignature signature;
    publishState(yakkai::mpris::buildUnavailableMediaPayload(), signature, diagnostic);
}

void YakkaiMprisMediaSource::publishState(
    const QString& mediaJson,
    const StableSignature& signature,
    const QString& diagnostic)
{
    setDiagnosticText(diagnostic);

    if (m_runtimeMediaJson != mediaJson) {
        m_runtimeMediaJson = mediaJson;
        emit runtimeMediaChanged();
    }

    if (m_stableSignature == signature) {
        return;
    }

    m_stableSignature = signature;
    m_available = signature.available;
    m_playing = signature.playing;
    m_activeService = signature.service;
    m_mediaJson = mediaJson;
    emit mediaChanged();
}

void YakkaiMprisMediaSource::setDiagnosticText(const QString& value)
{
    if (m_diagnosticText == value) {
        return;
    }

    m_diagnosticText = value;
    emit diagnosticTextChanged();
}
