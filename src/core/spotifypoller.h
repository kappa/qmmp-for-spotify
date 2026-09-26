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
    void setEngineActive(bool active, bool awaiting);
    void setSelectedDevice(const QString &deviceId);
    // a qmmp main window is shown; without the general plugin qmmp counts as visible
    void setVisible(bool visible);
    // a poll soon, also after polling stopped; commands use this, it is not throttled
    void pollSoon();
    // a window shown, focus or a dialog opened: pollSoon() at most once per 5 s
    void pollOnInteraction();
    PlayerState lastState() const { return m_last; }

signals:
    void stateChanged(const PlayerState &state);
    // polls failed until the retries ran out; polling stays stopped until pollSoon()
    void connectionLost();

private:
    void poll();
    void scheduleNext();
    int nextDelayMs() const;

    SpotifyApi *m_api;
    QTimer m_timer;
    PlayerState m_last;
    QString m_device;
    bool m_enabled = false;
    bool m_engineActive = false;
    bool m_engineAwaiting = false;
    bool m_visible = true;
    bool m_inFlight = false;
    // from the last successful poll; nothing stops before the first one
    bool m_hadPlayback = true;
    int m_failures = 0;
    qint64 m_lastPollAt = 0;
    qint64 m_predictedEnd = 0;
};

#endif
