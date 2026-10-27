// The rig's colour, used as a chroma key on the VJ's media.
//
// One idea: the media is black and white *except* where it already matches the
// colour the lights are showing, within a limit. So the truss and the screen
// agree on one colour and only that colour is alive - the picture stops being
// wallpaper behind the rig and becomes the same instrument.
//
// The key colour is `rig_color`, the first colour control in scene.json, which
// is what makes it /controls/global/color/1 - the address eclipse-dmx sends to:
//
//   python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
//
// Nothing here is specific to that sender. Set the colour by hand and it is an
// ordinary selective-colour effect; the point is that it does not have to be.
//
// Prove the link first with the eclipse_link_test scene next door. This one
// cannot tell you whether a colour is arriving - a key that matches nothing
// looks exactly like a key nobody is driving - which is the whole reason that
// scene exists.

#define TAU (2.0 * PI)

// The largest distance the three metrics below can return, so `color_limit`
// means roughly the same fraction of "completely different colour" whichever
// one is selected. Without this, switching mode silently rescales the knob.
const float MAX_RGB_DISTANCE = 1.7320508;   // length(vec3(1)) - black to white
const float MAX_CHROMA_DISTANCE = 1.0552;   // red to cyan, in the UV plane

// Below this saturation a pixel has no meaningful hue - it is grey, and the
// angle you get back is whatever rounding left in the low bits. Hue matching
// treats anything under this as "not a match" rather than as "matches
// everything", which is what an ungated hue comparison does to every shadow in
// the frame.
const float HUE_NEEDS_SATURATION = 0.12;


// ---- how close is this pixel to the key ------------------------------------
//
// Three metrics, because "the same colour" means three different things
// depending on the footage, and none of them is right for all of it.

/// The chrominance half of YUV: which colour, with the brightness thrown away.
///
/// This is what a real chroma key compares, and the reason is shadows. A green
/// screen in shade and the same screen in a hotspot are far apart in RGB and
/// almost on top of each other here, so one limit covers both.
vec2 chromaOf(vec3 rgb)
{
    return vec2(dot(rgb, vec3(-0.169, -0.331,  0.500)),
                dot(rgb, vec3( 0.500, -0.419, -0.081)));
}

/// Distance around the hue wheel, ignoring how pale or how bright.
///
/// The loosest of the three, and the one to reach for when the rig is washing
/// a room: what comes back off a wall is the same hue as what left the par and
/// nothing like it in saturation. Gated on saturation - see the constant.
float hueDistance(vec3 pixel, vec3 key)
{
    vec3 a = _rgb2hsv(pixel);
    vec3 b = _rgb2hsv(key);

    // Shorter way round the wheel: red at 0.98 and red at 0.02 are neighbours,
    // and a plain subtraction calls them opposites.
    float apart = abs(a.x - b.x);
    apart = min(apart, 1.0 - apart) * 2.0;

    float grey = 1.0 - smoothstep(0.02, HUE_NEEDS_SATURATION, a.y);
    return mix(apart, 1.0, grey);
}

/// 0 where this pixel is the key colour, 1 where it is as far from it as the
/// selected metric can measure.
float keyDistance(vec3 pixel, vec3 key)
{
    if (match_mode < 0.5)
    {
        return length(chromaOf(pixel) - chromaOf(key)) / MAX_CHROMA_DISTANCE;
    }

    if (match_mode < 1.5)
    {
        return hueDistance(pixel, key);
    }

    return length(pixel - key) / MAX_RGB_DISTANCE;
}


// ---- what to show when the VJ has loaded nothing ---------------------------

/// A hue wheel, so the scene says something with no media loaded.
///
/// Deliberately every hue at once: whatever the key colour is, exactly one
/// wedge of this survives in colour, and driving the key sweeps that wedge
/// round like a radar. That makes it a usable bench check for the link - which
/// a still frame of anything real would not be - without pretending to be a
/// test card. The rings breathe on the bass so the scene is not inert while
/// you are looking at it.
vec3 hueWheel()
{
    vec2 p = _uvc * 2.0;
    float radius = length(p);

    float hue = fract(atan(p.y, p.x) / TAU + syn_Time * 0.02);
    float saturation = clamp(radius * 1.6, 0.0, 1.0);
    float value = 0.25 + 0.75 * _nsin(radius * 7.0 - syn_BassTime * 0.6);

    // Dark in the middle and towards the corners, so the wheel reads as an
    // object rather than as a full-bleed gradient - and so there is somewhere
    // for the black and white to actually be black.
    value *= smoothstep(0.0, 0.35, radius) * (1.0 - smoothstep(0.85, 1.6, radius));

    return _hsv2rgb(vec3(hue, saturation, value));
}


/// What the VJ loaded, whichever slot they loaded it into.
///
/// Two slots, and asking about only one of them is why this scene came up
/// showing the wheel with a picture plainly loaded. `_isMediaActive()` answers
/// for **video and webcams**; a still assigned to the image slot is not "media"
/// by its reckoning and has to be asked about separately, through
/// `_exists(syn_UserImage)`. Every shipped scene that handles both does exactly
/// this, in this order - a video is the more specific thing to have loaded, so
/// it wins when somehow both are set.
/// The flags are advice, and `media_source` is the override for when they are
/// wrong.
///
/// Learned from eclipse_media_probe: that scene samples both slots
/// unconditionally and showed the footage, while this one asked the flags first
/// and drew the wheel. So a sampler can hold a picture that every flag denies
/// exists - which means gating on them is a scene that fails for a reason the
/// VJ can neither see nor fix. Hence: ask them, but let anyone overrule them.
vec3 sourcePixel()
{
    // ---- forced ----
    if (media_source > 2.5)
    {
        return hueWheel();
    }

    if (media_source > 1.5)
    {
        return _loadUserImage().rgb;
    }

    if (media_source > 0.5)
    {
        return _loadMedia().rgb;
    }

    // ---- auto ----
    //
    // Every signal the app offers, because no two shipped scenes agree on
    // which one to trust: _isMediaActive() is what vibe_thresholds uses,
    // syn_MediaType >= 0.5 is what PixelPopArt uses, and media_key asks
    // _exists(syn_UserImage) for the still-image slot. Any of them saying yes
    // is treated as yes.
    if (_isMediaActive() || syn_MediaType >= 0.5)
    {
        return _loadMedia().rgb;
    }

    if (_exists(syn_UserImage))
    {
        return _loadUserImage().rgb;
    }

    return hueWheel();
}


vec4 renderMain(void)
{
    vec3 media = sourcePixel();

    // ---- which colour is the key ----
    //
    // The rig's, or one set by hand. Two reasons the second exists, and both
    // are about being able to see the effect at all:
    //
    // A running sender writes rig_color thirty times a second, so once the
    // link is up that control cannot be moved by hand - the scene becomes
    // untestable exactly when it starts working. And **grey and white cannot
    // be keyed**: a neutral colour has no chrominance and no meaningful hue,
    // so nothing in any footage is near it, nothing survives, and the frame
    // goes entirely monochrome. That is correct behaviour and it looks
    // identical to a broken scene. A rig sitting on a white cue - mythos26
    // opens on one - leaves no way to tell those apart without this toggle.
    vec3 key = mix(rig_color, manual_key, key_by_hand);

    // ---- the mask ----
    //
    // A bass hit widens the limit rather than brightening anything: more of the
    // frame passes the test for a moment, so the colour spreads on the beat and
    // pulls back. Louder-is-brighter would fight the rig for the same job.
    float limit = color_limit + beat_widen * syn_BassHits * 0.35;

    // +0.001 so a feather of zero is a hard edge rather than a smoothstep with
    // both edges in the same place, which is undefined and comes out as a
    // stripe of noise along the boundary on some drivers.
    // `offKey` and not `distance`: a variable by that name shadows the built-in
    // distance() for the rest of the scope, which is a confusing way to break a
    // line added underneath it later.
    float offKey = keyDistance(media, key);
    float matched = 1.0 - smoothstep(limit, limit + feather + 0.001, offKey);

    // Which side of the key keeps its colour. Off: the matching colour is the
    // one that survives - the rig picks what stays alive. On: it is the one
    // that is removed - the rig picks what gets erased, which is the greenscreen
    // reading of the same control and looks completely different on the same
    // footage.
    float keep = mix(matched, 1.0 - matched, invert_key);

    // ---- the two halves ----

    float luma = _luminance(media);

    // Monochrome, tinted. mono_tint is doubled so its default of a mid grey is
    // neutral: at 0.5 this is exactly the luminance, and moving it from there
    // pushes the black and white cool or warm without changing its brightness.
    vec3 mono = mix(media, luma * mono_tint * 2.0, desaturate);

    // The kept side, in the media's own colour or repainted in the key's.
    // Repainting takes the key's hue and saturation and keeps the *pixel's*
    // brightness, so the picture's shape survives - flooding it with a flat
    // flat key colour would key a hole in the frame rather than colour it.
    vec3 pixelHsv = _rgb2hsv(media);
    vec3 keyHsv = _rgb2hsv(key);
    vec3 kept = mix(media, _hsv2rgb(vec3(keyHsv.x, keyHsv.y, pixelHsv.z)), paint_with_key);

    // Lifted, because the whole frame around it is grey and a merely-correct
    // colour reads as washed out in that company.
    vec3 boosted = _rgb2hsv(kept);
    boosted.y = clamp(boosted.y * color_boost, 0.0, 1.0);
    kept = _hsv2rgb(boosted);

    vec3 col = mix(mono, kept, keep);

    // The mask on its own, which is how you set the limit: turn it on, widen
    // until the thing you want is solid white and the rest is solid black.
    col = mix(col, vec3(keep), show_mask);

    // The VJ's master fade is opt-in, and off by default.
    //
    // It used to be an unconditional multiply here, and it made this the only
    // scene of the four that could go entirely black with nothing wrong with
    // it - a fader at zero, or a deck that is not on air, and the whole thing
    // is `col * 0.0`. Everything else in this folder ignores the fade, so the
    // symptom was "this one scene is black and the rest are fine", which reads
    // as a broken scene and is not one.
    //
    // Respecting the fade is a real thing to want; being unable to tell it
    // from a fault is not. So it is a switch, and it starts off.
    return vec4(col * brightness * mix(1.0, syn_FadeInOut, respect_fade), 1.0);
}
