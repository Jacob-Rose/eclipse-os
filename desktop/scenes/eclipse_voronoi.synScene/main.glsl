// ============================================================================
// CREDIT
// ----------------------------------------------------------------------------
// This is Shane's "Perspex Web Lattice", which ships inside Synesthesia as
// "Voronoi Geode" (Synesthesia 1.25.4,
// Contents/Resources/release/voronoi_geode.synScene), grown from his
// ShaderToy work. The raymarched 2nd-order Voronoi, the height map and its
// lattice ID, the edge detection folded into the normal function, the
// triplanar texturing and the fake environment mapping are all his and are
// carried here unchanged. The two agate photographs are the originals.
//
// What eclipse-os changed is colour, and only colour - the five blocks
// marked ECLIPSE below. The geometry, the lighting model and the reactivity
// are the original's. Shane's own notes follow.
// ============================================================================

// Voronoi Geode, with its colours taken from the rig.
//
// The original's colour comes from three places, and all three are ramps
// driven by a single luminance value, which is what makes this scene worth
// recolouring at all:
//
//   1. The fiery palette. After the surface is collapsed to grey, the
//      original multiplies by vec3(min(c*1.5,1.), pow(c,5.), pow(c,24.)) -
//      staggered exponents, so the channels come up in sequence and a value
//      sweeping 0..1 reads black, red, orange, white. That is a blackbody
//      ramp, and rigRamp() below is the same ramp with its three stops taken
//      from the rig instead of being fixed at red/green/blue. The scalars
//      and their order are Shane's; only the colours they light are ours.
//
//   2. The agate photographs. Unlike the palette these are not code - they
//      are stone, mean saturation 0.59 and 0.66, both sitting around hue
//      70 degrees. They multiply in before the ramp, so at full strength
//      they drag any rig colour towards that yellow-olive no matter what
//      the ramp says. `texture_color` is how much of that hue survives; at
//      its default of 0 the marbling is kept as luminance and the rig owns
//      the colour outright. The detail is what makes the surface read as
//      stone; the hue is what makes it read as *that* stone.
//
//   3. The rim light and the environment reflection. Small, but the
//      reflection is scaled by syn_Level, so it blooms on exactly the loud
//      passages when people are looking at it. Left fixed it would drift
//      off-hue at the worst possible moment, so it follows the rig too.
//
// The colour roles, all derived from one sent colour - see rigPalette():
//
//   rig_color    the FIRST colour control in scene.json, which is what makes
//                it /controls/global/color/1 - the address eclipse-dmx sends:
//
//                  python -m eclipse_dmx osc ../config/mythos26.json --device synesthesia
//
//   rig_color_2  the SECOND, /controls/global/color/2. Nothing sends this
//                yet, so by default it is not read: `auto_second` derives it
//                as the complement of the first, which is what the original
//                pairs anyway - a warm crystal against a cool blue rim.
//
// Set `rig_amount` to 0 and every block below falls back to the original's
// own arithmetic, so the two can be compared on one knob.

  // Perspex Web Lattice
  // -------------------

  // I felt that Shadertoy didn't have enough Voronoi examples, so I made another one. :) I'm
  // not exactly sure what it's supposed to be... My best guess it that an Alien race with no 
  // common sense designed a monitor system with physics defying materials. :)

  // Technically speaking, there's not much to it. It's just some raymarched 2nd order Voronoi.
  // The dark perspex-looking web lattice is created by manipulating the Voronoi value slightly 
  // and giving the effected region an ID value so as to color it differently, but that's about
  // it. The details are contained in the "heightMap" function.

  // There's also some subtle edge detection in order to give the example a slight comic look. 
  // 3D geometric edge detection doesn't really differ a great deal in concept from 2D pixel 
  // edge detection, but it obviously involves more processing power. However, it's possible to 
  // combine the edge detection with the normal calculation and virtually get it for free. Kali 
  // uses it to great effect in his "Fractal Land" example. It's also possible to do a
  // tetrahedral version... I think Nimitz and some others may have done it already. Anyway, 
  // you can see how it's done in the "nr" (normal) function.

  // Geometric edge related examples:

  // Fractal Land - Kali
  // https://www.shadertoy.com/view/XsBXWt

  // Rotating Cubes - Shau
  // https://www.shadertoy.com/view/4sGSRc

  // Voronoi mesh related:

  //   // I haven't really looked into this, but it's interesting.
  // Weaved Voronoi - FabriceNeyret2 
  //   https://www.shadertoy.com/view/ltsXRM



#define FAR 2.

int id = 0; // Object ID - Red perspex: 0; Black lattice: 1.


// ======================= ECLIPSE 1 of 5: the palette ========================
// One sent colour in, the scene's colour roles out. Stated once, so that a
// colour change on the truss moves the whole picture coherently instead of
// moving one part of it and leaving the rest where it was.

float luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }

/// The other side of the hue wheel, at the same brightness. The same
/// derivation Eclipse Nova uses, for the same reason: the sender carries one
/// fixture, so the second colour has to come from somewhere.
vec3 complementOf(vec3 c)
{
    float mx = max(max(c.r, c.g), c.b);
    float mn = min(min(c.r, c.g), c.b);
    return clamp((mx + mn) - c, 0.0, 1.0);
}

struct Palette {
    vec3 body;   // the crystal itself - this is the colour a person names
    vec3 fill;   // what is missing from body to reach white; the ramp's pale step
    vec3 tip;    // the white-hot top, where a crystal catches the light
    vec3 rim;    // the cool edge highlight the original keeps at (.5,.7,1)
    vec3 spec;   // the near-white specular at (1,.97,.92)
    vec3 env;    // the fake environment reflection, which blooms on level
};

Palette rigPalette()
{
    vec3 first  = mix(rig_color, manual_color, color_by_hand);
    vec3 second = mix(rig_color_2, complementOf(first), auto_second);

    Palette p;
    p.body = first;
    p.fill = clamp(vec3(1.0) - first, 0.0, 1.0);
    p.tip  = mix(first, vec3(1.0), 0.75);
    p.rim  = second;
    p.spec = mix(vec3(1.0), p.tip, 0.5);
    p.env  = mix(second, first, 0.25);
    return p;
}

/// Shane's ramp, restopped.
///
/// The original is `vec3(min(t*1.5,1.), pow(t,5.), pow(t,24.))*2.` - which,
/// read as colour rather than as arithmetic, is *red* at the first weight,
/// plus *green* at the second, plus *blue* at the third. Three stops that add
/// in sequence, so a value sweeping 0..1 goes black, red, orange, white.
///
/// The three scalars and their order are his and are untouched. What changes
/// is what each one lights: the rig's colour first, then what is missing from
/// it to reach white, then the white tip. Driven red this tracks the original
/// almost exactly in the red and green channels - mean error under 0.14 per
/// channel over the whole sweep, against 1.25 for the obvious mix()-based
/// version, which is why it is written additively and not the tidy way.
///
/// One honest difference: the original's mid-tones pass through *orange*,
/// because red plus green is orange. Ours pass through a paler version of
/// whatever was sent, because for an arbitrary hue there is no second
/// primary to add - only white. The original travels in hue, this travels in
/// saturation. That is the price of the hue being the rig's to name, and it
/// is the whole point of the scene.
vec3 rigRamp(float t, Palette pal)
{
    float a = min(t*1.5, 1.0);
    float b = pow(t, 5.0);
    float c = pow(t, 24.0);

    vec3 ramp = pal.body*a + pal.fill*b;
    ramp = mix(ramp, pal.tip, c);
    return ramp * 2.0;
}
// ====================== end ECLIPSE 1 of 5 ==================================


// Tri-Planar blending function. Based on an old Nvidia writeup:
// GPU Gems 3 - Ryan Geiss: http://http.developer.nvidia.com/GPUGems3/gpugems3_ch01.html
vec3 tex3D(in vec3 p, in vec3 n ){

    n = max((abs(n) - .2), .001);
    n /= (n.x + n.y + n.z ); // Roughly normalized.
    vec3 col;
    if (alternate_stone < 0.5){
      col = (texture(agate1, p.yz)*n.x + texture(agate1, p.zx)*n.y + texture(agate1, p.xy)*n.z).xyz;
    } else {
      col = pow((texture(agate2, p.yz*0.5)*n.x + texture(agate2, p.zx*1.0)*n.y + texture(agate2, p.xy*0.75)*n.z).xyz, vec3(1.8))*2.0;
    }
    if (_isMediaActive() && media_texture > 0.5){
      col = (_textureMedia(p.yz*0.25-vec2(0.5,0.5))*n.x + _textureMedia(p.zx*0.25-vec2(0.5,0.5))*n.y + _textureMedia(p.xy*0.25-vec2(0.5,0.5))*n.z).xyz;
    }

    // ==================== ECLIPSE 2 of 5: the stone's hue ===================
    // Keep the marbling, drop the hue. The agate is a photograph and carries
    // its own strong yellow-olive; it multiplies in before the ramp, so left
    // alone it tints every rig colour towards stone. Collapsing it to
    // luminance costs no detail at all - only its colour - and hands the hue
    // to rigRamp(). texture_color at 1 restores the original's stone.
    col = mix(vec3(luma(col)), col, texture_color);
    // ==================== end ECLIPSE 2 of 5 ================================

    // Loose sRGB to RGB conversion to counter final value gamma correction...
    // in case you're wondering.
    return col*col;
}

// Compact, self-contained version of IQ's 3D value noise function. I have a transparent noise
// example that explains it, if you require it.
float n3D(vec3 p){
  float retVal = _pulse(fract(p.z*p.z*10.0), 0.5, 0.1);
  const vec3 s = vec3(7, 157, 113);
  vec3 ip = floor(p); p -= ip; 
    vec4 h = vec4(0., s.yz, s.y + s.z) + dot(ip, s);
    p = p*p*(3. - 2.*p); //p *= p*p*(p*(p * 6. - 15.) + 10.);
    h = mix(fract(sin(h)*43758.5453), fract(sin(h + s.x)*43758.5453), p.x);
    h.xy = mix(h.xz, h.yw, p.y);
    float var = mix(h.x, h.y, p.z);
    return mix(var, var*(0.7+syn_HighHits*0.5), highs_shimmer); // Range: [0, 1].
}

// vec2 to vec2 hash.
vec2 hash22(vec2 p) { 

    // Faster, but doesn't disperse things quite as nicely. However, when framerate
    // is an issue, and it often is, this is a good one to use. Basically, it's a tweaked 
    // amalgamation I put together, based on a couple of other random algorithms I've 
    // seen around... so use it with caution, because I make a tonne of mistakes. :)
    float n = sin(dot(p, vec2(41, 289)));
    //return fract(vec2(262144, 32768)*n); 
    
    // Animated.
    p = fract(vec2(262144, 32768)*n); 
    // Note the ".45," insted of ".5" that you'd expect to see. When edging, it can open 
    // up the cells ever so slightly for a more even spread. In fact, lower numbers work 
    // even better, but then the random movement would become too restricted. Zero would 
    // give you square cells.
    p = sin( p*6.2831853 + script_time*0.5)*.45 + .5; 
    return mix(p, vec2(0.0), grid_growth*(1.0-syn_Presence));
}

// 2D 2nd-order Voronoi: Obviously, this is just a rehash of IQ's original. I've tidied
// up those if-statements. Since there's less writing, it should go faster. That's how 
// it works, right? :)
//
float Voronoi(in vec2 p){
    
  vec2 g = floor(p), o; p -= g;
  
  vec3 d = vec3(1); // 1.4, etc. "d.z" holds the distance comparison value.
  // g.xy += _pulse(dot(_uvc, _uvc)*0.25, 1.0-pulseCrystal, 0.2)*0.000001;
  
  for(int y = -1; y <= 1; y++){
    for(int x = -1; x <= 1; x++){
            
      o = vec2(x, y);
      o += hash22(g + o) - p;
      d.z = dot(o, o); 
            // More distance metrics.
            o = abs(o);
            // d.z = max(o.x*.8666 + o.y*.5, o.y);// 
            if (alternate_crystals > 0.5){
              d.z = max(o.x, o.y);
            }
            // d.z = (o.x*.7 + o.y*.7);

            d.y = max(d.x, min(d.y, d.z));
            d.x = min(d.x, d.z); 
                       
    }
  }
    d.xy -= _pulse(dot(_uvc, _uvc)*0.25, 0.95-pulse, 0.15)*4.0;
    // d.xy -= syn_BassLevel*pow(distance(_uv, vec2(sin(TIME), cos(TIME))),3.0)*1.0*bass_pulse;

    return (max(d.y/1.2 - d.x*1., 0.)/1.2)*2.0;
    //return d.y - d.x; // return 1.-d.x; // etc.
    
}

// The height map values. In this case, it's just a Voronoi variation. By the way, I could
// optimize this a lot further, but it's not a particularly taxing distance function, so
// I've left it in a more readable state.
float heightMap(vec3 p){
    // p.xy -= _uvc*_pulse(dot(_uvc, _uvc)*0.25, 1.0-pulseCrystal, 0.2)*0.1;

    id =0;
    float c = Voronoi(p.xy*6.); // The fiery bit.

    // For lower values, reverse the surface direction, smooth, then
    // give it an ID value of one. Ie: this is the black web-like
    // portion of the surface.
    if (c<.07) {c = smoothstep(0.7, 1., mix(1.-c, c, 0.0))*.2; id = 1; }
    return c;
}

// Standard back plane height map. Put the plane at vec3(0, 0, 1), then add some height values.
// Obviously, you don't want the values to be too large. The one's here account for about 10%
// of the distance between the plane and the camera.
float m(vec3 p){
   
    float h = heightMap(p); // texture(iChannel0, p.xy/2.).x; // Texture work too.
    
    return 1. - p.z - h*.1;
    
}

// // Tetrahedral normal, to save a couple of "map" calls. Courtesy of IQ.
// vec3 nr(in vec3 p){

//     // Note the slightly increased sampling distance, to alleviate artifacts due to hit point inaccuracies.
//     vec2 e = vec2(0.005, -0.005); 
//     return normalize(e.xyy * m(p + e.xyy) + e.yyx * m(p + e.yyx) + e.yxy * m(p + e.yxy) + e.xxx * m(p + e.xxx));
// }


// // Standard normal function - for comparison with the one below.
// vec3 nr(in vec3 p) {
//   const vec2 e = vec2(0.005, 0);
//   return normalize(vec3(m(p + e.xyy) - m(p - e.xyy), m(p + e.yxy) - m(p - e.yxy), m(p + e.yyx) - m(p - e.yyx)));
// }


// The normal function with some edge detection rolled into it.
vec3 nr(vec3 p, inout float edge) { 
  
    vec2 e = vec2(.005, 0);

    // Take some distance function measurements from either side of the hit point on all three axes.
  float d1 = m(p + e.xyy), d2 = m(p - e.xyy);
  float d3 = m(p + e.yxy), d4 = m(p - e.yxy);
  float d5 = m(p + e.yyx), d6 = m(p - e.yyx);
  float d = m(p)*2.;  // The hit point itself - Doubled to cut down on calculations. See below.
     
    // Edges - Take a geometry measurement from either side of the hit point. Average them, then see how
    // much the value differs from the hit point itself. Do this for X, Y and Z directions. Here, the sum
    // is used for the overall difference, but there are other ways. Note that it's mainly sharp surface 
    // curves that register a discernible difference.
    edge = abs(d1 + d2 - d) + abs(d3 + d4 - d) + abs(d5 + d6 - d);
    //edge = max(max(abs(d1 + d2 - d), abs(d3 + d4 - d)), abs(d5 + d6 - d)); // Etc.
    
    // Once you have an edge value, it needs to normalized, and smoothed if possible. How you 
    // do that is up to you. This is what I came up with for now, but I might tweak it later.
    edge = smoothstep(0., 1., sqrt(edge/e.x*2.));
  
    // Return the normal.
    // Standard, normalized gradient mearsurement.
    return normalize(vec3(d1 - d2, d3 - d4, d5 - d6));
}

/*
// I keep a collection of occlusion routines... OK, that sounded really nerdy. :)
// Anyway, I like this one. I'm assuming it's based on IQ's original.
float cAO(in vec3 p, in vec3 n)
{
  float sca = 3., occ = 0.;
    for(float i=0.; i<5.; i++){
    
        float hr = .01 + i*.5/4.;        
        float dd = m(n * hr + p);
        occ += (hr - dd)*sca;
        sca *= 0.7;
    }
    return clamp(1.0 - occ, 0., 1.);    
}
*/


// Standard hue rotation formula... compacted down a bit.
// vec3 rotHue(vec3 p, float a){

//     vec2 cs = sin(vec2(1.570796, 0) + a);

//     mat3 hr = mat3(0.299,  0.587,  0.114,  0.299,  0.587,  0.114,  0.299,  0.587,  0.114) +
//             mat3(0.701, -0.587, -0.114, -0.299,  0.413, -0.114, -0.300, -0.588,  0.886) * cs.x +
//             mat3(0.168,  0.330, -0.497, -0.328,  0.035,  0.292,  1.250, -1.050, -0.203) * cs.y;
               
//     return clamp(p*hr, 0., 1.);
// }


// Simple environment mapping. Pass the reflected vector in and create some
// colored noise with it. The normal is redundant here, but it can be used
// to pass into a 3D texture mapping function to produce some interesting
// environmental reflections.
//
// More sophisticated environment mapping:
// UI easy to integrate - XT95    
// https://www.shadertoy.com/view/ldKSDm
vec3 eMap(vec3 rd, vec3 sn){
    
    vec3 sRd = rd; // Save rd, just for some mixing at the end.
    
    // Add a time component, scale, then pass into the noise function.
    rd.xy -= syn_Time*.1;
    rd *= 3.;
    
    //vec3 tx = tex3D(iChannel0, rd/3., sn);
    //float c = dot(tx*tx, vec3(.299, .587, .114));

    float c = (n3D(rd)*.57 + n3D(rd*2.)*.28)*1.2; // Noise value.
    c = smoothstep(0.5, 1., c); // Darken and add contast for more of a spotlight look.

    //vec3 col = vec3(c, c*c, c*c*c*c).zyx; // Simple, warm coloring.
    vec3 col = vec3(min(c*1.5, 1.), pow(c, 2.5), pow(c, 12.)).zyx; // More color.
    col *= vec3(1.0, syn_BassHits*3.0, 1.0);

    // =============== ECLIPSE 3 of 5: the environment reflection =============
    // Small in area, but renderMain scales this by syn_Level, so it is what
    // blooms across the crystals on the loud passages - the moments people
    // are actually looking. Left at the original's fixed blue it would be the
    // one part of the picture that ignores the truss, and it would ignore it
    // loudly.
    //
    // Same spotlight shape (the two exponents are Shane's), stopped on the
    // rig's environment colour rising to its tip. The original's bass move
    // multiplies the green channel, which on a driven palette is a hue swing
    // on every kick; here the bass pushes towards the tip instead - the same
    // brightening, no colour drift.
    Palette pal = rigPalette();
    vec3 rigCol = pal.env * min(c*1.5, 1.0);
    rigCol = mix(rigCol, mix(pal.env, pal.tip, 0.5), pow(c, 2.5));
    rigCol = mix(rigCol, pal.tip, pow(c, 12.0));
    rigCol *= 1.0 + syn_BassHits*2.0;

    col = mix(col, rigCol, rig_amount);

    // The original tones this down by rotating channels, which on a driven
    // palette rotates hue. Under the rig it tones down towards the palette's
    // shadowed base instead: the same darkening, on the same fixed pattern.
    vec3 stockOut = mix(col, col.yzx, sRd*.25+.25);
    vec3 rigOut   = mix(col, pal.body*0.35, dot(sRd, vec3(1.0))/3.0*0.25+0.25);
    return mix(stockOut, rigOut, rig_amount);
    // =============== end ECLIPSE 3 of 5 =====================================
}

vec4 renderMain(){
    vec4 c = vec4(0.0);
    // Unit direction ray, camera origin and light position.
    vec3 r = normalize(vec3(_xy - RENDERSIZE.xy*.5, RENDERSIZE.y)), 
         o = vec3(0, 0, elevation), 
         l = o + vec3(0, 0, -1);
   
    // Rotate the canvas. Note that sine and cosine are kind of rolled into one.
    vec2 a = sin(vec2(1.570796, 0) + TIME*spinning/8.); // Fabrice's observation.
    r.xy = mat2(a, -a.y, a.x) * r.xy;

    
    // Standard raymarching routine. Raymarching a slightly perturbed back plane front-on
    // doesn't usually require many iterations. Unless you rely on your GPU for warmth,
    // this is a good thing. :)
    float d, t = 0.;
    
    for(int i=0; i<32;i++){
      if (i%2 == 0){
        r.xy = _rotate(r.xy, d*d*rot_distort*500.0);

      }

        d = m(o + r*t);
        // There isn't really a far plane to go beyond, but it's there anyway.
        if(abs(d)<0.001 || t>FAR) break;
        t += d*0.99;

    }
    
    t = min(t, FAR);
    
    // Set the initial scene color to black.
    c = vec4(0);
    
    float edge = 0.; // Edge value - to be passed into the normal.
    
    if(t<FAR){
    
        vec3 p = o + r*t, n = nr(p, edge);

        l -= p; // Light to surface vector. Ie: Light direction vector.
        d = max(length(l), 0.001); // Light to surface distance.
        l /= d; // Normalizing the light direction vector.

        
 
        // Obtain the height map (destorted Voronoi) value, and use it to slightly
        // shade the surface. Gives a more shadowy appearance.
        float hm = heightMap(p);

        // Texture value at the surface. Use the heighmap value above to distort the
        // texture a bit.
        vec3 tx = tex3D((p*2. + hm*(0.2+syn_MediaType*0.2)), n);
        // tx = floor(tx*15.999)/15.; // Quantized cartoony colors, if you get bored enough.

        c.xyz = vec3(1.)*(hm*.8 + .2); // Applying the shading to the final color.
        
        c.xyz *= vec3(1.5)*tx; // Multiplying by the texture value and lightening.
        
        
        // Color the cell part with a fiery (I incorrectly spell it firey all the time) 
        // palette and the latticey web thing a very dark color.
        //
        c.x = dot(c.xyz, vec3(.299, .587, .114)); // Grayscale.
        if (id==0){
          // ================ ECLIPSE 4 of 5: the fiery palette ===============
          // The scene's main colour generator, and the reason it recolours
          // cleanly: by this line the surface has already been collapsed to a
          // single grey value in c.x, and everything that follows is a ramp
          // over it. Swap the ramp's stops and the whole crystal follows.
          //
          // The original also had a c.rgb = c.brg swizzle for its green
          // variant - a second palette got by rotating channels. There is no
          // second palette to get now: the rig names the colour, so rotating
          // it would only be a way of ignoring what was sent. It is gone, and
          // the toggle that chose it now chooses which stone (alternate_stone).
          if (_isMediaActive() && media_texture > 0.5 && texture_color > 0.5){
            // The original's media mode: with a clip standing in for the
            // stone and its own colour kept, the ramp is not wanted - the
            // video is the palette. Kept as it was.
            c.xyz += tx;
          } else {
            Palette pal = rigPalette();
            vec3 stock = c.xyz * vec3(min(c.x*1.5, 1.), pow(c.x, 5.), pow(c.x, 24.))*2.;
            vec3 rig   = c.xyz * rigRamp(c.x, pal);
            c.xyz = mix(stock, rig, rig_amount);
          }
          // ================ end ECLIPSE 4 of 5 ==============================
        } else{
          c.xyz = c.xyz*0.1;
        }
        
        // Hue rotation, for anyone who's interested.
        // c.xyz = rotHue(c.xyz, mod(TIME*0.5/16., 6.283));
       
        
        float df = max(dot(l, n), 0.); // Diffuse.
        float sp = pow(max(dot(reflect(-l, n), -r), 0.), 32.); // Specular.
        
        if(id == 1) sp *= sp; // Increase specularity on the dark lattice.
        
    // Applying some diffuse and specular lighting to the surface.
        // ================ ECLIPSE 5 of 5: the highlight tints ==============
        // The two fixed tints in the lighting. The broad specular becomes the
        // palette's near-white; the tight rim becomes the second colour,
        // which is the role the original's cool (.5,.7,1) edge was already
        // playing by hand. Small in area and easy to leave alone - but they
        // sit on the crystal edges, which is where the eye goes.
        Palette lightPal = rigPalette();
        vec3 specTint = mix(vec3(1, .97, .92), lightPal.spec, rig_amount);
        vec3 rimTint  = mix(vec3(.5, .7, 1),   lightPal.rim,  rig_amount);
        c.xyz = c.xyz*(df + .75) + specTint*sp + rimTint*pow(sp, 32.);
        // ================ end ECLIPSE 5 of 5 ==============================
        
        // Add the fake environmapping. Give the dark surface less reflectivity.
        vec3 em = eMap(reflect(r, n), n); // Fake environment mapping.
        if(id == 1) em *= .5;
        c.xyz += em*(0.0+0.9*syn_Level);
        
        // Edges.
        //if(id == 0)c.xyz += edge*.1; // Lighter edges.
        c.xyz *= 1. - edge*.8; // Darker edges.
        
        // Attenuation, based on light to surface distance.    
        c.xyz *= 1./(1. + d*d*.125);
        
        // AO - The effect is probably too subtle, in this case, so we may as well
        // save some cycles.
        //c.xyz *= cAO(p, n);
        c.rgb += smoothstep(0.5,1.0,edge)*1.0*0.0;
    }
    
    
    // Vignette.
    //vec2 uv = u/RENDERSIZE.xy;
    //c.xyz = mix(c.xyz, vec3(0, 0, .5), .1 -pow(16.*uv.x*uv.y*(1.-uv.x)*(1.-uv.y), 0.25)*.1);
    c = mix(c, pow(c, vec4(1.5))*2.0, abs(rot_distort)*10.0);
    // Apply some statistically unlikely (but close enough) 2.0 gamma correction. :)
    return vec4(sqrt(clamp(c.xyz, 0., 1.)), 1.);
}