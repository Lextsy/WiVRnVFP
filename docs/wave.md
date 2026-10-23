# WaveVR backend (HTC Vive Focus Plus)

`client/wave/` implements the OpenXR runtime API on top of HTC's WaveVR SDK, so the
WiVRn client runs on Wave 1 headsets (Vive Focus Plus, `Vive_Focus_Plus`, Android 7.1.2 /
API 25, Adreno 540). It is selected with `-DWIVRN_WAVE_RUNTIME=ON`.

# Build

The WaveVR SDK is proprietary and is consumed from a local checkout, not downloaded:

```properties
# gradle.properties
wave_sdk_dir=/path/to/wave-sdk
org.gradle.java.home=/path/to/jdk-17
```

Building OpenSSL from source in the Gradle/CMake graph needs a POSIX shell; on Windows,
point the build at an existing Android OpenSSL tree instead (its `./Configure android-arm64`
output laid out as `include/` + `lib/libcrypto.so`):

```properties
openssl_prebuilt_dir=/path/to/openssl-prebuilt
```

Without it the Gradle build configures and builds OpenSSL itself, which requires WSL or
Cygwin.

`wave_sdk_dir` must contain the SDK layout (`include/wvr/`, `jni/<abi>/*.so`, and
`wvr_client.aar`). The Gradle build copies the SDK native libraries into `jniLibs` and
compiles the shim against `include/wvr`.

Gradle must run on **Java 17**; newer JDKs fail the AGP configuration.

`generatePngs` renders the app icon with **`rsvg-convert`** (librsvg), which must be on the
PATH; on Windows install librsvg, or add `-x generatePngs` once the icons exist under
`build/generated_resources`.

# Device provisioning (required, once per headset)

Wave only puts an app into VR mode if its listener is in the `enabled_vr_listeners`
secure setting. Without it, `WVR_Init` reports `VrModeEnable failed` and the in-process
SVR compositor never starts, so no texture is ever composited. From adb:

```bash
adb shell settings put secure enabled_vr_listeners \
  "com.htc.vr.oem/com.htc.vr.core.server.WVRNotificationOverlayService:com.htc.vr.core.server/com.htc.vr.core.server.VRListenerService"
```

Verify:

```bash
adb shell settings get secure enabled_vr_listeners
```

The value must keep the `com.htc.vr.core.server/com.htc.vr.core.server.VRListenerService`
entry that `AndroidManifest.xml` declares through `android:enableVrMode`
(`@string/wvr_vr_mode_component`); the `com.htc.vr.oem/...WVRNotificationOverlayService`
entry is the launcher's own and must be preserved. Writing this setting requires
`WRITE_SECURE_SETTINGS`, which adb has.

A healthy start shows, from the app's own process:

```
svrInitialize
svrApi Version : 3.0.1
svrBeginVr
[CheckPoint_E] WarpThreadMain Entered
[CheckPoint_G] WVR_SubmitFrame firstTime.
```

# Connecting to a server

The server listens on **TCP and UDP 9757**. The headset pairs once, then reconnects
automatically:

```bash
# First pairing: the PIN goes in the URI userinfo...
adb shell am start -a android.intent.action.VIEW -c com.htc.intent.category.VRAPP \
  -d 'wivrn+tcp://123456@192.168.1.10' \
  -n org.meumeu.wivrn.local/org.meumeu.wivrn.MainActivity

# ... or in a `pin=` query parameter (accepted by `application::set_server_uri`)
adb shell am start -a android.intent.action.VIEW -c com.htc.intent.category.VRAPP \
  -d 'wivrn+tcp://192.168.1.10?pin=123456' \
  -n org.meumeu.wivrn.local/org.meumeu.wivrn.MainActivity

# Every later session: plain URI, no PIN
adb shell am start -a android.intent.action.VIEW -c com.htc.intent.category.VRAPP \
  -d 'wivrn+tcp://192.168.1.10' \
  -n org.meumeu.wivrn.local/org.meumeu.wivrn.MainActivity
```

A cold start **without** a URI opens the lobby and never dials: the URI is the only
trigger the launch path has. A URI connect is **not** written to the client config
(`application::set_server_uri` keeps it in memory), so only the lobby's "Add server" GUI
persists a server. Manual entries are always listed `reachable` and connect without
mDNS; autoconnect needs mDNS discovery.

The PIN is consumed by the lobby's `pin_enter` scene, which exits immediately when the URI
supplies it. Pairing keys are stored on the **server** in `~/.config/wivrn/known_keys.json`;
deleting an entry there forces a re-pair. A session is established when the app logs
`Session state changed ... XR_SESSION_STATE_FOCUSED` and `Created MediaCodec decoder for
video/hevc` for each stream.

The Wave launcher hands the display over asynchronously. Launching the VIEW intent before
VR mode and a surface are ready leaves the app in a paused state that never renders; gate
the intent on the session reaching `FOCUSED`.

# API 25 decoder constraints

The API-25 MediaCodec/Vulkan stack has no API-28 features, and each gap needs a
workaround in `client/decoder/android/`:

- **Codec probing**: `AMediaCodec_getDecoderNameForType`/`getCodecInfoForType` are
  API 29+. Enumerate `MediaCodecList.getCodecInfos()`, skip `isEncoder`, match
  `getSupportedTypes()`, and reject software codecs by name prefix (`OMX.google`,
  `c2.android`) — the only API-21 way to get a hardware decoder.
- **No AHB → Vulkan import**: `vkGetAndroidHardwareBufferPropertiesANDROID` fails on the
  Adreno 540 driver. Decoded `AImage`s go through the CPU path: `AImage_getPlaneData`
  planes, BT.601 YUV→RGBA, written into a linear `B8G8R8A8_UNORM` image.
- **No async MediaCodec callbacks** (API 28+): a pump thread dequeues input buffers and
  releases output buffers synchronously.
- **The decoder must hand out a real Vulkan sampler.** `stream_defoveator::ensure_pipeline`
  passes `decoder::sampler()` as an *immutable sampler* in the descriptor set layout, and
  the Adreno driver dereferences immutable samplers while assigning pipeline-layout
  bindings. A null sampler (the Ycbcr sampler is only created by the AHB path) crashes
  inside `vkCreatePipelineLayout` with a SIGSEGV. The CPU path therefore creates a plain
  linear/clamp-to-edge sampler.

# The server: no fork changes

`server/` is untouched upstream code and stays that way: the whole VFP contract is
satisfied by advertising the right values from the client.

**Protocol version is the one hard constraint.** `common/protocol_version.h` is a hash of
the packet definitions (`protocol_revision` in `common/wivrn_packets.h`); a server built
from a source tree with a different revision rejects the headset in the crypto handshake
with `Incompatible protocol version` (`server/driver/wivrn_connection.cpp`). Build the
server from this fork's upstream base commit, not from a newer upstream master.

Everything else is stock negotiation:

- Encoder pacing follows `from_headset::refresh_rate_changed`
  (`server/driver/wivrn_session.cpp`); the shim's measured-rate events steer it.
- Stream size comes from `resolution_scale` / `stream_scale`, framerate from
  `fps_divider` - existing client settings the stock server already honours.
- The codec is picked from the client's advertised `supported_codecs`; the Focus Plus
  lists h265/h264 (no AV1 decoder) and the server encodes HEVC.
- No HMD database entry is needed: the server never inspects the device model.

The stock server rejects reconnecting headsets whose `headset_info` differs in the
fields `validate_headset_info` compares (`supported_codecs`, `bit_depth`, `system_name`,
`available_refresh_rates`, palm pose / user presence / passthrough / body tracking). That
is why the shim's advertised refresh-rate list and its codec probe must be deterministic
across runs.

Host-side requirements this device depends on:

- Start the server with `DISPLAY` unset on headless NVIDIA hosts: `vkCreateInstance`
  blocks forever against a headless Xorg, the compositor is never created, and the
  headset stays in the lobby.
- TCP **and** UDP 9757 reachable. Pairing keys live in `~/.config/wivrn/known_keys.json`;
  deleting an entry forces a re-pair.
- mDNS may be dead on the LAN (UDP 5353 firewalled, an AP that does not flood multicast
  between Ethernet and Wi-Fi) while direct TCP works; test with `avahi-browse _wivrn._tcp`
  from a wireless device.
- The server exits when no client is connected and can linger with no listener after a
  session: check `ss -tlnp | grep 9757` before restarting, or the new instance fails with
  `Address already in use`. A surviving Monado child ignores SIGTERM and blocks
  `wivrnctl pair` - `kill -9` it and restart the attached app, whose IPC connection dies
  with Monado.

# The server needs an OpenXR application attached

WiVRn's server composites frames for an OpenXR application; the encoder only encodes
composited frames. With no application attached, Monado's main loop runs, the compositor
renders nothing, and the wire to the headset carries only control traffic (tens of
kilobytes per minute, not the megabytes a video stream needs).

`xrapp` is a minimal raw-Vulkan OpenXR app that renders red (left) / green (right) eye
views. Build it on the server host:

```bash
g++ -std=c++20 -O1 -DXR_USE_GRAPHICS_API_VULKAN -DXR_EXTENSION_PROTOTYPES \
    xrapp.cpp -o xrapp -lopenxr_loader -lvulkan
```

Run it with the runtime json exported — the loader's `active_runtime.json` lookup does not
find the WiVRn driver:

```bash
XR_RUNTIME_JSON=~/wivrn-src/build-server/openxr_wivrn-dev.json ./xrapp
```

Start the server **first**, then `xrapp`, then connect the headset: `xrGetSystem` blocks
until a headset system exists. The app must enable `XR_FB_display_refresh_rate` and call
`xrRequestDisplayRefreshRateFB(session, 75.0f)` — the server drives the display from the
**application's** requested rate.

# Frame-rate control

The server paces and encodes at the refresh rate the headset advertises
(`from_headset::refresh_rate_changed` → `compositor::set_framerate`). Wave composites only
as fast as the app submits frames, and on the Focus Plus the decode+render loop sustains
well below the panel's 75 Hz. Encoding at 75 Hz makes decoded frames age past the stream
scene's 1 s stall guard (`stream.cpp`, `blitted - received_from_decoder > 1 s`), which pops
the stream scene and closes the connection.

The shim therefore measures its achieved `xrEndFrame` rate (EMA-filtered in `end_frame`)
and announces it through `XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB`; the client
forwards it to the server, so the server encodes at the rate the headset sustains.
`xrEnumerateDisplayRefreshRatesFB` must report **only the stable panel rate**
(`WVR_GetRenderProps().refreshRate`): the client puts that list into its headset-info
packet and `validate_headset_info` refuses a reconnecting headset whose advertised list
differs from the live session's, so returning the measured rate breaks every second
connection. `xrGetDisplayRefreshRateFB` returns the last announced (measured) rate, and
`xrRequestDisplayRefreshRateFB` is accepted without changing the shim's pacing (Wave owns
the panel). Frame pacing stays on the panel rate; only the server's encoder follows the
announced rate.

The other levers on the sustained rate live in the client's settings page, which is a quad
layer and therefore invisible on Wave, so edit the file directly:

```sh
adb shell run-as org.meumeu.wivrn.local cat files/client.json
```

The app reads `<files>/client.json` at startup and keeps defaults for keys it does not
find, so a one-key file is valid.

- `resolution_scale` (default 1.2) multiplies the recommended eye size, and the server
  sizes the three streams from it. `1.0` renders 1440x1600 per eye instead of 1728x1920:
  31 % less decode, copy and fill work per frame.
- `stream_scale` (default 1.0) scales the encoded streams on top of that.
- `fps_divider` (default 1, `2` is what the GUI offers) divides the framerate the server
  paces and encodes at, independently of what the headset advertises.

The app rewrites `client.json` when it exits, so stop it before editing the file:

```sh
adb shell am force-stop org.meumeu.wivrn.local
adb shell run-as org.meumeu.wivrn.local sh -c \
  'printf %s "{\"resolution_scale\": 1.0, \"fps_divider\": 2}" > files/client.json'
```

# Diagnostics

- Monado's debug logging is off by default: start the server with `XRT_LOG=D` for
  compositor detail (`u_logging.c` reads it as `XRT_LOG`).

- The Wave build mirrors its log into the app's external files dir, because HTC's mlogger
  suppresses app-side logcat output:
  `adb shell "cat /storage/emulated/0/Android/data/org.meumeu.wivrn.local/files/wivrn.log"`.
  The throttled markers that prove video is arriving: `Stream blit #`, `scene gpu time`,
  `render_overlay: mask`, `lobby head`; a session that stalls logs `Stream stalled`.
- WiVRn's server has **no per-frame encoder logging** at any level. Whether video is
  flowing is measured on the wire, not in the log: capture with
  `tcpdump -i <if> -s 0 -w out.pcap 'port 9757'` and sum the packet lengths per direction.
- Measured baselines on a Vive Focus Plus against a GTX 1050 Ti server, static OpenXR
  scene, 3 streams at 896x960, 60 s capture window: server to headset 60-100 KB,
  headset to server 250 KB (pose and IMU), client `xrEndFrame` rate 19 Hz, Wave
  compositor rate 39 Hz, session lifetime 2.8 s when the server encodes at 75 Hz. A
  session that survives keeps the first number proportional to the announced rate for
  the whole window; dying at ~2.8 s means the announced rate is still the panel rate.

# Runtime behaviour and known limits

- **Layer types.** Wave composites eye textures only. WiVRn renders its GUI (and 3D model
  previews) into OpenXR **quad layers** (`XR_TYPE_COMPOSITION_LAYER_QUAD`, type 36); Wave
  has no path for them, so `xrEndFrame` drops every non-projection layer and logs
  `dropping composition layer of type 36` once. The GUI is not visible on this device.
- **Swapchains Wave never composites.** A Wave texture queue only rotates for the queues
  Wave composites, so a quad-layer swapchain's queue stops yielding textures after the
  first one. `xrAcquireSwapchainImage` detects this (`submitted_to_wave` is set only for
  swapchains that `xrEndFrame` submits) and renders the swapchain locally, handing out
  images round-robin, instead of stalling the frame loop on a queue that never rotates.
  Presentation resumes automatically if Wave starts yielding again.
- **Frame pacing** comes from polling `WVR_GetAvailableTextureIndex` in
  `xrAcquireSwapchainImage`; it is the only pacing source the shim has.
- **Client/server library versions.** The bundled `wvr_client.aar` is Wave **3.1.5**
  (`com.htc.vr.BuildConfig.VERSION_CODE=3001005`) while the headset's VR service may be
  **3.2.0** (`versionCode 3002000`). The runtime logs
  `version is not compatible. client:3.1.5 server: 3.2.0`; registration still succeeds
  and the session runs.
- **`WVR_ReleaseTextureQueue` must not be called** on Wave 3.1.5: its trampoline resolves
  to a null runtime entry point and the process faults at `pc=0`. `destroy_swapchain`
  deliberately skips it; Wave reclaims the queues at process exit.
- `WVR_ObtainTextureQueue`'s last argument is **level**, not length.
- `WVR_GetAvailableTextureIndex` is non-blocking (`-1` = nothing available) and does not
  check the texture out; every freshly created queue reports an available index.

# Noise that is not an error

These appear at startup or teardown and do not affect the session:

- `ClassNotFoundException: com.htc.vr.core.plugin.VRClientPlugin`
- `QVR_API_NOT_SUPPORTED` (eye tracking), `Loading config file from vendors has failed`,
  `Undefined variable -- gWaveNumOfVsyncToPredict_APP/_ATW`
- `Using default fonts (AFontMatcher requires API 29)` and `unknown family 17` — the
  headset is API 25, WiVRn's font matcher needs API 29
- At teardown: `Frame not displayed since not in VR mode`,
  `L_GetNextFrameToWarp overlaySync = 0xb is invalid`, `SetVrModeEnabled success :false`
  (it means it was enabled), and `GL_INVALID_OPERATION` spam from the Wave launcher
  (`com.htc.mobilevr.launcher`).
