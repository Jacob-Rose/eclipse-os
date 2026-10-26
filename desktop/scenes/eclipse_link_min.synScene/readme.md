# Eclipse Link Minimal

The smallest scene that can answer **"is anything rendering at all"**. Load it
when a bigger scene comes up black.

It draws three things:

| What | Driven by | What it proves |
|---|---|---|
| the field | `rig_color` | the colour control. Grey means nothing has arrived — a working scene and a quiet sender. |
| a white sweep, left to right | `TIME` and nothing else | the scene is rendering. Two seconds a crossing. |
| a magenta corner block | nothing at all | this shader drew this frame. It is in every frame it ever draws. |

## Why it is built out of nothing

`eclipse_link_test` came up black, and it is the wrong instrument to find out
why: it has a feedback buffer, a second pass, eight texture reads and a toggle
that decides which half of it draws. Any one of those failing looks identical
from the front. This scene has none of them, and each absence rules out one way
to be black:

- **No `PASSES`.** A scene whose only declared pass has a `TARGET` renders
  perfectly into a buffer and puts nothing on the screen.
- **No `texture()`.** Nothing to sample from a buffer that is empty, unbound or
  a different size than the shader assumed.
- **No `syn_FadeInOut`.** The VJ's master fade is a legitimate black screen and
  it is not the one we are hunting. Ignored on purpose, so a fader at zero
  cannot make this scene lie.
- **No media, no `_isMediaActive()`.** No branch that depends on the VJ having
  loaded anything.
- **No loops, no helper functions, no `#define`.** Nothing for a stricter
  compiler to reject.

`brightness` scales the field only. The sweep and the corner ignore it, so
turning it down cannot produce the black screen this scene exists to rule out.

## Reading it

| What you see | What it means | Next |
|---|---|---|
| Black | Not a shader problem. The scene did not load, did not compile, or the app is not drawing it. | Check Synesthesia's console for a compile error, and that the folder is in the scenes directory the app is watching. |
| Corner and sweep, grey field | The scene is rendering and no OSC is landing on control 1. | `python -m eclipse_dmx osc --test`, and check OSC **input** is switched on in Settings → OSC. |
| Corner and sweep, coloured field | Everything works. | Move up to `eclipse_link_test`; anything black there is in the parts this scene left out. |
| Corner, no sweep | Rendering once and then not again — a stuck or paused render. | Nothing to do with the link. |

## What to try next on the test card

In the order they are worth testing, most likely first:

1. **Add the screen pass explicitly.** `eclipse_link_test` declares one entry in
   `PASSES` (`BuffA`) and relies on the app appending an implicit final pass to
   the screen. The authoring guide's own example writes that final pass out as
   an empty `{}`. If the app does not append one, the *only* pass is the buffer,
   nothing reaches the screen, and the result is exactly a black screen with no
   error anywhere.
2. **Set `test_card` off**, which drops the scene to a full-bleed wash of
   `rig_color` and skips the readouts, the eight texture reads and `linkMovement`
   entirely. Still black means the card's contents are not the problem.
3. **Check `FLOAT: true` on that pass.** A float target the GPU will not filter
   linearly can come back as nothing.

Do these one at a time. The card is being kept exactly as it is until then —
changing two things and watching it work tells you nothing about which one it
was.
