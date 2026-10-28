# Crate — the container plugin

A VST3 **effect** whose job is to host other VST3 plugins, chosen from the library the
DAW already scanned. This document is the map: what is built, what is deliberately not
built yet, how to compile it, and the rules the audio path follows.

## Layer map

| Layer | Path | Depends on | Status |
|---|---|---|---|
| Utilities | `source/util/` | nothing | done, tested |
| JSON5 parser | `source/json/` | nothing | done, tested |
| Host registry readers (REAPER `.ini`, Live SQLite, FL `.fst`) | `source/registry/` | util, json, bundled SQLite | done, tested |
| Versioned state codec | `source/state/` | util | done, tested |
| Crash journal + blacklist | `source/app/` | util | done, tested |
| **Hosting engine** (slot, chain, latency compensation, bypass ramps) | `source/host/` | util, registry model | done, tested |
| **VST3 plugin** (processor, controller, factory, hosted-plugin wrapper) | `source/vst3/` | VST3 SDK + everything above | builds with the SDK |
| Grid editor | — | — | **next milestone** |

The engine (`source/host/`) is SDK-free on purpose: it is the code that runs inside
`process()`, and it is unit-tested with fake plugins so a regression shows up in a test
run and not in somebody's session.

## Build

### Core + tests (no SDK, no CMake needed)

```bash
cd vst3-container
bash scripts/build-tests.sh        # builds and runs the whole suite
```

or with CMake:

```bash
cmake -S . -B build -DCRATE_BUILD_PLUGIN=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

### The plugin

Requires the Steinberg VST3 SDK (3.7.x or 3.8.x; the code uses only the stable hosting
API — `Module::create`, `IPluginFactory`, `IComponent`, `IAudioProcessor`,
`IEditController`, `MemoryStream`).

```bash
# with a local SDK checkout
cmake -S . -B build -DCRATE_VST3_SDK_DIR=/path/to/vst3sdk
cmake --build build --config Release

# or let CMake fetch it (needs network and git submodules)
cmake -S . -B build -DCRATE_FETCH_VST3_SDK=ON
cmake --build build --config Release
```

Install the bundle where the DAW scans:

* Windows: `%ProgramFiles%\Common Files\VST3\Crate.vst3`
* macOS: `~/Library/Audio/Plug-Ins/VST3/Crate.vst3`
* Linux: `~/.vst3/Crate.vst3`

The bundle's platform entry points come from the SDK itself: `CMakeLists.txt` calls
`smtg_target_add_library_main(Crate)`, which compiles `dllmain.cpp` on Windows
(`DllMain`, `InitDll`, `ExitDll`) and `linuxmain.cpp` / `macmain.cpp` on the other
platforms (`ModuleEntry`). Do not drop that call: a bundle that only exports
`GetPluginFactory` still loads on Windows (where `InitDll` is optional) but not on
Linux, where `ModuleEntry` is mandatory.

Then **rescan plugins in the DAW**. A container that "does not appear" is almost always
a wrong folder or a host that has not rescanned.


## What works today

* **The editor: a searchable grid of the host's own plugin registry.** Cards show name,
  vendor, category and a status badge; a vendor accent makes the wall readable before
  snapshots exist. Clicking a card (or Enter) loads it into the selected slot; the eight
  slot chips at the top choose the slot. Arrow keys, Page Up/Down, the wheel and the
  scrollbar navigate. The header always names the registry the list came from, and the
  footer reports the load result or the reason it failed — never an unexplained empty grid.
* **No scanning on open.** The list is the host registry, read once; `moduleinfo.json` is
  read per card, a few cards per idle tick, and a click resolves its own card if needed.
* Loads the plugins named in the session state, in slot order, in series.
* Bit-transparent pass-through when no slot is loaded (`Chain` copies, never masks).
* Latency compensation: every slot's dry path is delayed by exactly the hosted plugin's
  reported latency, so mix and bypass stay in phase. `getLatencySamples()` is the sum.
* Click-free bypass and mix: a 20 ms per-sample ramp, no `setProcessing` toggling.
* Silence flags: forwarded in, honest out (`lastOutputSilent` per slot, drained dry path).
* NaN/Inf scrubbing at every slot boundary, so a broken plugin cannot poison the master.
* Missing/moved plugins and UID mismatches produce a status sentence, not a crash; a
  slot that fails to start degrades to pass-through while the rest of the chain runs.
* Safe mode: if the journal says the host died while loading a plugin, that plugin is
  left out of the next session and named on its card.
* State: schema-v2 blob (slots, UIDs, paths, per-slot bypass/mix, hosted component and
  controller state), plus a checksummed controller blob for the selected slot.

## What is not built yet (stated, not hidden)

1. **Snapshot images on the cards.** `moduleinfo.json` snapshots are parsed and cached by
   the registry layer, but the editor draws a vendor-coloured accent instead of the image.
   Loading visible bitmaps is the next UI commit.
2. **A Linux/macOS editor build.** The editor is VSTGUI and defaults on for Windows (the
   shipped bundle). On Linux VSTGUI needs the X11/cairo/pango development packages; the
   CMake option `-DCRATE_BUILD_EDITOR=ON` is there, but it is not wired into CI.
3. **MIDI routing.** Only the main audio bus pair is hosted; instruments load but do not
   receive events. Audio effects are unaffected.
4. **Forwarding `restartComponent` from hosted plugins.** The container tells its own host
   that latency changed after a load, but a hosted plugin that changes latency at runtime
   is only noticed (`HostedComponentHandler::latencyChanged`), not yet pushed upstream.
5. **Out-of-process loading.** Crashing plugins take the container down with them; the
   journal and safe mode bound the damage, they do not prevent it.
6. **Parameter mirroring.** Hosted parameters do not appear in the DAW's automation list.

## The rules the audio path follows

Every one of these has a test or a comment next to the code that enforces it:

* no allocation, no lock, no I/O, no logging, no exceptions in `process()`,
* buffers sized once in `prepare()` from `ProcessSetup.maxBlockSize`,
* blocks larger than the prepared size are chunked, and the tail is zeroed rather than
  left stale,
* hosted plugins are only loaded and activated on the message thread,
* latency and tail are cached in atomics so the DAW can ask from any thread,
* state is written from a snapshot, never by poking the chain from the UI thread.

## Testing

`scripts/build-tests.sh` is the fast inner loop and what CI should run. It compiles
`source/**` **except** `source/vst3/` (that needs the SDK) plus `tests/**`, and runs the
suite. Fixtures for the registry readers live in `tests/fixtures/`.

The engine tests use `tests/fake_plugin.h`: a fake plugin with real gain, real delay,
optional failure and optional NaN output. If a test there fails, the bug is in the
engine, not in a third-party plugin.

When the SDK is available, `ctest -R crate_hosting_check` runs the integration check:
`tests/integration/gainplugin.cpp` builds a fixture `.vst3` (gain 2.0, 32 samples of
latency) and `tests/integration/hosting_check.cpp` loads it through the container's own
state format and asserts that

* the hosted gain is applied (0.25 in, 0.50 out),
* the container reports the plugin's 32 samples of latency,
* 50 % wet stays in phase (0.375 out) because the dry path is delayed by the latency,
* bypass passes the dry signal,
* a missing bundle degrades to pass-through instead of crashing or going silent,
* saving and re-reading the state keeps the slot's UID and bundle path.

That check is what catches SDK-API-level mistakes (wrong interface, wrong IID, a plugin
class that cannot be found) that the SDK-free tests cannot see. It also loads
`Crate.vst3` itself and asserts that the factory creates the audio component, that the
component names its controller class, and that the controller initializes and exposes
the slot parameters. That is the contract a host uses to find the editor; a component
without it is treated as having no controller at all (`Component does not provide a
required controller class ID` in the SDK validator).

Run the SDK validator before shipping — hosts blacklist failing plugins permanently:
`validator Crate.vst3` from the SDK's `public.sdk/samples/vst-hosting/validator`. The
bundle above passes all 47 validator tests (the "64bit Audio Processing not supported"
lines are informational: Crate intentionally accepts `kSample32` only).

## Next milestone: inside the card

The grid, the search, the slot chips and the click-to-load path are in place. The next
work is inside a loaded card: embed the hosted plugin's own editor (`IEditController::
createView` + our `IPlugFrame`) in a panel of the container, and draw `moduleinfo.json`
snapshot images instead of the vendor accent. After that: MIDI routing to hosted
instruments, and mirroring hosted parameters into the DAW's automation list.
