#ifndef SPOTIFYENGINE_H
#define SPOTIFYENGINE_H

#include <QElapsedTimer>
#include <QTimer>
#include <qmmp/abstractengine.h>
#include "enginelogic.h"
#include "spotifyapi.h"

class SpotifyClient;

class SpotifyEngine : public AbstractEngine, private EngineSink
{
    Q_OBJECT
public:
    explicit SpotifyEngine(QObject *parent = nullptr);
    ~SpotifyEngine();

    bool enqueue(InputSource *source) override;
    bool play() override;
    void seek(qint64 time) override;
    void stop() override;
    void pause() override;

private:
    void apiPlay(const QString &uri, qint64 positionMs, quint64 generation) override;
    void apiPause() override;
    void apiResume() override;
    void apiSeek(qint64 positionMs) override;
    void apiQueue(const QString &uri) override;
    void apiNext() override;
    void reportState(EngineState state) override;
    void reportTrack(const QString &path, const SpotifyTrack &track) override;
    void reportFinished() override;
    void reportError(const QString &message, bool fatal) override;

    SpotifyApi::Callback notifyOnFailure() const;
    void publishTracks();

    SpotifyClient *m_client;
    QElapsedTimer m_clock;
    EngineLogic m_logic;
    QTimer m_ticker;
};

#endif
