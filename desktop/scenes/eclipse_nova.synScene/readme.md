# Eclipse Nova

Felix Woitzel's **Nova** — the scene that ships with Synesthesia — painted in
the colour the rig is showing. The galaxies are `rig_color`, which arrives over
OSC from `eclipse-dmx`; the other five colours of the look are derived from it
by one rule, so a colour change on the truss moves the whole picture and not a
corner of it. The other change is how the hi-hats move it (below). Everything
else is the original, untouched.

Credit where it is due: the scene is Woitzel's. `main.glsl` marks everything
eclipse-os changed with `ECLIPSE`.

## Running it

```
cd _REPOS/eclipse-os/desktop/python
python -m eclipse_dmx osc --test                       # is the app listening
python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
```

Prove the link with **Eclipse Link Test** first. This scene cannot tell you
whether a colour is arriving — a teal Nova nobody is driving looks exactly like
a teal Nova being driven teal. That is what its default colour is, on purpose:
with nothing sending, this is Nova in its first regime.

## Why the rig drives the shader and not the other way round

You asked whether the colour could instead be got deterministically *from* the
shader and routed out to the rig. For this scene, no — and knowing why is
worth more than the feature:

Nova's palette is not a function of anything. It has four hard-coded colour
sets, its **regimes**, and which one is showing is drawn at random each time
the scene is transitioned to (`SMOOTH_TRANSITIONS` on `cReg0..cReg3` in the
original `scene.json`, cross-faded over three seconds). There is no clock or
audio value from which to recompute it on the other side, and Synesthesia
sends nothing out. The only live inputs are the audio ones — `syn_BassPresence`
brightens the galaxies — which the rig already analyses for itself.

So the deterministic version of "both sides show the same colour" is what this
scene does: make one side the source. The rig is the natural one; it is the
side with a cue list.

### If you want a pattern to match one regime exactly

The original's four regimes, in linear RGB, in case an eclipse pattern should
be built to sit on the *unmodified* Nova instead. Note the ones above 1.0 are
deliberate — an overbright wash.

| role | regime 0 (default) | regime 1 | regime 2 | regime 3 |
|---|---|---|---|---|
| base wash | 1.40, 1.20, 0.20 | 0.45, 0.75, 1.00 | 0, 0, 0 | 0, 0, 0 |
| galaxies | 0.00, 0.85, 0.85 | 1.00, 0.70, 0.20 | 0.45, 0.10, 1.00 | 0.90, 0.50, 0.00 |
| add / subtract | 0.50, 0.70, 0.75 | 0, 0, 0 | 0.25, 0.55, 1.00 | 0.00, 0.50, 0.00 |
| bleed | 0.60, 0.80, 0.80 | 1.00, 1.00, 0.00 | 0.25, 0.55, 1.00 | 1.00, 0.80, 0.00 |
| fine structure | 0.28, 0.38, 0.21 | 0, 0, 0 | 1, 1, 1 | 1, 1, 1 |
| depth | 0.90, 0.90, 0.90 | 0.20, 0.20, 0.80 | 0.00, 0.30, 0.80 | 1.00, 0.70, 0.00 |

A pattern that targets regime 2, say, would send the galaxy colour
`(0.45, 0.10, 1.00)` to this scene with `auto_second` off and `rig_color_2`
black, and the picture is regime 2's galaxies on regime 2's background. The
other four roles will be this scene's derivation rather than the table's —
close, not identical. Identical means running the original scene and pinning
its regime with a preset, and accepting that the rig then has to be told
which preset is up.

## How six colours come from one

`rigPalette()` in `main.glsl`:

| role | from |
|---|---|
| galaxies | `rig_color` — the part of the picture a person points at |
| base wash | the second colour × `base_amount` |
| bleed | the galaxy colour lightened 40 % towards white |
| fine structure | the second colour lightened 50 % towards white (only visible with `radial_grid` on) |
| add / subtract tint | halfway between the two, at 70 % |
| depth | 35 % of the way from the galaxy colour to the second |

The **second colour** is, by default, the complement of the first — the other
side of the hue wheel — because that is what the sender can give you (one
fixture) and what the original pairs in regime 1 (orange galaxies on sky
blue). Turn `auto_second` off and it reads `rig_color_2` instead, which is
`/controls/global/color/2` — the same convention as Eclipse Churn — for the
day the sender carries two fixtures.

## Controls

| control | what it does |
|---|---|
| `rig_color` | The galaxies. First colour control; the sender's target. |
| `rig_color_2` | The wash. Second colour control. Read only with `auto_second` off. The show's `nova` cue turns `auto_second` off and sends this per palette. |
| `auto_second` | Derive the wash as the complement of `rig_color`. On by default. |
| `manual_color` / `color_by_hand` | Try a first colour while the link is live, which otherwise overwrites `rig_color` thirty times a second. Same reason as in Eclipse Chroma Key. |
| `base_amount` | How bright the wash is. 0 is the black background of regimes 2 and 3; 1 a full wash like 0 and 1. |
| `simplify`, `sandstorm`, `flashing`, `pulsate`, `radial_grid`, `beat_rotate` | The original's, unchanged. |
| `smoothing` | How softly the hi-hats move the picture; 0.6 by default, 0 is the original's jolt. See below. |

The `light` / `dark` smooth transition of the original is kept: `dark` flips
the add/subtract tint between subtracting and adding on each transition, and
is not a colour, so it was not this scene's to remove.

## The hats, followed rather than obeyed

The original feeds `syn_HighHits` straight into its feedback loop: as the gain
on the green channel, as a gate on the red, and as an offset on the
coordinates the galaxies are sampled at. A hit is a step to 1 for one frame, so
every hat shoved the whole picture sideways and back, and blinked the wash and
bleed in, because those are keyed off red at ten times gain. The picture
pumped against the music instead of moving with it, and with no bass it froze
outright, because its rotation runs on `syn_BassTime`.

So the hats now drive an envelope with a short attack and a long release,
carried from frame to frame in buffA's alpha (nothing else uses it). Their gain
on the green loop drops from 0.8 to 0.5, so a held envelope cannot bloom, and
the rotation gets a slow drift so it keeps turning through a breakdown. In an
offline render at 124 bpm with a hat on every off-beat, the worst
frame-to-frame jump fell from 1.9× the median to 1.3×, the same as with the
hats unplugged, and the picture still swells with them. `smoothing` at 0 brings
the original's hats back for comparison. The drift stays on.

## Installing

Same as the others: copy the whole `eclipse_nova.synScene` folder into the
scenes directory Synesthesia watches. On this Mac that directory *is* this
folder, so it is already installed. Drop an `eclipse_nova.png` beside
`main.glsl` for a tile in the browser; without one the tile is blank and
nothing breaks. Saving `main.glsl` hot-reloads.
