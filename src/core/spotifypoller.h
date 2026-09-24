#ifndef SPOTIFYPOLLER_H
#define SPOTIFYPOLLER_H

#include <QObject>
#include <QTimer>
#include "spotifytypes.h"

class SpotifyApi;

class SpotifyPoller : public QObject
{
    Q_OBJECT
public:
    explicit SpotifyPoller(SpotifyApi *api, QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setEngineActive(bool active);
    void setSelectedDevice(const QString &deviceId);
    void pollSoon();
    PlayerState lastState() const { return m_last; }

signals:
    void stateChanged(const PlayerState &state);

private:
    void poll();
    void scheduleNext();

    SpotifyApi *m_api;
    QTimer m_timer;
    PlayerState m_last;
    QString m_device;
    bool m_enabled = false;
    bool m_engineActive = false;
    bool m_inFlight = false;
    int m_failures = 0;
};

#endif
