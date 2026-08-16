# Eclipse Link Test

A test card for driving a Synesthesia colour control from outside, over OSC.
The sender is `eclipse-dmx`, in `_REPOS/eclipse-os/desktop`, but nothing here is
specific to it — anything that speaks OSC will do.

## Setting it up, once

1. **Synesthesia → Settings → OSC**: switch OSC **input** on. It is off out of
   the box, and off means every packet is discarded by the OS with no error at
   either end — which is indistinguishable from a wrong port, a wrong address
   and a scene with no colour control. This is the first thing to check and the
   easiest to forget. OSC input is a **Pro** feature; on other licences the app
   takes MIDI instead, which this sender does not speak.
2. Note the **port** in that same panel. The sender defaults to `6000` because
   that is what the app comes up with; if yours says something else, either
   change one of them or pass `--address 127.0.0.1:<port>`.
3. Load this scene.
4. From `_REPOS/eclipse-os/desktop/python`:

   ```
   python -m eclipse_dmx osc --test
   ```

   That sweeps a hue for ten seconds with no show and no hardware involved. The
   big field should sweep with it.

Then the real thing — a rig, a look, and this scene taking its colour from it:

```
python -m eclipse_dmx osc ../config/synesthesia_test.json --device synesthesia
```

## Reading the card

| What you see | What it means |
|---|---|
| Nothing moves, not even the dot | The scene is not rendering. Nothing to do with OSC. |
| Dot circles, everything else grey | Scene is fine, no OSC is landing on control 1. In order of likelihood: input switched off, wrong port, wrong address. |
| Field is coloured, strip is flat | A colour arrived once and then stopped. |
| One bar moves, two do not | Only one channel is landing — try `--separate`. |
| Green frame | The colour has changed within the last few seconds. |
| Amber frame | A colour is present but has not moved. |

The **history strip** is the element worth trusting most: it is the only one
that distinguishes a live source from a control someone set by hand.

Grey is the "nothing arrived" default for both colour controls, chosen so that
an unconnected card looks unmistakably unconnected rather than merely dark.

## The addressing, which is the part that bites

The sender's default target is:

```
/controls/global/color/1
```

`1` is **positional** — the first colour control *in the order this scene's
`CONTROLS` array declares them*, not a control named "1". In `scene.json` that
is `rig_color`, and `rig_color_2` is `2`. Reorder the array and the sender
silently points somewhere else.

Two consequences worth knowing before a set:

- Positional addresses do not survive a **scene change**. Another scene's first
  colour control is whatever it happens to be. For a fixed set list that is
  fine; for improvising it is not.
- The stable alternative is by name — `/controls/scene/rigcolor` — at the cost
  of one address per scene. Synesthesia lowercases names and drops underscores
  for OSC, so `rig_color` is `rigcolor`. Pass it with `--control`.

## Turning it into something you would actually project

Set **test_card** off. The overlay disappears and the scene becomes a full-bleed
wash of the incoming colour — which is not much of a look on its own, but it is
the right starting point to paste into a real shader: everywhere that shader
picks a colour, `rig_color` is now available as a `vec3` in `0..1`.

**Eclipse Chroma Key**, next door, is that shader: the VJ's media in black and
white except where it already matches `rig_color`, within a limit. It declares
its key colour under the same name and in the same position, so nothing about
the sender's command line changes between the two.

## Why it is not audio reactive

Everything on this card is either the incoming colour or a clock. Nothing reads
`syn_BassLevel` or any of its relatives, on purpose: a card that moves with the
music cannot tell you whether it is also moving with your sender. Once the link
is proven, audio reactivity belongs in the scene you build next, not in the
instrument you used to prove it.
