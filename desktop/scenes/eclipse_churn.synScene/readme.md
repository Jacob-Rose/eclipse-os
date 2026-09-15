# eclipse_churn

Synesthesia's bundled **Churning** (Victor S.), painted in two colours the rig
chooses. The simulation is untouched — it is the original `main.glsl` with a
header, one function, and one line added at the end of `mainImage`. See the
header of `main.glsl` for what and why.

Churning has no palette: the rainbow is the feedback buffer's own state, read
out as colour. So the paint job is on the *readout*: each pixel keeps its
brightness as texture, and its hue decides which of the two colours it is
painted with (red at one end of the hue circle, blue at the other, mixes
between). The dynamics — the churn — are exactly Victor's.

## Controls

| control | OSC | what it does |
|---|---|---|
| `rig_color` | `/controls/scene/rigcolor`, `/controls/global/color/1` | The rig's colour. Default red. |
| `rig_color_2` | `/controls/scene/rigcolor2`, `/controls/global/color/2` | The second colour. Default blue. Nothing sends it yet. |
| `rig_amount` | `/controls/scene/rigamount` | 0 is stock Churning, 1 is only the two colours. |
| `rig_contrast` | `/controls/scene/rigcontrast` | Puts the hue variation the palette throws away back as light and dark. |

Everything else is Churning's own.

## Not done

- `eclipse-dmx` sends one fixture. Driving `rig_color_2` from a second fixture
  is a small change in `viewer._send_osc` / `osc.send_color`.
- `direction` and `colRegSel` are `HARD_TRANSITIONS`: the app randomises them
  on scene entry, so the base look differs each time the scene is cued. For a
  show, pin `colRegSel` in `scene.json`.
