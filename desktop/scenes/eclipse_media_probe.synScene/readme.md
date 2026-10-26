# Eclipse Media Probe

Shows what Synesthesia actually hands a scene when you load media, and through
which call. Diagnostic only — it keys nothing and has no controls.

Load it with your media loaded, and read it:

```
+---------------------------+---------------------------+
|                           |                           |
|      _loadMedia()         |     _loadUserImage()      |
|   (video / webcam path)   |    (still image path)     |
|                           |                           |
+---------------------------+---------------------------+
| [active] [userimage]   [ syn_MediaType, six segments ] |
+--------------------------------------------------------+
```

| What you see | What it means |
|---|---|
| Picture on the **left** | `_loadMedia()` reaches it. That is the call a scene should use. |
| Picture on the **right** | `_loadUserImage()` reaches it — it is in the still-image slot. |
| Both halves black | Neither call reaches it. The app has not given this scene the media at all; nothing in the shader can fix that. |
| **Green** block lit | `_isMediaActive()` is true. |
| **Blue** block lit | `_exists(syn_UserImage)` is true. |
| Amber segments | `syn_MediaType`, one segment per whole number. All dark is 0. |

## Why it exists

`eclipse_chroma_key` drew its fallback with media plainly loaded, and the three
ways of asking "is there media" do not agree with each other — nor does the
documentation say which covers a still, a video, a webcam or an NDI feed:

- `_isMediaActive()` — used by `vibe_thresholds`
- `syn_MediaType >= 0.5` — used by `PixelPopArt`, which has **no** `MEDIA` key
  in its `scene.json` at all, so that key is not what enables media
- `_exists(syn_UserImage)` — used by `media_key_&_cloner_fx` for the still
  assigned to the image slot

Both halves are sampled unconditionally, on every pixel, whatever the flags say.
The flags are the thing under test: a picture that only drew when a flag gave
permission would prove nothing.

## Then what

Whichever half shows the picture is the call `eclipse_chroma_key` should be
using, and which flags are lit says how it should decide. Report what you see
and the fix is one function — `sourcePixel()`, at the top of that scene's
`main.glsl`.

If **both halves are black** while the app is clearly playing something, the
problem is above the shader: media is not reaching scenes at all, and the next
thing to try is a scene neither of us wrote — `Media Key & Cloner FX`, which
ships with the app and says outright that it requires user media. If that one is
also empty, it is the app or the media file, not us.

## Compile-checked

Like every other scene here, against a real GLSL ES 3.00 compiler with the
Synesthesia helpers stubbed, before being handed over — headless Chrome does it
without needing the app:

```sh
chrome --headless=new --enable-unsafe-swiftshader --dump-dom \
       http://127.0.0.1:8731/check_probe.html
```
