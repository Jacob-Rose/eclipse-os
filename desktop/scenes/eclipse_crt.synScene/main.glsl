// ============================================================================
// CREDIT / LICENSE
// ----------------------------------------------------------------------------
// The CRT filter in this file is:
//
//     [RVM] RETRO VIDEO MONITOR - v20210519
//     by Timothy Lottes
//
// released by its author into the PUBLIC DOMAIN under the Unlicense
// (https://unlicense.org/). The filter maths - the portability macros, the
// gather-4 kernel, RvmF and its scanline/grille/slot-mask paths - are his work,
// carried here unchanged. Anyone is free to copy, modify and use it for any
// purpose; no warranty is given. The Unlicense dedication travels with the code.
//
// What eclipse-os added is the wiring, not the filter: the two-pass structure,
// the BuffA samplers, the Synesthesia entry point and the controls (see below).
// ============================================================================

// A CRT styliser for Synesthesia, built around Timothy Lottes' Retro Video
// Monitor (RVM, v20210519, public domain / Unlicense). The filter is his,
// unchanged in its maths; what this file does is wire it into Synesthesia's
// world instead of ShaderToy's.
//
// What changed from the ShaderToy reference, and why
// --------------------------------------------------
//   - Two passes instead of one. RVM is an *up-sampler*: it wants a genuinely
//     low-res input and paints scanlines and a slot mask as it blows it up. So
//     PASS 0 draws the source into BuffA at 640x360 (the "tube"), and the final
//     pass runs RVM over BuffA up to the 1920x1080 screen. The ShaderToy demo
//     faked the low-res input with an INPUT_DOT divisor because it only had one
//     full-res texture to sample; a real second buffer is cleaner and that hack
//     is gone.
//   - The three gather-4 samplers read BuffA, not iChannel0, and derive the
//     texel size from BuffA itself (textureSize) rather than iChannelResolution.
//     They still linearise on read (FromSrgb1): RVM works in linear light.
//   - The entry point is renderMain()/PASSINDEX, not mainImage(), and reads
//     _xy / RENDERSIZE / TIME instead of fragCoord / iResolution.
//   - HLSL and the packed-16-bit path are dropped. This is GLSL, 32-bit.
//
// Configurable, or committed to the last level
// --------------------------------------------
// RVM's *mask style* - PVM scanlines (mode 0), Wega grille (1), arcade slot
// mask (2) - is a compile-time #if, because each is a different pixel layout and
// there is no cheap way to branch a whole raster geometry per fragment. This
// scene commits to the last one, arcade, and sets RVM_MODE 2 below; change that
// one number to retune the whole look. Everything that RVM *does* take as a
// runtime argument - warp amount, spot size (blur), and whether the filter runs
// at all - is a control, so those move live without a recompile.

//==============================================================
// SETUP  -  the compile-time choices (see the note above)
//==============================================================
#define RVM_MODE 2      // {0:PVM 240p, 1:Wega 480p, 2:arcade slot mask}
#define RVM_GLSL 1
#define RVM_32BIT 1
#define RVM_WARP 1      // curvature available; its *strength* is the warp control
#define RVM_2PI 6.28318530718
// Output back to sRGB for the display. RVM's internal maths is linear light; the
// screen is not. Set to 0 if the picture comes up dark/oversaturated because
// this build of Synesthesia already gamma-corrects the shader's output.
#define RVM_OUTPUT_SRGB 1


//==============================================================
// sRGB  <->  linear
//==============================================================
float FromSrgb1(float c){
	return (c <= 0.04045) ? c * (1.0 / 12.92)
	                      : pow(c * (1.0 / 1.055) + (0.055 / 1.055), 2.4);
}
vec3 FromSrgb(vec3 c){ return vec3(FromSrgb1(c.r), FromSrgb1(c.g), FromSrgb1(c.b)); }

float ToSrgb1(float c){
	return (c < 0.0031308) ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055;
}
vec3 ToSrgb(vec3 c){ return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b)); }


//==============================================================
// PORTABILITY  -  RVM's own type/aliasing macros (GLSL subset)
//==============================================================
#define RvmF1 float
#define RvmF2 vec2
#define RvmF2_(x) vec2((x),(x))
#define RvmF3 vec3
#define RvmF3_(x) vec3((x),(x),(x))
#define RvmF4 vec4
#define RvmFractF1 fract
#define RvmNCosF2(x) cos((x)*RvmF2_(RVM_2PI))
#define RvmRcpF1(x) (1.0/(x))
#define RvmRcpF2(x) (RvmF2_(1.0)/(x))
#define RvmRcpF3(x) (RvmF3_(1.0)/(x))
#define RvmSatF1(x) clamp((x),0.0,1.0)
#define RvmSatF2(x) clamp((x),0.0,1.0)

RvmF1 RvmMax3F1(RvmF1 a, RvmF1 b, RvmF1 c){ return max(a, max(b, c)); }
RvmF2 RvmMax3F2(RvmF2 a, RvmF2 b, RvmF2 c){ return max(a, max(b, c)); }


//==============================================================
// GATHER-4 over BuffA
//--------------------------------------------------------------
// The one place RVM touches the outside world. `uv` is a normalised coordinate
// in the *input* (BuffA) space; each of these returns the four texels of a 2x2
// footprint around it, one colour channel at a time, in RVM's gather ordering:
//
//   W Z
//   X Y
//
// Linearised on the way out because the filter downstream assumes linear light.
// Deriving the texel size from BuffA means the scene stays correct if you resize
// the pass in scene.json.
//==============================================================
vec4 RvmR4F(vec2 uv){
	vec2 hp = 0.5 / vec2(textureSize(BuffA, 0));
	vec4 o;
	o.w = FromSrgb1(texture(BuffA, uv + vec2(-hp.x, -hp.y)).r);
	o.z = FromSrgb1(texture(BuffA, uv + vec2( hp.x, -hp.y)).r);
	o.x = FromSrgb1(texture(BuffA, uv + vec2(-hp.x,  hp.y)).r);
	o.y = FromSrgb1(texture(BuffA, uv + vec2( hp.x,  hp.y)).r);
	return o;
}
vec4 RvmG4F(vec2 uv){
	vec2 hp = 0.5 / vec2(textureSize(BuffA, 0));
	vec4 o;
	o.w = FromSrgb1(texture(BuffA, uv + vec2(-hp.x, -hp.y)).g);
	o.z = FromSrgb1(texture(BuffA, uv + vec2( hp.x, -hp.y)).g);
	o.x = FromSrgb1(texture(BuffA, uv + vec2(-hp.x,  hp.y)).g);
	o.y = FromSrgb1(texture(BuffA, uv + vec2( hp.x,  hp.y)).g);
	return o;
}
vec4 RvmB4F(vec2 uv){
	vec2 hp = 0.5 / vec2(textureSize(BuffA, 0));
	vec4 o;
	o.w = FromSrgb1(texture(BuffA, uv + vec2(-hp.x, -hp.y)).b);
	o.z = FromSrgb1(texture(BuffA, uv + vec2( hp.x, -hp.y)).b);
	o.x = FromSrgb1(texture(BuffA, uv + vec2(-hp.x,  hp.y)).b);
	o.y = FromSrgb1(texture(BuffA, uv + vec2( hp.x,  hp.y)).b);
	return o;
}


//==============================================================
// FILTER ENTRY  -  RVM's RvmF, verbatim in its maths
//--------------------------------------------------------------
// Input must be linear {0..1}; output is linear. Every argument is supplied by
// renderCRT() below from BuffA's size and the controls. This is Lottes' code;
// the comments are his. See the header note for what the arguments mean.
//==============================================================
// Paired gaussian approximation
RvmF2 RvmPolyF2(RvmF2 x){
	x = RvmSatF2(-x * x + RvmF2(1.0, 1.0)); return x * x;
}

//--------------------------------------------------------------
#define RVM_DARK (7.0/8.0)
#define RVM_SCAN_DIV 3.0
#define RVM_SCAN_MAX (8.0/15.0)
#define RVM_SCAN_MIN (RVM_SCAN_DIV*RVM_SCAN_MAX)
#define RVM_SCAN_SIZ (RVM_SCAN_MAX-RVM_SCAN_MIN)

RvmF3 RvmF(
	RvmF2 ipos,                    // output pixel position
	RvmF2 inputSizeDivOutputSize,  // inputSize / outputSize
	RvmF2 halfInputSize,           // 0.5 * inputSize
	RvmF2 rcpInputSize,            // 1.0 / inputSize
	RvmF2 twoDivOutputSize,        // 2.0 / outputSize
	RvmF1 inputHeight,             // inputSize.y
	RvmF2 warp,                    // scanline warp {0=flat}
	RvmF1 blur,                    // horizontal blur {0.50 blurry .. 1.00 blocky}
	RvmF4 blur4)                   // {0.5,-0.5,-1.5,-2.5} * blur
{
	// Optional apply warp
	RvmF2 pos;
#ifdef RVM_WARP
	// Convert to {-1 to 1} range
	pos = ipos * twoDivOutputSize - RvmF2_(1.0);
	// Distort pushes image outside {-1 to 1} range
	pos *= RvmF2_(1.0) + pos.yx * pos.yx * warp;
	// Vignette to kill off-image content
	RvmF2 vin2 = RvmSatF2(pos * pos);
	// 1-((1-x)*(1-y)) -> (1-x)*y+x
	RvmF1 vin = (RvmF1(1.0) - vin2.x) * vin2.y + vin2.x;
	vin = RvmSatF1((-vin) * inputHeight + inputHeight);
	// Leave in {0 to inputSize}
	pos = pos * halfInputSize + halfInputSize;
#else
	pos = ipos * inputSizeDivOutputSize;
#endif

	// Get to center for first gather 4
#if RVM_MODE==0
	RvmF2 g = floor(pos + RvmF2(-1.5, -0.5)) + RvmF2_(1.0);
	RvmF2 gp = g * rcpInputSize;
	g.y -= RvmF1(0.5);
#endif
#if RVM_MODE!=0
	RvmF2 g = floor(pos + RvmF2(-0.5, -1.5)) + RvmF2_(1.0);
	RvmF2 gp = g * rcpInputSize;
	g.x -= RvmF1(0.5);
#endif

	// 4x2 / 2x4 sampled footprint via two gather-4 taps
#if RVM_MODE==0
	RvmF4 colRS = RvmR4F(gp); RvmF4 colGS = RvmG4F(gp); RvmF4 colBS = RvmB4F(gp);
	gp.x += RvmF1(2.0 * rcpInputSize.x);
	RvmF4 colRT = RvmR4F(gp); RvmF4 colGT = RvmG4F(gp); RvmF4 colBT = RvmB4F(gp);
#endif
#if RVM_MODE!=0
	RvmF4 colRS = RvmR4F(gp); RvmF4 colGS = RvmG4F(gp); RvmF4 colBS = RvmB4F(gp);
	gp.y += RvmF1(2.0 * rcpInputSize.y);
	RvmF4 colRT = RvmR4F(gp); RvmF4 colGT = RvmG4F(gp); RvmF4 colBT = RvmB4F(gp);
#endif

	// Horizontal (mode 0) / vertical (modes 1,2) gaussian kernel
#if RVM_MODE==0
	RvmF1 offB = RvmF1(pos.x - g.x);
#endif
#if RVM_MODE!=0
	RvmF1 offB = RvmF1(pos.y - g.y);
#endif
	RvmF2 offS = RvmF2(offB, offB) * RvmF2_(blur) + blur4.xy;
	RvmF2 offT = RvmF2(offB, offB) * RvmF2_(blur) + blur4.zw;
	RvmF2 horS = RvmPolyF2(offS);
	RvmF2 horT = RvmPolyF2(offT);

	// Kernel totals and reciprocal
	RvmF2 hor0 = horS + horT;
	RvmF1 horD = RvmRcpF1(hor0.x + hor0.y);
#ifdef RVM_WARP
	horD *= RvmF1(vin);
#endif

	// Apply the kernel, up and down sums
#if RVM_MODE==0
	RvmF2 colRU2 = colRS.wz * horS + colRT.wz * horT;
	RvmF2 colGU2 = colGS.wz * horS + colGT.wz * horT;
	RvmF2 colBU2 = colBS.wz * horS + colBT.wz * horT;
	RvmF2 colRD2 = colRS.xy * horS + colRT.xy * horT;
	RvmF2 colGD2 = colGS.xy * horS + colGT.xy * horT;
	RvmF2 colBD2 = colBS.xy * horS + colBT.xy * horT;
	RvmF2 colRL = RvmF2(colRU2.x + colRU2.y, colRD2.x + colRD2.y);
	RvmF2 colGL = RvmF2(colGU2.x + colGU2.y, colGD2.x + colGD2.y);
	RvmF2 colBL = RvmF2(colBU2.x + colBU2.y, colBD2.x + colBD2.y);
#endif
#if RVM_MODE!=0
	RvmF2 colRL = colRS.wz * horS.xx + colRS.xy * horS.yy +
	              colRT.wz * horT.xx + colRT.xy * horT.yy;
	RvmF2 colGL = colGS.wz * horS.xx + colGS.xy * horS.yy +
	              colGT.wz * horT.xx + colGT.xy * horT.yy;
	RvmF2 colBL = colBS.wz * horS.xx + colBS.xy * horS.yy +
	              colBT.wz * horT.xx + colBT.xy * horT.yy;
#endif

	// Normalize by kernel total
	colRL *= RvmF2_(horD);
	colGL *= RvmF2_(horD);
	colBL *= RvmF2_(horD);

#if RVM_MODE==0
	// Channel maximums -> variable scan width (thicker at peak, self-normalising)
	RvmF2 colML = RvmMax3F2(colRL, colGL, colBL);
	colML = sqrt(colML);
	RvmF2 scnL = colML * RvmF2_(RVM_SCAN_SIZ) + RvmF2_(RVM_SCAN_MIN);
	RvmF1 offY = RvmF1(pos.y - g.y);
	scnL.x = ( offY) * scnL.x;
	scnL.y = (-offY) * scnL.y + scnL.y;
	scnL = min(RvmF2_(0.5), scnL);
	scnL = RvmNCosF2(scnL);
	scnL = scnL * RvmF2_(0.5) + RvmF2_(0.5);
	RvmF2 nrmL = RvmF2_(1.0) - colML;
	nrmL = nrmL * RvmF2_(RVM_SCAN_MIN / RVM_SCAN_MAX - 1.0) + RvmF2_(1.0);
	scnL *= nrmL;
#endif
#if RVM_MODE!=0
	RvmF1 offX = RvmF1(pos.x - g.x);
	RvmF2 scnL = RvmF2(RvmF1(1.0) - offX, offX);
#endif

	// Apply scan and merge the two nearest lines
	colRL *= scnL; colGL *= scnL; colBL *= scnL;
	RvmF3 col;
	col.r = colRL.x + colRL.y;
	col.g = colGL.x + colGL.y;
	col.b = colBL.x + colBL.y;

	// Grille (mode 1)
#if RVM_MODE==1
	RvmF1 lim = RvmF1(1.0 / ((1.0 / 3.0) + (2.0 / 3.0) * RVM_DARK));
	RvmF3 colD = col * col; colD *= RVM_DARK;
	RvmF3 amp = RvmRcpF3(RvmF3_(lim * 1.0 / 3.0) + RvmF3_(lim * 2.0 / 3.0) * col);
	ipos.x = RvmFractF1(ipos.x * RvmF1(1.0 / 3.0));
	col *= amp; colD *= amp;
	if      (ipos.x < RvmF1(1.0 / 3.0)) { colD.r = col.r; }
	else if (ipos.x < RvmF1(2.0 / 3.0)) { colD.g = col.g; }
	else                                { colD.b = col.b; }
	return colD;
#endif

	// Slot mask (mode 2 - the arcade look this scene commits to)
#if RVM_MODE==2
	RvmF1 lim = RvmF1(1.0 / ((3.0 / 12.0) + (9.0 / 12.0) * RVM_DARK));
	if (RvmFractF1(ipos.x * RvmF1(1.0 / 6.0)) > RvmF1(0.5)) ipos.y += RvmF1(2.0);
	ipos.y = RvmFractF1(ipos.y * RvmF1(1.0 / 4.0));
	RvmF3 colD = col * col; colD *= RVM_DARK;
	RvmF3 amp = RvmRcpF3(RvmF3_(lim * 3.0 / 12.0) + RvmF3_(lim * 9.0 / 12.0) * col);
	ipos.x = RvmFractF1(ipos.x * RvmF1(1.0 / 3.0));
	col *= amp; colD *= amp;
	if (ipos.y > RvmF1(1.0 / 4.0)) {
		if      (ipos.x < RvmF1(1.0 / 3.0)) { colD.r = col.r; }
		else if (ipos.x < RvmF1(2.0 / 3.0)) { colD.g = col.g; }
		else                                { colD.b = col.b; }
	}
	return colD;
#endif

	return col;
}


//==============================================================
// PASS 0  ->  BuffA : the source, at the tube's native resolution
//--------------------------------------------------------------
// Whatever you want the CRT to chew on. First choice is loaded media - a still,
// a video, a feed - which is where your baked-in text will live. With nothing
// loaded it falls back to a tint-able test pattern so the filter has structure
// to show off and the scene is never simply black. The three media probes match
// eclipse_media_probe: the app does not agree with itself about which one means
// "there is media", so ask all of them.
//==============================================================
vec4 renderSource(void)
{
	vec3 src;
	if (_isMediaActive() || syn_MediaType >= 0.5)
	{
		src = _loadMedia().rgb;
	}
	else if (_exists(syn_UserImage))
	{
		src = _loadUserImage().rgb;
	}
	else
	{
		// No media: coarse vertical bars in the rig's colour. Bars rather than a
		// flat wash so scanlines and the slot mask have an edge to bite on, and
		// tinted by rig_color so the OSC link is still visibly doing something.
		float bars = step(0.5, fract(_uv.x * 8.0));
		src = mix(rig_color, rig_color * 0.25, bars);
	}
	return vec4(src, 1.0);
}


//==============================================================
// FINAL PASS  ->  screen : run RVM over BuffA
//==============================================================
vec4 renderCRT(void)
{
	// crt_enable off: the clean reference. Still a real up-sample of BuffA, so
	// toggling it is a fair before/after of what the filter adds.
	if (crt_enable < 0.5)
	{
		return vec4(texture(BuffA, _uv).rgb * brightness, 1.0);
	}

	vec2 isz = vec2(textureSize(BuffA, 0));

	// warp control 0..2 scales RVM's arcade default curvature. The x/y split is
	// Lottes' - horizontal and vertical warp differently because the tube does.
	vec2 warpVec = vec2(1.0 / 48.0, 1.0 / 24.0) * warp;

	vec3 col = RvmF(
		_xy,                    // ipos
		isz / RENDERSIZE,       // inputSizeDivOutputSize
		isz * 0.5,              // halfInputSize
		1.0 / isz,              // rcpInputSize
		2.0 / RENDERSIZE,       // twoDivOutputSize
		isz.y,                  // inputHeight
		warpVec,                // warp
		blur,                   // horizontal blur
		vec4(0.5, -0.5, -1.5, -2.5) * blur);

#if RVM_OUTPUT_SRGB
	col = ToSrgb(col);
#endif
	return vec4(col * brightness, 1.0);
}


vec4 renderMain(void)
{
	if (PASSINDEX == 0)
	{
		return renderSource();
	}
	return renderCRT();
}
