#ifndef POLLPOLICY_H
#define POLLPOLICY_H

#include <QString>

struct PlayerState;

struct PollInputs
{
    // a qmmp window has the focus (the application is active)
    bool focused = true;
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
    // the track-end polls already made for the playing track: each fires at most once
    bool beforeEndPolled = false;
    bool afterEndPolled = false;
    qint64 nowMs = 0;
};

const int kStopPolling = -1;
const int kKeepTimer = -1;
// the delay of pollSoon(): a command's confirmation poll
const int kPollSoonMs = 300;
// failed polls retried (2 s … 256 s) before polling stops
const int kMaxPollRetries = 8;

enum class PollKind { Regular, BeforeEnd, AfterEnd };

struct PollPlan
{
    int delayMs = kStopPolling;
    PollKind kind = PollKind::Regular;
};

// the next poll: a delay (or kStopPolling: nothing to watch, or the connection is lost), and
// whether it is one of the playing track's track-end polls
PollPlan planNextPoll(const PollInputs &inputs);
int nextPollDelayMs(const PollInputs &inputs);
// a successful poll shows another track, or the same one restarted or sought back: its
// track-end polls are due again
bool startsTrackEndCycle(const QString &trackId, qint64 progressMs, const PlayerState &state);
bool isPollHardFail(int failures);
// how an automatic poll request (a command's reply, the engine becoming active) re-arms the
// poll timer (remainingMs < 0: no timer), or kKeepTimer: soon, but never ahead of the error
// backoff
int pollSoonDelayMs(int failures, int remainingMs);
// when the track in a successful poll ends if it keeps playing; 0 when nothing plays
qint64 predictedTrackEndMs(const PlayerState &state, qint64 pollTimeMs);
// how a focus, dialog or user-command trigger re-arms the poll timer (remainingMs < 0: no timer),
// or kKeepTimer. It polls at most once per 5 s after the last poll was sent: inside that
// window it only brings a later timer forward to policyDelayMs, or, when polling stopped,
// polls when the window ends.
int interactionDelayMs(qint64 lastPollMs, qint64 nowMs, int remainingMs, int policyDelayMs);
// while requests are blocked the next poll waits for the block to end
int rateLimitedPollDelayMs(qint64 blockedUntilMs, qint64 nowMs);

#endif
