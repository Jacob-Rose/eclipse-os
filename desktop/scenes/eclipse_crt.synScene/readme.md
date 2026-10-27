# eclipse_crt

Your media, run through a CRT. The text you bake into the media comes out the
far side with scanlines, a slot mask and a bit of tube warp on it — an arcade
monitor rather than a flat panel.

The filter is Timothy Lottes' **Retro Video Monitor** (RVM, public domain). It
is unchanged in its maths; `main.glsl` only rewires it from ShaderToy's world
(one full-res texture, `mainImage`, `iResolution`) into Synesthesia's (a real
low-res buffer, `renderMain`, `RENDERSIZE`). Read the header of `main.glsl` for
exactly what moved and why.

## How it is built

Two passes, because RVM is an up-sampler and needs a genuinely low-res input:

```
PASS 0  -> BuffA (640x360)  the source: loaded media, or a tint-able fallback
FINAL   -> screen           RVM upscales BuffA to 1080 with the CRT look
```

`BuffA` is the tube. 640×360 gives 360 scanlines across a 1080 output, ~3× —
low enough to read as a CRT, 16:9 so media is not stretched. Change its size in
`scene.json` for a finer or chunkier tube.

## Controls

| control | what it does |
|---|---|
| `crt_enable` | The filter on/off. Off is a clean up-sample of the same buffer — toggle it for an honest before/after. |
| `warp` | Curvature. 0 flat, 1 the arcade default, above 1 bulges and vignettes the corners. Live. |
| `blur` | Horizontal spot size. 0.50 soft beam, 0.75 default, 1.00 hard pixel. Live. |
| `brightness` | Output level. A slot mask eats ~⅔ of the light making its grid, so a CRT run wants headroom above 1 to match the clean pass. |
| `rig_color` | Only shows through the no-media fallback, where it tints the test bars — so the OSC colour link is still visibly alive. Named and declared first like the other Link scenes. |

## The mask style is committed, on purpose

RVM has three looks — PVM scanlines, Wega grille, arcade slot mask — and each is
a different raster geometry selected by a compile-time `#if` (`RVM_MODE` at the
top of `main.glsl`). There is no cheap per-fragment way to switch a whole raster,
so this scene **commits to arcade** (`RVM_MODE 2`) and leaves the things RVM
*does* take as runtime arguments — warp, blur, on/off — as live controls. Want a
different tube? Change that one number and save; Synesthesia hot-reloads.

## Presets, not extra scenes

"A few looks" is what Synesthesia presets are for: same shader, saved control
values. An *arcade* preset (crt on, warp 1), a *clean* preset (crt off), a
*flat CRT* preset (crt on, warp 0) are three saves, not three files — the format
has no way to share shader code across scenes anyway, so one configurable scene
plus presets is the whole toolkit.

## If it comes up dark or oversaturated

RVM works in linear light and this scene converts back to sRGB on output
(`RVM_OUTPUT_SRGB` at the top). If your build of Synesthesia already
gamma-corrects the shader's output, that is a double correction — set
`RVM_OUTPUT_SRGB 0` and save.

## Installing

Same as the others: copy the whole `eclipse_crt.synScene` folder into the
scenes directory Synesthesia watches. Drop an `eclipse_crt.png` thumbnail in
beside `main.glsl` if you want a tile in the browser; without one the tile is
just blank, nothing breaks. Saving `main.glsl` hot-reloads.
