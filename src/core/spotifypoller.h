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
    // a qmmp window has the focus; without the general plugin qmmp counts as focused
    void setFocused(bool focused);
    // a poll soon, also after polling stopped for no playback, but not after a hard fail and
    // not ahead of the error backoff; commands' replies use this, it is not throttled; during
    // a poll it polls again after it
    void pollSoon();
    // a playback command by the user: a hard fail ends, the command's pollSoon() polls again;
    // during the backoff it polls like an interaction
    void userCommand();
    // focus or a dialog opened: ends a hard fail and polls, at most once per 5 s
    void pollOnInteraction();
    PlayerState lastState() const { return m_last; }

signals:
    void stateChanged(const PlayerState &state);
    // polls failed until the retries ran out; polling stays stopped until an interaction or
    // a user command
    void connectionLost();

private:
    void poll();
    void pollEarly();
    void scheduleNext();
    int nextDelayMs() const;

    SpotifyApi *m_api;
    QTimer m_timer;
    PlayerState m_last;
    QString m_device;
    bool m_enabled = false;
    bool m_engineActive = false;
    bool m_engineAwaiting = false;
    qint64 m_awaitingSince = 0;
    bool m_focused = true;
    bool m_inFlight = false;
    bool m_pollAgain = false;
    // from the last successful poll; nothing stops before the first one
    bool m_hadPlayback = true;
    int m_failures = 0;
    qint64 m_lastPollAt = 0;
    qint64 m_predictedEnd = 0;
};

#endif
