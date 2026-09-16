# scenes

Synesthesia scenes that take their colour from the rig.

`eclipse-dmx` can send the colour of one fixture out as OSC while a show runs
(`python -m eclipse_dmx osc`, and `readme.md` in `desktop/` for the rest of it).
These are the other end of that: scenes with a colour control at a known
address, so the visuals behind the truss are tinted by the same render that is
lighting it — one look, two surfaces, neither side knowing about the other.

| scene | what it is |
|---|---|
| `eclipse_link_min.synScene` | The smallest thing that can render. Answers "is anything drawing at all". Run this when something else comes up black. |
| `eclipse_link_test.synScene` | A test card. Answers "is the colour arriving, and is it changing", and tells apart the ways that can fail. **Currently comes up black — under diagnosis; see the minimal scene's readme.** |
| `eclipse_chroma_key.synScene` | The look. Your media in black and white except where it already matches the rig's colour, within a limit. |
| `eclipse_churn.synScene` | Synesthesia's bundled Churning (Victor S.), painted in two rig colours instead of its own rainbow. First test of taking a shipped feedback scene and recolouring its readout. |
| `eclipse_nova.synScene` | Synesthesia's bundled Nova (Felix Woitzel), its four random colour regimes replaced by a palette derived from the rig's colour — the galaxies are `rig_color`, the other five roles follow by one rule. Its readme explains why the rig drives the shader and not the reverse. |
| `eclipse_crt.synScene` | A CRT styliser. Your media — with text baked into it — run through Timothy Lottes' Retro Video Monitor in arcade slot-mask mode: scanlines, a slot mask and tube warp. Warp, spot size and brightness are live controls. |
| `eclipse_voronoi.synScene` | Synesthesia's bundled Voronoi Geode (Shane), its fiery blackbody ramp re-stopped on the rig's colour and the agate photographs kept as marbling but stripped of their own hue. Its readme explains why a scene whose palette is a ramp over one grey value is the easiest kind to recolour — and what a hue-driven original gives up to become colour-driven. |

They are in that order on purpose: each one adds exactly what the one above it
left out, so a scene that fails tells you which addition broke it. Minimal has
no passes and no textures; the test card adds a feedback buffer and a second
pass; the chroma key adds media.

Each has its own `readme.md`. The short version:

```sh
cd desktop/python
python -m eclipse_dmx osc --test                       # is the app listening
python -m eclipse_dmx osc ../config/synesthesia_test.json --device synesthesia
python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
```

## Installing one

A scene is a **folder** ending in `.synScene`. Copy the whole folder into the
scenes directory Synesthesia is watching — the app names it in its settings; on
this machine it is `%USERPROFILE%\Videos\obscuria`. It appears in the browser
without a restart, and saving `main.glsl` hot-reloads the shader.

They are kept here as well as there because a scene is part of this feature: the
control it exposes is the sender's target, and the two are one thing that only
works if both halves agree. The copy in the scenes directory is the one the app
loads — edit either, but do not let them drift.

## Both scenes name the key colour `rig_color`

Not decoration. The sender's default address is **positional** —
`/controls/global/color/1` is the first colour control in the order a scene's
`CONTROLS` array declares them — which means it points somewhere different in
every scene, and does not survive a scene change. The stable alternative is by
name:

```sh
python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia \
    --control /controls/scene/rigcolor
```

Synesthesia lowercases control names and drops underscores for OSC, so
`rig_color` is `rigcolor`. Because both scenes use that name, and declare it
first, either address drives either scene. A third scene should do the same.
