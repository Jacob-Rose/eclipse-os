# Eclipse Voronoi

Shane's **Voronoi Geode** — the scene that ships with Synesthesia, as
"Perspex Web Lattice" on ShaderToy — with its crystals painted in the colour
the rig is showing. `rig_color` arrives over OSC from `eclipse-dmx`; the rim
light, the environment reflection and the stone's own hue all follow from it
by one rule, so a colour change on the truss moves the whole picture.
Everything that is not colour is the original, untouched.

Credit where it is due: the scene is Shane's, and so are the two agate
photographs. `main.glsl` marks the five blocks eclipse-os replaced, and
`rig_amount` at 0 puts every one of them back.

## Running it

```
cd _REPOS/eclipse-os/desktop/python
python -m eclipse_dmx osc --test                       # is the app listening
python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
```

Prove the link with **Eclipse Link Test** first, for the reason Eclipse Nova's
readme gives: a red geode nobody is driving looks exactly like a red geode
being driven red, and red is this scene's default on purpose — it is where the
original sits.

## Why this scene recoloured cleanly

Worth writing down, because it is the property to look for in the next one.

By the time Shane's shader picks a colour, the surface has already been
collapsed to a single grey number:

```glsl
c.x = dot(c.xyz, vec3(.299, .587, .114));           // main.glsl, the grey
c.xyz *= vec3(min(c.x*1.5, 1.), pow(c.x, 5.), pow(c.x, 24.))*2.;
```

That second line is the whole palette. Read as arithmetic it is three
exponents; read as colour it is **red at the first weight, plus green at the
second, plus blue at the third** — three stops that add in sequence, so a
value sweeping 0..1 reads black, then red, then orange, then white. A
blackbody ramp.

A ramp over one scalar is the easiest thing in the world to re-stop. The
scalars stay Shane's; only the colours they light become ours. Compare Nova,
where the palette was four hard-coded sets drawn by lot and the work was
finding a rule to replace a random choice.

### The one thing that is not code

The crystals are also textured with two **agate photographs**, and those are
not arithmetic — they are stone. Measured: mean saturation 0.59 and 0.66, both
sitting around hue 70°, a firm yellow-olive. They multiply in *before* the
ramp, so at full strength they drag every rig colour towards stone no matter
what the ramp says.

`texture_color` is how much of that hue survives. At its default of **0** the
marbling is kept as luminance and the rig owns the colour outright; at 1 the
original's stone is back. This costs no detail at either setting — the
marbling is identical, and the marbling is what makes the surface read as
stone. What changes is whether it reads as *that* stone.

### What the recolour gives up

The original's mid-tones travel through **orange**, because red plus green is
orange. Ours travel through a paler version of whatever was sent, because for
an arbitrary hue there is no second primary to add — only white. The original
moves in hue, this moves in saturation.

That is a real difference in character and it is not recoverable: it is the
price of the hue being the rig's to name. Driven red, the ramp still tracks
the original closely — mean error under 0.14 per channel across the sweep,
against 1.25 for the obvious `mix()`-based rewrite, which is why `rigRamp()`
is written additively and not the tidy way.

## How the roles come from one colour

`rigPalette()` in `main.glsl`:

| role | from |
|---|---|
| crystal body | `rig_color` — the part of the picture a person points at |
| the pale step | what is missing from the body to reach white |
| the white tip | the body lightened 75 % towards white |
| rim light | the second colour — where the original's cool `(.5,.7,1)` edge was |
| specular | halfway from white to the tip |
| environment | the second colour, a quarter of the way back towards the first |

The **second colour** is by default the complement of the first, the same
derivation Eclipse Nova uses and for the same reason: the sender carries one
fixture. The original already pairs a warm crystal against a cool blue rim, so
the complement is what it was doing by hand. Turn `auto_second` off to read
`rig_color_2` — `/controls/global/color/2` — instead.

The environment reflection is small in area and easy to overlook, but
`renderMain` scales it by `syn_Level`, so it is what blooms across the
crystals on the loud passages. Left at the original's fixed blue it would be
the one part of the picture that ignores the truss, and it would ignore it
exactly when people are looking. The original's bass move multiplies the green
channel, which under a driven palette is a hue swing on every kick; here the
bass pushes towards the tip instead — the same brightening, no drift.

## Controls

| control | what it does |
|---|---|
| `rig_color` | The crystals. First colour control; the sender's target. |
| `rig_color_2` | Rim and environment. Second colour control. Read only with `auto_second` off. |
| `auto_second` | Derive the second as the complement of `rig_color`. On by default. |
| `manual_color` / `color_by_hand` | Try a first colour while the link is live, which otherwise overwrites `rig_color` thirty times a second. Same reason as in Eclipse Nova. |
| `rig_amount` | 0 is Shane's palette untouched, 1 is the rig's colour throughout. The two compared on one knob. |
| `texture_color` | How much of the agate's own hue survives. 0 by default. |
| `media_texture` | Use the loaded media as the crystal texture. **Off** by default — see below. |
| `alternate_stone` | Which agate the crystals are cut from. Replaces the original's `red_geode` / `green_circuits`. |
| `pulse`, `speed_motion`, `grid_growth`, `highs_shimmer`, `elevation`, `spinning`, `alternate_crystals`, `rot_distort` | The original's, unchanged. |

### Two toggles became one

The original's `red_geode` and `green_circuits` each chose a stone *and* a
colour — the second by rotating channels (`c.rgb = c.brg`), a second palette
got for free. There is no second palette to get now: the rig names the colour,
and rotating it would only be a way of ignoring what was sent. So the swizzle
is gone and the two toggles collapse to `alternate_stone`, which chooses
marbling. With both original toggles off the scene rendered black; that state
no longer exists.

### `media_texture` is off, and the original's was on

The original replaces the agate with the loaded media whenever any is loaded.
That is a good effect and it is still here, but it cannot be the default in a
show, for the reason the cue table gives: **the app has no "no media" message**,
so a cue that names no media inherits the last cue's clip. With media on by
default the geode's crystals would be cut from whatever the previous cue was
playing.

Worse, the show's way of saying "no media" is `black.mp4`, and black media
through this scene is black crystals. Off by default, the cue cannot be
surprised either way. Turn it on with `texture_color` at 1 for the original's
media mode — the clip's own colour on the crystals, no ramp.

> The same trap, in a harsher form, is why **Cloud Ten** comes up black: its
> last pass does `col *= _loadMediaAsMask().r`, so `black.mp4` multiplies the
> entire scene to nothing. See the `clouds` cue in `config/mythos-show.json`.

## Installing

Same as the others: copy the whole `eclipse_voronoi.synScene` folder into the
scenes directory Synesthesia watches. On this Mac that directory *is* this
folder, so it is already installed. Drop an `eclipse_voronoi.png` beside
`main.glsl` for a tile in the browser; without one the tile is blank and
nothing breaks. Saving `main.glsl` hot-reloads.

`"GPU": 4` — the heaviest scene in this set, and the original's own rating,
unchanged by the recolour. 32 raymarch steps, a 3×3 Voronoi inner loop, and a
normal function that calls the distance function seven times per hit. Worth
knowing when placing it in a playlist next to lighter scenes.

## Status

The shader compiles clean and the ramp is verified numerically against the
original across the luminance sweep. **It has not yet been looked at on
screen** — do that before it goes in front of anyone.
