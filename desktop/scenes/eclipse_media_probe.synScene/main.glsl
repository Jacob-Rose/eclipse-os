// What the app will actually give this scene, and through which call.
//
// Built because eclipse_chroma_key drew its fallback with media plainly
// loaded, and the three ways of asking "is there media" do not agree:
//
//   _isMediaActive() ........ used by vibe_thresholds
//   syn_MediaType >= 0.5 .... used by PixelPopArt, which has no MEDIA key at
//                             all in its scene.json
//   _exists(syn_UserImage) .. used by media_key_&_cloner_fx for the still
//                             assigned to the image slot
//
// Nothing in the docs says which one covers a still, a video, a webcam or an
// NDI feed. This scene stops the argument by showing all of them side by side,
// live, against whatever is actually loaded.
//
// It keys nothing, masks nothing and has no controls worth turning. Read it and
// then go and fix the scene that matters.
//
//   TOP LEFT  half ....... _loadMedia().rgb        (video / webcam path)
//   TOP RIGHT half ....... _loadUserImage().rgb    (still image path)
//   bottom strip, left ... two flags: media active, user image exists
//   bottom strip, right .. syn_MediaType, as a bar of six segments
//
// A half that is black is a call returning nothing. A half with a picture in it
// is the call the real scene should be using.

vec4 renderMain(void)
{
    vec3 col = vec3(0.02);

    // ---- the two sources, side by side ----
    //
    // Both sampled unconditionally and on every pixel, whatever the flags say.
    // The flags are the thing under test, so a picture that only draws when a
    // flag says it may would prove nothing at all.
    if (_uv.y > 0.16)
    {
        // Each call is left to correct its own coordinates - that is part of
        // what is being tested. Splitting the screen only decides which of the
        // two gets drawn here.
        if (_uv.x < 0.5)
        {
            col = _loadMedia().rgb;
        }
        else
        {
            col = _loadUserImage().rgb;
        }

        // A seam down the middle, so an all-black frame still shows where the
        // two halves are rather than reading as one empty scene.
        if (abs(_uv.x - 0.5) < 0.002)
        {
            col = vec3(0.35);
        }
    }
    else
    {
        // ---- flag: is media active ----
        //
        // Both flags are read with a plain `if` and not a ternary, because
        // `_exists` is something the app substitutes into the source before it
        // compiles and every scene on this machine that uses it does so as an
        // `if` condition. Copying the form that is known to work costs nothing
        // here, and this file exists to be trusted.
        if (_uv.x > 0.02 && _uv.x < 0.14)
        {
            col = vec3(0.18, 0.05, 0.05);
            if (_isMediaActive())
            {
                col = vec3(0.15, 0.85, 0.35);
            }
        }

        // ---- flag: does a user image exist ----
        if (_uv.x > 0.16 && _uv.x < 0.28)
        {
            col = vec3(0.18, 0.05, 0.05);
            if (_exists(syn_UserImage))
            {
                col = vec3(0.25, 0.55, 0.95);
            }
        }

        // ---- syn_MediaType, as six segments ----
        //
        // A number this scene cannot print. Six because the scenes on this
        // machine branch on values up to about 4 - see the ladder in dripz -
        // and a bar that runs out of room would be its own kind of lie.
        if (_uv.x > 0.34 && _uv.x < 0.98)
        {
            float segment = floor((_uv.x - 0.34) / 0.64 * 6.0);
            float gap = fract((_uv.x - 0.34) / 0.64 * 6.0);

            col = (gap > 0.08 && syn_MediaType >= segment + 0.5)
                ? vec3(0.95, 0.65, 0.10)
                : vec3(0.10);
        }
    }

    return vec4(col, 1.0);
}
