# Frontend frame pacing

The measured Windows frontend sometimes presents without advancing the game,
then catches up on a later presentation. The approved approach is to wait for
the next NTSC frame deadline before sampling input and running the next frame.

- Keep the original 53.693175 MHz / (3420 * 262) game rate and fractional time.
- Keep fast-forward and automated runs unthrottled.
- Retain the eight-frame accumulated-time cap and three-frame catch-up limit.
- Test deadline rounding, early wakeups, long stalls, and unthrottled transitions
  with a deterministic clock, then build the Windows and Android frontends.
- Repeat the existing isolated Windows measurements with VSync on and off;
  validate the existing frontend scripted tests and request a code review.

Measurements describe CPU-side presentation returns, not physical display
scanout. PicoDrive and the original comparison video are unavailable, so this
change does not establish a performance ranking between the two projects.
