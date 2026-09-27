# Context playback — design

Date: 2026-09-26. Agreed with Alex in a design round. Supersedes the backlog item "Play
Spotify-sourced playlists as Spotify contexts". Device selection is out of scope (separate
item "No device selection").

## Goal

qmmp stops driving Spotify one track at a time. Spotify plays lists by itself — a Spotify
*context* or a list of tracks — and qmmp follows what it plays. This matches how Alex uses
Spotify (he doesn't edit playlists in qmmp) and removes the queueing machinery that caused
most of the recent bugs and request traffic.

## Playback model

Two kinds of hand-off. In both, Spotify advances on its own and qmmp only follows.

1. **Context** — a qmmp playlist loaded from Spotify (Load Playlist, Load Liked Songs, a
   pasted playlist or album link) and not edited since is *bound* to its context URI.
   Starting any Spotify track in it sends
   `PUT /me/player/play {context_uri, offset: {uri: <track uri>}}`.
   Liked Songs is the context `spotify:user:<user id>:collection` (verified live
   2026-09-26).
2. **Run** — starting a Spotify track in any other playlist (hand-built, pasted track
   links, mixed with local files, or a detached one) sends
   `PUT /me/player/play {uris: [...], offset: {uri: <track uri>}}` where the list is the
   consecutive Spotify tracks of the qmmp playlist from the started track up to (not
   including) the next local file or the playlist end, at most **500** tracks.

qmmp never queues tracks and never starts single tracks on its own. Removed: queue-late,
the skip-to-queued path, the "device did not advance" fallback, and queue-related state.

### Following

- When Spotify plays a track of the current hand-off, qmmp moves its cursor to the matching
  playlist item (matched by track ID; the `?unavailable` path marker is ignored for
  matching) without sending any request.
- A natural track end is **not** reported to qmmp as finished (qmmp would then start the
  next item itself). The post-end poll (~1.5 s after the predicted end) sees the new track
  and the follower moves the cursor.
- Exception: at the **last track of a run** the engine reports the track finished, so qmmp
  moves on: to the local file that follows (the existing hand-over pauses the device), or,
  at the playlist end, qmmp stops and the device is paused (rule of 2026-09-24: custom
  lists pause at the end).
- Contexts end the Spotify way: whatever Spotify plays next (repeat, Autoplay) is followed.
- **Something outside the current hand-off** (Autoplay, another album/playlist started on
  the phone):
  - if the player state reports a context: qmmp loads that context into its own qmmp
    playlist `Spotify: <name>` (reusing an existing one of that name), binds it, and
    follows there;
  - if there is no context: the track is appended to the current playlist (as today).

### Long runs

Runs longer than 500 tracks are handed over in portions. About **2.5 s before the predicted
end of the portion's last track**, qmmp hands over the next portion, starting from its
first track at position 0 (decided 2026-09-26: cutting the last seconds of a track, which
are often silent, beats a hiccup or a gap). The predicted end comes from the engine clock,
refreshed by the pre-end poll ~5 s before the end. No queueing is used (it is one track per
request and can't be undone). Spotify accepted lists of 100, 800 and 2000 URIs in the
spike; silent truncation was not checked, so the 500 cap stays. At ~3.5 min per track a
portion lasts ~29 h, so this is rare.

### Shuffle and repeat

qmmp's Shuffle and Repeat toggles mirror Spotify's, both ways:
- toggling in qmmp sends `PUT /me/player/shuffle?state=` / `PUT /me/player/repeat?state=`
  (`off` / `context`; qmmp's "repeat track" → `track`). Both return **200 with a non-JSON
  body** (an opaque id) — treat any 2xx as success, don't parse the body;
- `shuffle_state` / `repeat_state` from polls update qmmp's toggles (without sending
  anything back).

**Next with shuffle on** (decided: option A). qmmp's Next picks qmmp's own random item
and the engine starts the hand-off from it. Spotify's shuffle order then restarts from
that track — it still sounds random, but it isn't Spotify's order. Alex uses Next with
shuffle often: this gets its own manual test scenario (below).

## Binding and its indication

- The binding (qmmp playlist → context URI + the list of track IDs at load time) is kept
  in the plugin's settings so it survives restarts. Keyed by the qmmp playlist name.
- Any structural change to a bound playlist that qmmp didn't make itself (add, remove,
  move, sort, shuffle-sort) **detaches** it: it plays as runs from then on. Loading the
  playlist again re-binds it.
- Indication (decided: no message boxes — qmmp notices are modal):
  1. **Playlist name**: bound playlists are named `Spotify: <name>`; on detach the
     playlist is renamed `<name> (edited)`. Reloading recreates/renames it to
     `Spotify: <name>`.
  2. **Tools → Spotify menu**: an inactive first line showing the state of the playlist
     that is playing (or selected when nothing plays): `Playing as Spotify playlist:
     <name>` / `Plain list (not linked to Spotify)`.
- No notices on bind or detach.

## Components

- **SpotifyApi**: `playContext(device, contextUri, offsetUri, positionMs)`,
  `playUris(device, uris, offsetUri, positionMs)`, `setShuffle(device, bool)`,
  `setRepeat(device, mode)`. `PlayerState` gains `contextUri`, `contextType`, `shuffle`,
  `repeat`. The existing budgets, rate-limit gate, 5 s command timeout and start
  coalescing apply unchanged.
- **Binding store** (general plugin): bind on load, detach on structural change, persist,
  rename, menu line.
- **Hand-off planning** (pure, `src/logic`): given the qmmp playlist items, the started
  index and the binding → context hand-off or run (with its end and portion boundaries).
- **Engine**: start = one hand-off request; follow; finish only at the end of a run;
  portion switch; no queueing.
- **Follower**: cursor matching inside the hand-off; loading an outside context; append
  otherwise; shuffle/repeat mirroring into qmmp.
- **Polling**: unchanged (focus-based; polls ~5 s before and ~1.5 s after the predicted
  end; the post-end poll is what moves the cursor while unfocused).

## Edge cases

- Starting on an unavailable track: skipped locally (no request) as today; qmmp starts
  the next item, which becomes the hand-off start.
- Unavailable tracks inside a run are left in the `uris` list (Spotify skips them).
- The context changed on Spotify's side after loading: the binding stays; tracks Spotify
  plays that qmmp's copy doesn't have are handled as "outside" → appended. Reloading
  refreshes.
- Start failures: systemic → stop with the notice; track-specific → skip (unchanged).
- Stop pauses the device; pause/seek/volume unchanged.

## Testing

Unit (pure): hand-off planning (context vs run, run end at local files and playlist end,
500 portions), edit detection → detach, cursor matching (incl. marked paths), outside
context → load decision, shuffle/repeat mapping, "finish only at the end of a run".

Manual (live), including:
- Load Liked Songs, double-click track 10: plays as context (Spotify shows "Liked Songs"),
  natural advance moves qmmp's cursor, in focus and unfocused.
- **Next with shuffle on**, several times in Liked Songs: something random plays each
  time, the cursor matches the device, Spotify stays in shuffle, no surprises.
- Toggle shuffle/repeat in qmmp and on the phone: both sides follow.
- Edit a bound playlist: renamed `… (edited)`, menu line changes, plays as runs.
- Mixed playlist: Spotify run → local file (device paused) → next Spotify run.
- Start an album on the phone: qmmp loads it as `Spotify: <album>` and follows.
- Let a context end with Autoplay on: qmmp follows (records what context Autoplay reports).
- A run over 500 tracks crossing a portion boundary (e.g. an edited Liked Songs): the
  last ~2.5 s of the portion's last track are cut, the next portion starts cleanly.

## Open, verified during implementation

- What `context` Autoplay tracks report.
- Whether `offset` is honoured with shuffle on (Alex had shuffle on during the spike).
