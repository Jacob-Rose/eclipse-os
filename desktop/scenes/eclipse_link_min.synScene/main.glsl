// The smallest scene that can answer "is anything rendering at all".
//
// This exists because eclipse_link_test came up black, and that scene is the
// wrong instrument to find out why: it has a feedback buffer, a second pass,
// eight texture reads and a control that decides which half of it draws. Any
// one of those failing is a black screen, and from the front they look the
// same. So this one has none of them.
//
// What is deliberately NOT in here, and why each one is a way to be black:
//
//   no PASSES ............. nothing to render into a buffer and forget to
//                           draw to the screen. A scene whose only pass has a
//                           TARGET renders perfectly and shows nothing.
//   no texture() .......... nothing to sample from a buffer that is empty,
//                           unbound, or the wrong size.
//   no syn_FadeInOut ...... the VJ's master fade is a legitimate black screen
//                           and it is not the one we are hunting. Ignored on
//                           purpose: a fader at zero must not be able to make
//                           this scene lie.
//   no _isMediaActive ..... no branch that depends on the VJ having loaded
//                           something.
//   no loops, no helpers .. nothing that a stricter compiler than the one this
//                           was written against might reject.
//
// So if this is black, the problem is upstream of any shader we wrote: the
// scene did not load, did not compile, or the app is not drawing it. And if it
// is *not* black, every one of the things listed above becomes a suspect, in
// that order.
//
// The three things it draws, and what each one proves:
//
//   the field ....... the incoming colour. Grey means nothing has arrived,
//                     which is a working scene and a quiet sender.
//   the sweep ....... a white line crossing left to right, off TIME alone. If
//                     this moves, the scene is rendering, full stop.
//   the corner ...... a fixed magenta block that depends on nothing at all.
//                     Present in every frame this shader ever draws.

vec4 renderMain(void)
{
    // The field: whatever arrived on the colour control.
    vec3 col = rig_color * brightness;

    // The sweep: a clock, and nothing else. Two seconds a crossing, wide
    // enough to see from across a room and thin enough not to be the picture.
    float sweep = fract(TIME * 0.5);
    col = mix(col, vec3(1.0), step(abs(_uv.x - sweep), 0.006));

    // The corner: unconditional. If the shader runs, this is on screen - so a
    // frame without it is not a frame this file drew.
    if (_uv.x < 0.08 && _uv.y > 0.92)
    {
        col = vec3(0.95, 0.15, 0.65);
    }

    return vec4(col, 1.0);
}
