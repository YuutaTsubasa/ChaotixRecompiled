# Netplay: online two-player (issue #46)

Two people on two machines play the game's own two-player mode together over
the Internet or a LAN. It uses rollback netcode: your own buttons act at once
(after a small, fixed input delay), the other player's are predicted until
they arrive, and when a prediction was wrong the game is quietly put back a
few frames and run again with what they really pressed.

It works because the game is deterministic: given the same starting state
and the same pads on every frame, two machines do exactly the same thing
(`chaotix_headless --lockstep` checks this for every frame of the recompiled
code). The two games are compared every second, and if they ever differ the
session stops with **DESYNC** rather than carrying on with two different
games.

Version 1 is desktop first. Android and iOS build it and the page works there
(the address is typed with the on-screen keyboard), but it has only been
played on Windows.

## Playing

1. Both players use the **same release**, the **same ROM**, and the **same
   mods that change the game** (ROM patches, character stats, replacement
   music). See [What must match](#what-must-match).
2. The host opens **NETPLAY** on the front menu (or the pause menu), checks
   **PORT** (7845 unless changed) and chooses **HOST A GAME**. The game goes
   on as usual while the host waits.
3. The other player types the host's address into **ADDRESS** (for example
   `203.0.113.5:7845`, `192.168.1.20`, or a Tailscale name such as
   `my-pc.tail1234.ts.net`; the port may be left off when it is 7845) and
   chooses **JOIN A GAME**.
4. The host's game holds for a moment while it is sent to the other player
   (a few hundred kilobytes), then both play on from exactly that moment. The
   host is **player 1** (pad 1), the other player **player 2** (pad 2). Each
   uses their own first controller or keyboard.
5. In the game, choose **PLAY STYLE: 2P** as you would on one console: player
   2 then controls the partner. (In 1P mode player 2's pad does nothing; the
   game itself offers no other way in.)
6. **DISCONNECT** on the NETPLAY page ends the session; closing the program
   does too, and the other player is told.

A good moment to start is the title screen or the front menu: the host's game
is sent as it is, including its save (DATA slots). The joining player plays
the host's saves and gets their own game and saves back, untouched, when the
session ends.

### The NETPLAY page

| Row | What it does |
| --- | --- |
| HOST A GAME | wait for a player on PORT (you are player 1) |
| JOIN A GAME | join the host at ADDRESS (you are player 2) |
| ADDRESS | the host's `address:port`. Press it and type; Enter keeps it, Escape puts the old one back. With a pad: up/down change the last character, right adds one, left deletes one, A keeps it. On a phone the on-screen keyboard comes up. |
| PORT | the UDP port to host on (type it, or left/right) |
| INPUT DELAY | 0-4 frames (2 to begin with); see below. Used by the next session. |
| STATUS | WAITING FOR PLAYER 2, RECEIVING THE GAME 40%, or `PLAYER 1 - PING 48 MS - ROLLBACK 2` while playing |
| DISCONNECT | end the session |

While a session runs, the same status is shown at the top left of the game,
with **WAITING** when the game has to hold for the other player (their pads
have fallen too far behind, see below). Messages (a player refused, the
connection lost, a desync) come up as notices at the bottom.

### Input delay

Your buttons are applied that many frames after you press them, on both
machines. Every frame of delay is a frame of the other player's latency the
game does not have to guess at. With 0 the controls feel exactly as offline
but nearly every change of the other player's pad is corrected after the
fact (the picture can jump a little); with 2 (the default) a connection of
up to about 70 ms round trip needs almost no corrections. The two players
may use different delays.

## Ports, routers and VPNs

There is no matchmaking server and no NAT traversal in this version: the
joining player must be able to reach the host's UDP port.

- **Same LAN**: join the host's LAN address (`ipconfig` / `ip addr` shows it).
- **Over the Internet, simplest**: a mesh VPN such as
  [Tailscale](https://tailscale.com) or [ZeroTier](https://www.zerotier.com).
  Both players join the same network; the joiner uses the host's VPN address
  (`100.x.y.z` for Tailscale) or its name. No router setup.
- **Over the Internet, port forwarding**: the host forwards **UDP** port 7845
  (or the PORT chosen) on the router to their PC, and gives the joiner their
  public address.
- Windows asks once whether to let the program use the network: allow it on
  the networks you play on.

The joining side needs nothing forwarded (it opens its own port, and replies
come back through it).

## What must match

Checked when the player joins; a mismatch is refused with the reason on both
screens (`the ROM patches or character stats (mods) are not the same on both
sides`, ...):

- the program's version (and the netplay protocol's),
- the ROM (SHA-1),
- the image that runs: the ROM after **ROM patches** and **character stats**
  (mod packs, `Mods/Patches`, `Mods/Characters`),
- which tracks **replacement music** replaces (`Mods/Music`): the game stops
  its own music for a replaced track, which changes what it does.

Taken over from the host for the session, so they need not match (both
machines use the host's):

- widescreen (on/off, the margins, WIDEN EVERYTHING, HUD at the edges): the
  joiner sees the host's width, letterboxed to their window, and resizing a
  window does not change the margins until the session ends,
- 6-button pad,
- lobby shortcuts (partner and time of day),
- the ring tether options (OPTIONS > GAMEPLAY: TETHER and PARTNER,
  [RING_TETHER.md](RING_TETHER.md); FOLLOW and SOLO are meant for one player),
- the save data (the joiner plays the host's DATA slots).

Free to differ: volume, music and effects levels, video settings, controls,
texture and sprite replacements, character colours, the recompiled code or
the interpreters.

Not available during a session (they would change one player's game only):
save and load state, rewind, fast-forward, TIME ATTACK, ghosts, BACK TO
TITLE, the music player, cheats (they are not applied while the session runs
the game), reloading mods. Achievements and the speedrun timer do not follow
a netplay session.

## Desyncs

Every 60 frames each side hashes the whole machine state (minus the sound's
output filter, which follows each player's volume settings) at a frame whose
pads are known to both, and sends the hash. If the two ever differ, both
sides stop with **DESYNC at frame N** and write the state they hashed to
`Netplay/desync_frame<N>_player<1|2>.state` in the user data folder. These
are ordinary save states: load them with `chaotix_headless --load-state` to
find out what differs. A desync is always a bug (or a setting that changes the
game and is not yet checked at connect): please report it with both files.

## Limitations

- No NAT traversal, relay or lobby: see [Ports](#ports-routers-and-vpns). The
  code is laid out for one to be added (a `Transport` that relays or punches
  holes; `src/net/transport.h`).
- Two players only; no spectators.
- Sound: frames run again after a rollback make no sound (they were heard
  once already), so a misprediction can leave a short glitch: a sound
  effect from the wrong guess, or one cut short. Replacement music follows
  the frames as they were first run.
- A rollback runs up to 8 frames again in one display frame. On the
  development PC (an i9-14900KF, under load) that is 10-17 ms in a two-player
  level; a slower PC can drop a frame on the longest ones. More input delay
  makes long rollbacks rare.
- If the other player's pads are more than 8 frames late, the game holds
  (WAITING) until they come; after 5 seconds without a word from the other
  side the session ends.
- The host's game is sent as it is: if the host is in a level with cheats on,
  the joiner gets that level with their effects, but no cheat is applied from
  then on.
- Android and iOS: built and working as far as tested in the build, not
  played on real devices.

## How it works

Code: `src/net/`.

- `rollback.{h,cpp}`: the rollback core, with no sockets and no machine
  (unit-tested on a toy game in `tests/test_netplay.cpp`). Local pads go in
  `input_delay` frames ahead; the remote pad is predicted as the last one
  known; a snapshot is kept before every frame run on a prediction (and every
  60th, for the hashes); when a remote pad arrives that differs from what was
  used, the snapshot of that frame is loaded and the frames up to the present
  run again. A tick never puts more than 8 frames on a prediction; it waits
  instead.
- `netplay.{h,cpp}`: the session. Handshake, the start state in 1 KB chunks
  (the joiner asks for the ones it is missing, so loss only slows it down),
  then INPUT datagrams 60 times a second each way carrying every pad the
  other side has not acknowledged (so a lost datagram is covered by the
  next), the sender's frame and ack, ping time stamps and the latest state
  hashes. Time sync as in GGPO: each side knows how far ahead of the other it
  is, and the one further ahead skips a frame now and then. 5 s of silence
  ends the session.
- `protocol.{h,cpp}`: the wire format (little-endian, "CXNP" magic, protocol
  version 1).
- `udp_socket.{h,cpp}`: non-blocking UDP (BSD sockets / Winsock), IPv4 and
  IPv6; the only platform code.
- `loopback.{h,cpp}`: an in-process network with latency, jitter and loss,
  for tests.
- `machine_game.{h,cpp}`: the game behind the session: snapshots are the raw
  save state without the picture (`runtime/save_state.h`) plus the cartridge
  SRAM (so a level finished on a wrong guess is rolled back too), about 1 MB,
  saved or loaded in about 0.1 ms; the host's settings that change the game
  are applied before every frame on both sides.

Measured (Windows, i9-14900KF, other work running, a two-player level with
sound): a snapshot 0.1 ms, a load 0.1 ms, a hash 0.25 ms, a frame 1.2-2 ms,
a rollback of 8 frames 10-17 ms. Over the simulated network of the tests
(50-100 ms each way, 5% lost, input delay 2) a rollback happens every 20-25
frames and is 1-6 frames long.

## Testing

- `chaotix_tests` (`netplay.*`, `menu.netplay_page_*`): the core (prediction,
  rollback to the same result as no latency, input delay, the stall, loss,
  desync detection), the wire format, whole sessions over the simulated
  network, refusals and timeouts, and the NETPLAY page.
- `chaotix_headless --netplay-loopback LATENCY:JITTER:LOSS` runs both players
  in one process over the simulated network (milliseconds one way, +-
  milliseconds, percent lost), in simulated time, with fuzzed pads on both
  sides (`--netplay-fuzz SEED`), from the host's game after `--stage` (or
  `--netplay-start FRAMES` of the title screen). It ends when frame
  `--frames` is confirmed on both sides and passes when both machines' state
  at that frame and a single machine's, started from the same state and fed
  the confirmed pads of both players, are identical. `--netplay-delay N`,
  `--netplay-delay2 N` (the second player's), `--netplay-window N`,
  `--netplay-hash-every N`, `--netplay-seed N` (the network's),
  `--netplay-client-volume PCT` and `--netplay-inject-desync FRAME` (a byte of
  the second player's RAM changed at that frame, which must be reported) vary
  it.
- `chaotix_headless --netplay-host PORT` and `--netplay-join ADDRESS` do the
  same over real UDP, one player per process, in real time
  (`tests/test_netplay_udp.py` runs both on this machine).
- `ChaotixRecompiled --netplay-host PORT` / `--netplay-join ADDRESS` start a
  session as soon as the game is up, as the page would.

CTest: `netplay_loopback_level` (3000 frames of a 2P level, 50-100 ms each
way, 5% lost), `netplay_loopback_no_delay_widescreen` (no input delay,
widescreen, 60-140 ms, 8% lost, the second player at other volume levels),
`netplay_desync_is_detected`, `netplay_udp_localhost`.
