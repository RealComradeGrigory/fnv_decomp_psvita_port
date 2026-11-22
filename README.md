# FNV Vita 00.10 — CPU memory access and incremental outdoor loading

Install `fnv_vita_00.10.vpk` with VitaShell over the existing application. Title ID remains `FNVV00001`. Keep `ux0:data/fnv_vita/Data/` and its existing original game archives, including `Update.bsa`. No new game-data copy or re-export is needed when upgrading from 00.09. Graphics selections are preserved.

Your 00.09 readings outdoors were Update 0.1 ms, Scene 30.7 ms, Weapon/UI 24 ms, GPU wait 12 ms, and Present 0.2 ms, with Update reaching approximately 1,000 ms while crossing outdoor areas. These readings identify substantial CPU work and a loading hitch; they do not directly measure GPU execution time or utilization. **00.10 builds and passes host checks, but has not been run on the Vita. Its FPS improvement and remaining hitch duration are unmeasured.**

## What changed

**Weapon animation:** Original first-person animation now writes to ordinary CPU memory rather than writing every vertex to uncached GPU memory and reading it back. The existing two workers and main thread share animation blocks. When ADS is zero, interpolation skips unused aim vertex samples. The renderer copies the completed pose once into its immutable GPU frame buffer; visibility preparation reads the CPU pose. Original GPU mesh buffers stay unchanged. GPU commands remain on the main thread, and disabling CPU workers also disables parallel weapon posing.

**Scene preparation:** Static meshes keep a compact CPU cache of positions and packed indices. Visibility workers read this cache rather than repeatedly reading uncached GPU memory. Full vertex attributes remain in the existing GPU buffers and are read when boundary polygons need interpolation. Collision preparation shares these cached positions, avoiding another copy of them. The cache does not change the original models, placements, material order, LOD selection or view distances.

**Walking between exterior cells:** The previous loader destroyed and rebuilt all nine terrain tiles, loaded regional assets and rebuilt the full collision world synchronously. The replacement keeps the active region rendered and collidable while preparing a pending region across updates. It reuses overlapping terrain, admits at most one heavy asset/preparation operation per walking update, and processes collision in chunks capped at 8,192 triangle additions per update. It checks an approximately 4 ms soft budget between work units. Collision storage is reserved before insertion to avoid repeatedly copying a growing triangle array.

Movement toward a cell edge starts neighbor preparation within the final 1,024 game units. A ready neighbor is committed only after the player enters its cell. Changing direction, changing graphics settings or leaving the world can cancel preparation safely. Walking is constrained to the active terrain rectangle while a replacement is pending; noclip remains unrestricted. A bounded 16 MiB CPU source cache avoids repeatedly parsing the same coarse LOD meshes.

Initial exterior loads and door/world transitions still finish synchronously. A single model parse, texture decode/upload, allocation or vertex transformation cannot be preempted by the soft budget. Cancellation, final GPU synchronization, outgoing-resource release and the final region swap also occur on the main thread. **This is not a hard 4 ms limit or a guarantee that every hitch is gone.** Existing model budgets remain: 64 MiB/768 models for a prepared region, with an 88 MiB/1,024-model transient residency cap while generations coexist. Dense regions can still reach those limits.

The interior placement correction is preserved. Earlier releases and the original Antigravity project are untouched.

## Timings to compare on the Vita

Select **START → Show FPS → FPS + timings**. Stand in the same outdoor position with the same weapon, camera direction and graphics settings. The existing Update, Scene, Weapon/UI, GPU wait and Present wait readings remain. Three new readings split the combined Weapon/UI cost:

- **Pose CPU:** original animation sampling/interpolation and its worker barrier.
- **Weapon draw CPU:** viewmodel rendering and effects, excluding posing.
- **HUD/UI CPU:** HUD, dialogue/Pip-Boy and FPS/timing overlay.

The three new lines sum to Weapon/UI, apart from display smoothing/rounding. Their values are CPU wall times, including any worker waits, rather than a sum of all core execution times. GPU wait and Present include driver/synchronization work and are not GPU utilization readings.

Compare workers On and Off after loading settles, then walk across the same boundary that previously froze. Report the FPS, Scene, Pose, Weapon draw, HUD/UI and largest Update readings. The new five-second `VIEWMODEL` log line separates these costs. `WorldScene` commit lines record update count, reused terrain, new models, collision work and largest preparation slice. The first synchronous load is expected to show a large slice; look at later walking commits. Logs are at `ux0:data/fnv_vita/fnv_vita.log`; copy the current log before relaunching because launching replaces it.

## Verified on the PC

- VitaSDK build and package checks pass.
- All 28 tested scene views match the actual 00.09 source's vertex/index/order hashes. The cached and uncached paths, optimized serial and parallel submission, and captured shader/matrix/sampler/depth state also match.
- All five original weapon models and 25 clips load and render. Across 525 sampled animation states, the CPU serial and parallel paths produce byte-identical complete poses, muzzle anchors and Pip-Boy attachment matrices to the saved 00.09 interpolation. Original GPU buffers stay unchanged, and queued multipart snapshots remain immutable.
- The Goodsprings crossing prepares across 58 host update calls, reuses six terrain GPU buffers, loads one new model and preserves the exact original opaque collision triangles, transforms and order. No walking slice exceeds one heavy operation or 8,192 triangle additions. This is an operation-count check, not Vita timing evidence.
- Prefetch completion without early commit, cancellation after partial collision construction, replacement targets, graphics changes and cleanup during pending preparation pass. Active physics remains intact while replacement geometry is prepared.
- All 30,096 exported world tiles decode. Doc's original hallway remains traversable. Original exterior exit/return links, terrain, six outdoor locations, settings persistence/migration, fog restoration and resource cleanup pass.
- Existing checks pass for 411 NIF/270 DDS fixtures, physics, 1,000 UI transitions, 0/1/2-worker execution, 6,000 barriers, 100 renderer worker-mode switches, five legacy interiors, head attachments/dialogue and 60 room views. No frame-arena overflow or double-free is detected.

Tracked region CPU geometry/collision/LOD storage peaks at approximately **80.33 MiB** in the world checks, including simultaneous active and pending collision. Tracked GPU buffers peak at **111.1 MiB**, and retained preparation scratch at **8.31 MiB**. These are partial host accounting figures; they exclude transient asset decodes, some container/allocator overhead, framebuffers/GPU context, the world database and other gameplay systems. They are not a total Vita memory measurement.

The scene work counts remain unchanged from 00.09: the four Goodsprings Performance views submit 253,750 triangles and 914 draws before fog. The new optimization changes where CPU geometry is read and when streaming work occurs; those unchanged triangle counts are expected.

GPU calls in the checks are stubs. Actual Vita shader behavior, native thread scheduling, cache behavior, memory pressure, FPS and loading pauses need device verification. Existing compatibility limits remain: simplified lighting/terrain materials, incomplete NPC/scripts/quests, unsupported SpeedTree runtime, and visible-model collision in place of original Havok data. The exported maps remain the same 14 base-game worldspaces and 388 interiors, without DLC/mod overrides.

## Source and reproducible checks

The source archive contains code, prepared original assets/map packs, exporters, provenance and checks. Build with VitaSDK, CMake and Ninja:

```powershell
$env:VITASDK='<VitaSDK folder>'
cmake -S . -B build -G Ninja
cmake --build build
```

Host checks need Python 3, Windows x64 Zig, a host `zlib1.dll` and your original game archives:

```powershell
python tests/extract_fixtures.py --project . --game-data '<Data folder>'
python tests/run-host-checks.py --project . --sdk '<VitaSDK>' --zig '<zig.exe>' --fixtures tests/assets --zlib-dll '<zlib1.dll>'
python tests/run-world-checks.py --project . --sdk '<VitaSDK>' --zig '<zig.exe>' --game-data '<Data folder>' --zlib-dll '<zlib1.dll>' --preview-dir tests/previews
```

`tests/visual_review/reference_viewmodel009.h` preserves the prior interpolation for equivalence checks. `world_reference009.cpp` is an optional comparison harness for a separate 00.09 checkout, selected with the world runner's `--checks` argument. UI preview images are CPU rasterizations with supplied test numbers; they are not device screenshots or benchmark results.
