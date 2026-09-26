#ifndef POLLPOLICY_H
#define POLLPOLICY_H

#include <QtGlobal>

struct PlayerState;

struct PollInputs
{
    // a qmmp main window is shown and not minimized
    bool visible = true;
    // qmmp's engine holds a Spotify track
    bool engineActive = false;
    // the engine sent play/next and waits for the device to start; its start timeout needs polls
    bool engineAwaiting = false;
    // when the engine began waiting (continuously)
    qint64 awaitingSinceMs = 0;
    // the last successful poll showed the selected device playing
    bool selectedPlaying = false;
    // the last successful poll showed any playback (false: HTTP 204)
    bool hadPlayback = true;
    // failed polls in a row (rate-limited ones do not count)
    int failures = 0;
    // when the playing track ends, from the last successful poll; 0 = unknown
    qint64 predictedEndMs = 0;
    qint64 nowMs = 0;
};

const int kStopPolling = -1;
const int kKeepTimer = -1;
// the delay of pollSoon(): a command's confirmation poll
const int kPollSoonMs = 300;
// failed polls retried (2 s … 256 s) before polling stops
const int kMaxPollRetries = 8;

// the delay until the next poll, or kStopPolling: nothing to watch, or the connection is lost
int nextPollDelayMs(const PollInputs &inputs);
bool isPollHardFail(int failures);
// when the track in a successful poll ends if it keeps playing; 0 when nothing plays
qint64 predictedTrackEndMs(const PlayerState &state, qint64 pollTimeMs);
// how a window, focus or dialog trigger re-arms the poll timer (remainingMs < 0: no timer),
// or kKeepTimer. It polls at most once per 5 s after the last poll was sent: inside that
// window it only brings a later timer forward to policyDelayMs, or, when polling stopped,
// polls when the window ends.
int interactionDelayMs(qint64 lastPollMs, qint64 nowMs, int remainingMs, int policyDelayMs);
// while requests are blocked the next poll waits for the block to end
int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 nowMs);

#endif
