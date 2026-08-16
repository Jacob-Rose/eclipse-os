# Eclipse Chroma Key

Your media in black and white, **except** where it already matches the colour
the rig is showing — within a limit you set. The key colour arrives over OSC
from `eclipse-dmx`, so the lights decide what stays in colour on screen.

The sender is in `_REPOS/eclipse-os/desktop`, but nothing here is specific to
it: set `rig_color` by hand and this is an ordinary selective-colour effect.
The point is that it does not have to be set by hand.

## Running it

Prove the link with **Eclipse Link Test** first — this scene cannot tell you
whether a colour is arriving, because a key that matches nothing looks exactly
like a key nobody is driving.

```
cd _REPOS/eclipse-os/desktop/python
python -m eclipse_dmx osc --test                       # is the app listening
python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
```

Load a video, a still or a webcam into Synesthesia's media slot. With nothing
loaded the scene keys a **hue wheel** instead, which is the better bench check
anyway: every hue is on screen at once, so exactly one wedge survives in colour
and driving the key sweeps that wedge round like a radar.

## Grey and white cannot be keyed — read this before deciding it is broken

A neutral colour has no chrominance and no meaningful hue. Nothing in any
footage is near it, so nothing survives, the whole frame goes monochrome, and
that is **indistinguishable from a broken scene**. It is the single most likely
reason this looks like it is not working.

Run the numbers: a saturated green pixel against a grey key is 0.51 apart in
chrominance, against a default limit of 0.28. No match, correctly.

mythos26 opens on `beat_pulse`, which is white on every hit for good reasons of
its own, so **the default show sends exactly the colour that cannot key
anything**. Two ways out:

- Cue something with a palette in it — `palette_wave`, `obelisk_seasons`.
- Or turn **Key by hand** on and set **manual_key** yourself.

### Key by hand

That toggle exists for a second reason too, and it is not obvious until it bites
you: **a running sender overwrites `rig_color` thirty times a second**, so once
the link works you can no longer drag that colour picker to try anything. The
scene becomes untestable at the exact moment it starts working. `manual_key` is
a separate control that nothing over OSC touches.

The fastest way to prove the effect works at all, independent of the rig:

1. **Key by hand** on.
2. **Show mask** on, **Limit** to 1.0 — the mask should go solid white, meaning
   everything matches. If it does not, the fault is in the scene, not the key.
3. Bring **Limit** back down and set **manual_key** to a colour that is actually
   in your footage.
4. Turn **Key by hand** off to hand control back to the truss.

## Setting the limit

Turn **Show mask** on. White is what keeps its colour, black is what goes
monochrome. Widen **Limit** until the thing you want is solid white and the rest
is solid black, then turn the mask off. That is the whole workflow; every other
control is taste.

**Match mode** is the one worth understanding, because it decides what "the same
colour" means and no single answer suits all footage:

| Mode | Ignores | Use it when |
|---|---|---|
| chroma | brightness | Default. A colour in shadow and the same colour in a hotspot both match — this is what a real chroma key compares. |
| hue only | brightness *and* saturation | The rig is washing a wall and what comes back is the right hue and nothing like the right saturation. Loosest. Pixels under ~12% saturation are excluded, because grey has no meaningful hue. |
| rgb | nothing | Strict. The colour must match in brightness too. Useful on flat graphics, wrong on video. |

**Invert** swaps which side of the limit keeps its colour. Off, the rig picks
what stays alive; on, it picks what gets erased. Same control, same footage,
completely different look.

## The addressing, which is the part that bites

The sender's default target is:

```
/controls/global/color/1
```

`1` is **positional** — the first colour control in the order `scene.json`
declares them, not a control named "1". Here that is `rig_color`, and
`mono_tint` is `2`. Reorder the `CONTROLS` array and the sender silently points
somewhere else.

Positional addresses do not survive a **scene change**: another scene's first
colour control is whatever it happens to be. The stable alternative is by name,
`/controls/scene/rigcolor`, passed with `--control` — Synesthesia lowercases
names and drops underscores for OSC. Both scenes in this folder name their key
colour `rig_color` on purpose, so that one address drives either of them.

## Why the beat widens the limit instead of brightening anything

More of the frame passes the test for a moment, so the colour spreads on the hit
and pulls back. Brightness is the rig's job — a screen that also flashes on the
bass is two instruments playing the same note, and the room reads it as one
mushy one.
