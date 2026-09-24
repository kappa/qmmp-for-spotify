#include <QPointer>
#include <qmmp/inputsource.h>
#include <qmmp/statehandler.h>
#include <qmmp/trackinfo.h>
#include "spotifyclient.h"
#include "spotifyengine.h"
#include "spotifypoller.h"

using namespace Qt::StringLiterals;

SpotifyEngine::SpotifyEngine(QObject *parent)
    : AbstractEngine(parent), m_client(SpotifyClient::instance()), m_logic(this, [this] { return m_clock.elapsed(); })
{
    m_clock.start();
    m_logic.setSelectedDevice(m_client->deviceId());
    m_logic.updateState(m_client->poller()->lastState());
    connect(m_client->poller(), &SpotifyPoller::stateChanged, this, [this](const PlayerState &state) {
        m_logic.updateState(state);
        publishTracks();
    });
    connect(m_client, &SpotifyClient::deviceChanged, this, [this] { m_logic.setSelectedDevice(m_client->deviceId()); });
    m_ticker.setInterval(250);
    connect(&m_ticker, &QTimer::timeout, this, [this] { StateHandler::instance()->dispatch(m_logic.elapsed(), 0); });
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
    m_logic.play();
    publishTracks();
    return true;
}

void SpotifyEngine::seek(qint64 time)
{
    m_logic.seek(time);
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
    m_logic.pause();
}

void SpotifyEngine::apiPlay(const QString &uri, qint64 positionMs, quint64 generation)
{
    QPointer<SpotifyEngine> self(this);
    SpotifyClient *client = m_client;
    m_client->api()->play(m_client->deviceId(), { uri }, positionMs, [self, client, generation](const ApiResult &result) {
        client->poller()->pollSoon();
        if(self && !result.ok)
        {
            self->m_logic.playFailed(generation, result.message);
            self->publishTracks();
        }
    });
    // precise timers never fire early; the margin absorbs millisecond rounding of the clock
    QTimer::singleShot(EngineLogic::kStartTimeoutMs + 50, Qt::PreciseTimer, this, [this] {
        m_logic.checkStartTimeout();
        publishTracks();
    });
}

void SpotifyEngine::apiPause()
{
    m_client->api()->pause(m_client->deviceId(), notifyOnFailure());
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
    m_client->api()->queue(m_client->deviceId(), uri, notifyOnFailure());
}

SpotifyApi::Callback SpotifyEngine::notifyOnFailure() const
{
    SpotifyClient *client = m_client;
    return [client](const ApiResult &result) {
        if(!result.ok)
            client->notify(result.message);
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
}

void SpotifyEngine::reportError(const QString &message, bool fatal)
{
    m_client->notify(message);
    m_ticker.stop();
    StateHandler::instance()->dispatch(fatal ? Qmmp::FatalError : Qmmp::NormalError);
}

void SpotifyEngine::publishTracks()
{
    m_client->setEngineTracks(m_logic.trackIds(), m_logic.isAwaitingStart());
}
