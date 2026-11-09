# Westguard Keep entry video: evidence and limits

Source: [Chomp, `2026 11 09 17 31 03`](https://www.youtube.com/watch?v=I6wnMdeppdM).
The public player metadata identifies a 14-second, 1080p/24 fps recording,
uploaded on 9 November 2026 at 17:33:03 UTC. Its description is empty.

## Visible evidence

The accessible full-resolution thumbnail names **Westguard Keep**. The
storyboard shows the player inside, exiting at approximately 1–2 seconds,
turning outside around 3–8 seconds, and returning through the entrance around
9.5–11 seconds. These are approximate positions in a 100-image, 48×27-pixel
storyboard, not frame-accurate hitch timestamps.

The thumbnail's Northlight HUD reads:

| Reading | Value |
| --- | ---: |
| GPU / CPU | RTX 3080 / Ryzen 7 7800X3D |
| Scene target / output | 2560×1440 / 2560×1440 |
| GPU load | 96% |
| FPS / 1% / 0.1% lows | 85 / 24 / 11 |
| Mean / maximum frame interval | 11.8 / 147.2 ms |
| Effects CPU / effects GPU interval | 1.98 / 6.52 ms |
| EVENT GPU wait / render queue idle | 0.00 / 3.94 ms |

The maximum and lows describe the HUD's retained sample history. They cannot
be assigned to the entrance, or even restricted to this 14-second clip. The
single high GPU-load reading suggests substantial GPU work at that instant;
it does not establish the cause of an intermittent stall. The effect timers,
presentation intervals and queue counters have different scopes and must not
be added into a CPU/GPU breakdown. A 24 fps recording also cannot resolve
every presentation interval of an 85 fps game.

## Which log belongs to this video?

The separately supplied cage capture uses the same RTX 3080, but build source
`78230609d33855625b2bbac96122660fb6886a37d18b1f18b4958264150de8d8`
(built 16:17:05 UTC). Its ZIP attachment was uploaded at 17:24:34.221 UTC,
before the time in this video's title and before its YouTube upload. Hardware
identity alone therefore does not establish a matching recording interval.
The capture ends at Gjalerbron; the thumbnail names Westguard Keep.

The local r69 input has a different build identity (`f76aef5f…`) and hardware.
Neither log is treated as synchronized telemetry for the Westguard entrance.
No build identity is readable in this video's thumbnail.

## Investigation result

The public page, player metadata, thumbnail and storyboard were obtained.
Full video downloads through both direct MP4 and official HLS CDN routes
failed with TLS connection resets/timeouts. Browser playback also timed out.
Consequently, exact repeated-frame/hitch intervals and a complete motion
review are unavailable. This is a preliminary evidence review, not a claim
that the full video was watched or that its entrance stall was reproduced.

No production change is justified by these inputs alone. In particular, the
thumbnail's zero EVENT reading does not support attributing its historical
147.2 ms maximum to an EVENT wait; it also does not exclude waits elsewhere.
Matching build-identified telemetry around the entrance would distinguish
scene/asset admission, cold shaders, model capture/replay and driver waits.
The ongoing local-log MRT work is validated separately and is not presented
as a measured fix for this RTX 3080 recording.

Private investigation artifacts are under `out/castle-entry-20261109/`:
player/oEmbed metadata, the accessible thumbnail/storyboard, a timestamped
contact sheet, CDN failure logs and a hash receipt. Media and signed playback
URLs remain outside Git. No game was started and no client files were changed.
