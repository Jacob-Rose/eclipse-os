// A test card for an external colour source, over OSC.
//
// This scene is not trying to look good. It is trying to answer one question -
// "is the colour arriving?" - and to tell apart the four ways that can fail,
// which from across a room otherwise look identical:
//
//   nothing moves at all ............ the scene is not rendering. Not an OSC
//                                     problem; check the app, not the sender.
//   the dot circles, all else grey .. the scene is rendering and no OSC is
//                                     landing on the first colour control.
//   the field moves, strip flat ..... a colour arrived once and then stopped.
//   one bar moves, two do not ....... the message form is wrong: r/g/b are
//                                     landing separately, or only one of them
//                                     is. Try the sender's other form.
//
// Deliberately *not* audio reactive. Everything here is either the incoming
// colour or a clock, so anything you see moving is evidence about the link and
// not about the music. That is the whole point of a test card; make it react
// to the bass and it stops being able to tell you anything.
//
// Sender, on the desk side of this repo:
//
//   python -m eclipse_dmx osc --test
//   python -m eclipse_dmx osc ../config/synesthesia_test.json --device synesthesia
//
// PASSINDEX 0 -> BuffA : a one-line rolling history of the incoming colour.
// PASSINDEX 1 -> screen: the card.

// BuffA advances one texel per rendered *frame*, not per second - there is no
// clock inside a feedback buffer - so its length in seconds is its width over
// the frame rate. At 1920 wide that is about thirty seconds at 60fps and a
// minute at 30. Long enough to watch a slow palette wave bend across it either
// way, which is the point.

// Anything below this counts as "not moving". Two OSC frames of the same slow
// wave differ by well under a percent, so the comparison is against the far
// end of the history rather than the previous frame; see linkMovement().
const float MOVEMENT_EPSILON = 0.02;


// ---- small helpers ---------------------------------------------------------
//
// Not underscore-prefixed: that prefix belongs to Synesthesia's own helper
// library, and shadowing one of those would be a confusing way to break a
// scene that has nothing to do with rectangles.

float bandMask(float value, float low, float high)
{
    return step(low, value) * step(value, high);
}

float boxMask(vec2 uv, vec2 low, vec2 high)
{
    return bandMask(uv.x, low.x, high.x) * bandMask(uv.y, low.y, high.y);
}


// ---- pass 0: the history strip ---------------------------------------------

/// Shifts the whole buffer one texel right and writes the newest colour into
/// the leftmost column.
///
/// The source is snapped to a texel centre rather than computed as `_uv` minus
/// a texel width. Both are the same arithmetic when the buffer is exactly the
/// size you assumed, and only one of them survives being wrong about that: a
/// sample landing half a texel off reads a blend of two neighbours, and a
/// buffer that feeds on itself turns that into a strip which smears to grey
/// over a few seconds. Deriving both the position and the size from the pass
/// itself means the shift is exact whatever size the buffer actually got.
vec4 renderHistory()
{
    if (_xy.x < 1.0)
    {
        return vec4(rig_color, 1.0);
    }

    vec2 source = (floor(_xy) - vec2(1.0, 0.0) + 0.5) / RENDERSIZE;
    return texture(BuffA, source);
}


// ---- pass 1: the card ------------------------------------------------------

/// How much the incoming colour has changed across the whole history.
///
/// Sampled at eight points rather than compared with the previous frame: a
/// colour crawling through a palette differs from its predecessor by far less
/// than any threshold worth setting, and a wave that happens to be at the same
/// point of its cycle as a moment ago would read as frozen. The largest
/// distance to any point in the last few seconds does not have that problem.
float linkMovement()
{
    float moved = 0.0;

    for (int i = 1; i <= 8; ++i)
    {
        vec3 past = texture(BuffA, vec2(float(i) / 8.0 * 0.99, 0.5)).rgb;
        moved = max(moved, length(rig_color - past));
    }

    return moved;
}

vec4 renderCard()
{
    vec3 col = rig_color * brightness;

    if (test_card < 0.5)
    {
        // The card off: a full-bleed wash of whatever arrived. This is the
        // scene as you would actually run it behind a set, once the link is
        // known to work.
        return vec4(col, 1.0);
    }

    // A dark plate across the bottom, so the readouts stay legible whatever
    // colour is behind them - including a bright one.
    float plate = boxMask(_uv, vec2(0.0, 0.0), vec2(1.0, 0.52));
    col = mix(col, vec3(0.04), plate * 0.92);

    // ---- the rolling history, newest at the right ----
    //
    // The single most useful element here: a colour that is *changing* is the
    // only proof that a live source is driving it, rather than a control
    // someone set by hand once.
    float strip = boxMask(_uv, vec2(0.06, 0.30), vec2(0.94, 0.46));
    if (strip > 0.5)
    {
        float across = (_uv.x - 0.06) / 0.88;
        col = texture(BuffA, vec2(1.0 - across, 0.5)).rgb * brightness;
    }

    // ---- r, g and b as their own bars ----
    //
    // Three, and separately, because which of them moves is the diagnosis. A
    // sender putting three floats in one message either lands all three or
    // none; one that sends /r, /g and /b as separate messages can land some.
    for (int channel = 0; channel < 3; ++channel)
    {
        float low = 0.06 + float(2 - channel) * 0.0667;
        float bar = boxMask(_uv, vec2(0.06, low), vec2(0.76, low + 0.050));

        if (bar > 0.5)
        {
            // Picked with a mask rather than by indexing rig_color[channel].
            // Dynamic indexing into a vector is legal in desktop GLSL and not
            // in every profile a scene might be compiled under, and this costs
            // one dot product to never have to care which one is in use.
            vec3 mask = (channel == 0) ? vec3(1.0, 0.0, 0.0)
                      : (channel == 1) ? vec3(0.0, 1.0, 0.0)
                                       : vec3(0.0, 0.0, 1.0);

            float value = dot(rig_color, mask);
            float along = (_uv.x - 0.06) / 0.70;

            col = (along <= value) ? mask : vec3(0.10);
        }
    }

    // ---- the second colour control ----
    //
    // Here to prove positional addressing. The sender's default targets the
    // *first* colour control of the running scene; this is the second, and
    // watching which square moves is how you find out whether "first" means
    // what you assumed it did.
    float swatch = boxMask(_uv, vec2(0.80, 0.06), vec2(0.94, 0.26));
    col = mix(col, rig_color_2 * brightness, swatch);

    // ---- the heartbeat ----
    //
    // Driven by TIME and nothing else. If this dot is circling, the scene is
    // alive and the shader is running, so a card that is otherwise inert is
    // telling you about the sender rather than about Synesthesia. Computed in
    // pixels so it stays round at any aspect.
    // (`spark`, not `dot` - a variable by that name would shadow the built-in
    // dot() for the rest of the scope, and the bars above need it.)
    float unit = min(RENDERSIZE.x, RENDERSIZE.y);
    vec2 centre = RENDERSIZE * vec2(0.93, 0.88);
    vec2 spark = centre + vec2(cos(TIME * 1.5), sin(TIME * 1.5)) * unit * 0.035;
    float sparkMask = 1.0 - smoothstep(unit * 0.006, unit * 0.010, length(_xy - spark));
    col = mix(col, vec3(0.85), sparkMask);

    // ---- the frame ----
    //
    // Green while the colour is moving, amber while it is merely present and
    // still. Both are readable from across a room, which a number would not be.
    float edge = 1.0 - step(6.0, min(min(_xy.x, _xy.y),
                                     min(RENDERSIZE.x - _xy.x, RENDERSIZE.y - _xy.y)));
    vec3 verdict = (linkMovement() > MOVEMENT_EPSILON) ? vec3(0.1, 0.9, 0.3)
                                                       : vec3(0.9, 0.6, 0.1);
    col = mix(col, verdict, edge);

    return vec4(col, 1.0);
}


vec4 renderMain(void)
{
    if (PASSINDEX == 0)
    {
        return renderHistory();
    }

    return renderCard();
}
