#include <QPointer>
#include <qmmp/inputsource.h>
#include <qmmp/soundcore.h>
#include <qmmp/statehandler.h>
#include <qmmp/trackinfo.h>
#include "apipolicy.h"
#include "spotifyauth.h"
#include "spotifyclient.h"
#include "spotifyengine.h"
#include "spotifypoller.h"

using namespace Qt::StringLiterals;

namespace {

// a command failed while waiting for the token when the session expired: the logout notice
// already said the device keeps playing, so its "not logged in" is not shown after it
void notifyFailure(SpotifyClient *client, const ApiResult &result)
{
    if(client->auth()->hasCredentials())
        client->notify(result.message);
}

}

SpotifyEngine::SpotifyEngine(QObject *parent)
    : AbstractEngine(parent), m_client(SpotifyClient::instance()), m_logic(this, [this] { return m_clock.elapsed(); }),
      m_volume(this, [this] { return m_clock.elapsed(); })
{
    m_clock.start();
    m_logic.setSelectedDevice(m_client->deviceId());
    m_logic.updateState(m_client->poller()->lastState());
    m_volume.setSelectedDevice(m_client->deviceId());
    SoundCore *core = SoundCore::instance();
    m_volume.setQmmpState(core->volume(), core->isMuted());
    connect(m_client->poller(), &SpotifyPoller::stateChanged, this, [this](const PlayerState &state) {
        m_logic.updateState(state);
        publishTracks();
        m_volume.updateState(state);
    });
    connect(m_client, &SpotifyClient::sessionEnded, this, [this] {
        m_logic.sessionEnded();
        publishTracks();
    });
    connect(m_client, &SpotifyClient::deviceChanged, this, [this] {
        m_logic.setSelectedDevice(m_client->deviceId());
        m_volume.setSelectedDevice(m_client->deviceId());
    });
    // SoundCore emits these even with no output open, which is always the case for this engine
    connect(core, &SoundCore::volumeChanged, this, [this](int volume) {
        m_volume.qmmpVolumeChanged(volume);
        scheduleVolumeSend();
    });
    connect(core, &SoundCore::mutedChanged, this, [this](bool muted) {
        m_volume.qmmpMutedChanged(muted);
        scheduleVolumeSend();
    });
    m_volumeTimer.setSingleShot(true);
    m_volumeTimer.setTimerType(Qt::PreciseTimer);
    // precise timers never fire early; the margin absorbs millisecond rounding of the clock
    m_volumeTimer.setInterval(int(VolumeLogic::kDebounceMs) + 5);
    connect(&m_volumeTimer, &QTimer::timeout, this, [this] { m_volume.tick(); });
    // a held seek key or a scrolled slider sends one seek, kSeekDebounceMs after the last step
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setTimerType(Qt::PreciseTimer);
    m_seekTimer.setInterval(int(EngineLogic::kSeekDebounceMs) + 5);
    connect(&m_seekTimer, &QTimer::timeout, this, [this] { m_logic.flushSeek(); });
    m_startTimer.setSingleShot(true);
    m_startTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        m_logic.flushStart();
        publishTracks();
    });
    m_ticker.setInterval(250);
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        m_logic.tick();
        StateHandler::instance()->dispatch(m_logic.elapsed(), 0);
    });
}

SpotifyEngine::~SpotifyEngine()
{
    m_logic.flushPendingPause();
    m_client->setEngineTracks({}, false);
}

bool SpotifyEngine::enqueue(InputSource *source)
{
    if(!m_logic.enqueue(source->path(), source->offset()))
        return false;
    source->deleteLater();
    publishTracks();
    return true;
}

bool SpotifyEngine::play()
{
    m_client->poller()->userCommand();
    m_client->setSuppressedTrack(QString());
    m_logic.play();
    publishTracks();
    return true;
}

void SpotifyEngine::seek(qint64 time)
{
    m_client->poller()->userCommand();
    m_logic.seek(time);
    m_seekTimer.start();
}

void SpotifyEngine::stop()
{
    m_logic.stop();
    publishTracks();
    // a play() in the same call stack (restart, Next, double-click) cancels the pause
    QTimer::singleShot(0, this, [this] { m_logic.flushPendingPause(); });
}

void SpotifyEngine::pause()
{
    m_client->poller()->userCommand();
    m_logic.pause();
}

void SpotifyEngine::apiPlay(const QString &uri, qint64 positionMs, quint64 generation)
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    m_startRequest = m_client->api()->play(m_client->deviceId(), { uri }, positionMs, [self, client, generation](const ApiResult &result) {
        client->poller()->pollSoon();
        if(self && !result.ok)
        {
            // a track not available in the user's country is skipped without a message
            const bool restricted = isRestrictedTrackReply(result.status, result.message);
            self->m_logic.playFailed(generation, restricted ? QString() : result.message,
                                     isSystemicPlayFailure(result.status, result.deviceProblem, result.reason));
            self->publishTracks();
        }
    });
    armStartTimeout();
}

// captures only the client: after a hand-over to another engine this engine is deleted
// before the reply arrives. Logged out, the pause fails without being sent and quietly.
void SpotifyEngine::apiPause()
{
    SpotifyClient *client = m_client;
    m_client->api()->pause(m_client->deviceId(), [client](const ApiResult &result) {
        if(!result.ok && !isAlreadyPausedReply(result.status, result.reason))
            notifyFailure(client, result);
        client->poller()->pollSoon();
    });
}

void SpotifyEngine::apiResume()
{
    m_client->api()->resume(m_client->deviceId(), notifyOnFailure());
}

void SpotifyEngine::apiSeek(qint64 positionMs)
{
    m_client->api()->seek(m_client->deviceId(), positionMs, notifyOnFailure());
}

void SpotifyEngine::apiQueue(const QString &uri)
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    m_client->api()->queue(m_client->deviceId(), uri, [self, client](const ApiResult &result) {
        if(!result.ok)
        {
            // the fallback play at the track's end skips an unavailable track quietly
            if(!isRestrictedTrackReply(result.status, result.message))
                notifyFailure(client, result);
            if(self)
                self->m_logic.queueFailed();
        }
        client->poller()->pollSoon();
    });
}

void SpotifyEngine::apiNext()
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    const quint64 generation = m_logic.activeGeneration();
    m_startRequest = m_client->api()->next(m_client->deviceId(), [self, client, generation](const ApiResult &result) {
        client->poller()->pollSoon();
        if(self && !result.ok)
        {
            self->m_logic.playFailed(generation, result.message,
                                     isSystemicPlayFailure(result.status, result.deviceProblem, result.reason));
            self->publishTracks();
        }
    });
    armStartTimeout();
}

// the device may land on something else than our track (a lost queue entry, Autoplay);
// without the timeout qmmp would wait for it for good, polling every 2 s
void SpotifyEngine::armStartTimeout()
{
    // precise timers never fire early; the margin absorbs millisecond rounding of the clock
    QTimer::singleShot(EngineLogic::kStartTimeoutMs + 50, Qt::PreciseTimer, this, [this] {
        m_logic.checkStartTimeout();
        publishTracks();
    });
}

// the aborted reply's callback does not run; the generation check would drop it anyway
void SpotifyEngine::cancelStartRequest()
{
    if(!m_startRequest)
        return;
    m_startRequest->cancel();
    m_startRequest.reset();
}

// precise timers never fire early; the margin absorbs millisecond rounding of the clock
void SpotifyEngine::pollAfter(qint64 delayMs)
{
    SpotifyPoller *poller = m_client->poller();
    QTimer::singleShot(int(delayMs) + 50, Qt::PreciseTimer, poller, [poller] { poller->pollSoon(); });
}

void SpotifyEngine::suppressFollow(const QString &trackId)
{
    m_client->setSuppressedTrack(trackId);
}

void SpotifyEngine::apiSetVolume(int percent)
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    m_client->api()->setVolume(m_client->deviceId(), percent, [self, client](const ApiResult &result) {
        if(result.ok)
            return;
        const bool disallowed = result.reason == "VOLUME_CONTROL_DISALLOW"_L1;
        if(!disallowed)
            notifyFailure(client, result);
        if(self)
            self->m_volume.setVolumeFailed(disallowed);
    });
}

void SpotifyEngine::setQmmpVolume(int percent)
{
    SoundCore::instance()->setVolume(percent);
}

void SpotifyEngine::reportVolumeUnsupported()
{
    m_client->notify(u"Spotify: this device does not allow volume control"_s);
}

SpotifyApi::Callback SpotifyEngine::notifyOnFailure() const
{
    SpotifyClient *client = m_client;
    return [client](const ApiResult &result) {
        if(!result.ok)
            notifyFailure(client, result);
        client->poller()->pollSoon();
    };
}

void SpotifyEngine::reportState(EngineState state)
{
    static const Qmmp::State states[] = { Qmmp::Stopped, Qmmp::Buffering, Qmmp::Playing, Qmmp::Paused };
    StateHandler::instance()->dispatch(states[int(state)]);
    if(state == EngineState::Playing || state == EngineState::Paused)
        m_ticker.start();
    else
        m_ticker.stop();
}

void SpotifyEngine::reportTrack(const QString &path, const SpotifyTrack &track)
{
    StateHandler::instance()->dispatch(track.durationMs);
    TrackInfo info(path);
    info.setDuration(track.durationMs);
    info.setValue(Qmmp::TITLE, track.title);
    info.setValue(Qmmp::ARTIST, track.artists.join(u", "_s));
    info.setValue(Qmmp::ALBUM, track.album);
    if(track.trackNumber > 0)
        info.setValue(Qmmp::TRACK, track.trackNumber);
    StateHandler::instance()->dispatch(info);
}

void SpotifyEngine::reportFinished()
{
    StateHandler::instance()->sendFinished();
    // queued after EVENT_FINISHED: a Spotify next track played from MediaPlayer::playNext
    // cancels the finish pause first; otherwise this (or ~SpotifyEngine) sends it
    QTimer::singleShot(0, this, [this] { m_logic.flushPendingPause(); });
}

void SpotifyEngine::reportError(const QString &message, bool fatal)
{
    if(!message.isEmpty())
        m_client->notify(message);
    m_ticker.stop();
    StateHandler::instance()->dispatch(fatal ? Qmmp::FatalError : Qmmp::NormalError);
}

void SpotifyEngine::reportNotice(const QString &message)
{
    m_client->notify(message);
}

// precise timers never fire early; the margin absorbs millisecond rounding of the clock
void SpotifyEngine::scheduleStartFlush(qint64 delayMs)
{
    m_startTimer.start(int(delayMs) + 5);
}

void SpotifyEngine::publishTracks()
{
    const QStringList ids = m_logic.trackIds();
    m_client->setEngineTracks(ids, m_logic.isAwaitingStart());
    // volume follows the device only while a Spotify track is current
    m_volume.setActive(!ids.isEmpty());
}

// each change restarts the timer, so it fires once, kDebounceMs after the last change of a burst
void SpotifyEngine::scheduleVolumeSend()
{
    if(m_volume.hasPendingSend())
        m_volumeTimer.start();
}
