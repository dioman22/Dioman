# Prompt: Build a VST3 Plugin That Indexes and Hosts Your VST3 Library

A copy‑paste brief for an AI coding agent building **a VST3 plugin whose editor is a Plugindex‑style
picture grid of your plugin library, and which hosts the plugin you pick inside its own signal chain**.

The difference from the desktop‑app version of this idea is not cosmetic:

1. **The container is the product.** A plugin cannot put another plugin onto a DAW track — no host API
   exposes that to a plugin. What it *can* do is scan the library, show it as cards, and load the chosen
   plugin **inside itself** (`IComponent` + `IAudioProcessor` + `IComponentHandler` + `IPlugView`). So the
   workflow becomes: put the container on the track once, and swap the contents with a click. No
   accessibility permissions, no keystroke automation, no per‑DAW adapters, no version‑keyed recipes —
   it works in Ableton, Cubase, Bitwig, REAPER, FL Studio and Studio One on day one, because it is just a
   plugin.
2. **Audio realtime is now your problem, permanently.** An app that stutters is annoying; a plugin that
   allocates, locks or blocks on the audio thread is a broken session. Every design decision is checked
   against "what happens when this runs 512 samples at a time under a 64‑sample buffer".
3. **The DAW's stability is your responsibility.** Scanning thousands of foreign plugins inside the host
   process means one bad plugin takes the user's session with it. Scanning therefore happens in a
   separate helper process (or a companion scanner app), and the plugin ships a **safe mode** that can
   always open a project even when a hosted plugin is missing, broken or unlicensed.

Output is a `.vst3` bundle — no installer, no `.exe`, no browser. See [Scope notes](#scope-notes) for
what "same functionality as Plugindex" honestly means in this shape.

---

## 1. Compact version (quick triage)

> You are a senior audio‑plugin engineer (VST 3 SDK, realtime DSP, GUI embedding). Build a **VST 3 audio
> effect** that is a *plugin browser + container*: its editor is a searchable grid of the VST3 plugins
> installed on the machine — vendor snapshot images, name, vendor, category from `moduleinfo.json`
> (JSON5), arch and health badges — and clicking a card loads that plugin **inside the container**. The
> library index is produced by a separate scanner helper (a small executable you also ship), never by
> loading foreign plugins in the host process at scan time. Requirements: bit‑transparent passthrough
> when the slot is empty or the container is bypassed; correct latency and tail reporting
> (`getLatencySamples`/`getTailSamples`), sample‑accurate MIDI/parameter routing, hosted‑plugin state
> saved in your own state stream with class UID + path + version, missing‑plugin detection with a safe
> mode, out‑of‑process scanning with a blacklist for crashers, and a grid that stays responsive while
> audio runs. Do not allocate, lock, log or touch the filesystem on the audio thread. Do not scan
> plugins from the audio thread or the host's main thread during `process`. Do not invent metadata; if
> `moduleinfo.json` is absent, the scanner probes `GetPluginFactory`/`PClassInfo2` out of process. Prove
> every claim with the SDK `validator`, a real DAW, and measured numbers — and stop to report after each
> phase.

---

## 2. Long‑form prompt

Fill the `INPUTS` block, paste everything between the BEGIN/END markers, and include Appendix A (it
carries the hosting‑API map the phases cite) and Appendix E (the test matrix). Ask for the SDK version
and the target hosts if they are missing rather than assuming them.

----- BEGIN PROMPT -----

### ROLE

Act as one engineer with three specialisms:

- **Audio plugin developer** — VST 3 SDK hosting side (`Module`, `PlugProvider`, `IAudioProcessor`,
  `IComponentHandler`, `IPlugFrame`), bus negotiation, parameter automation, state serialisation.
- **Realtime DSP engineer** — lock‑free audio path, preallocated buffers, sample‑accurate event
  routing, latency/tail accounting, denormal and NaN hygiene, bypass without clicks.
- **Desktop UI engineer** — embedding foreign plugin editors into your own view on macOS and Windows,
  HiDPI, focus and keyboard handling, and a grid that renders 1,500 cards without touching the audio
  thread.

The user is a producer in the middle of a session. A crash is not a bug report, it is a lost afternoon.

### OBJECTIVE

Ship one VST 3 plugin (audio effect) that:

1. **Indexes** the local VST3 library via a companion scanner, without ever loading a foreign plugin
   into the host process during scanning.
2. **Shows** that library as a picture grid inside its editor, searchable and filterable by vendor,
   category, sub‑category, instrument/effect, architecture and health.
3. **Hosts** the selected plugin in its own chain — one primary slot minimum, an ordered multi‑slot
   chain as the target — with audio, MIDI and automation flowing correctly.
4. **Embeds** the hosted plugin's editor in an area of your UI, resizing and focusing correctly on
   macOS and Windows.
5. **Restores** sessions reliably: full state, missing‑plugin detection, version and UID mismatch
   handling, and a safe mode that always opens.
6. **Reports latency and tail** honestly, so the DAW's delay compensation stays correct as slots change.
7. **Survives** bad plugins: a blacklist written from recorded crashes, and no path where a hostile or
   broken plugin can hang the host's main thread indefinitely.
8. **Stays bit‑transparent** when empty or bypassed, and silent (not noisy) when a slot fails.
9. **Works in every VST3 host** without host‑specific code — no accessibility, no keystroke synthesis,
   no per‑DAW adapters. Verify on at least four hosts.
10. **Ships** as a signed/notarised `.vst3` bundle for macOS and Windows, with a companion scanner.

### INPUTS

```
PRODUCT_NAME:          <your name>
PLUGIN_KIND:           <Fx container (default) / Instrument container / both as two bundles>
SLOTS:                 <1 (MVP) / 4 serial / 4 serial + 4 parallel sends>
STACK:                 <JUCE 8  |  raw VST 3 SDK + VSTGUI  |  raw VST 3 SDK + native UI>
TARGET_OS:             <macOS 11+ arm64+x86_64 / Windows 10-11 x64 / both>
HOSTS_TO_VERIFY:       <Ableton Live 12, REAPER, Bitwig, Cubase, Studio One — pick ≥4>
VST3_SDK_VERSION:      <e.g. 3.8.0 — confirm before coding>
PARAM_MIRRORING:       <none (MVP) / read-only display / full automation mirror>
SCANNER_FORM:          <companion .exe/.app  |  CLI shipped in the bundle  |  both>
LICENSE_MODEL:         <paid / free / open source — matters for JUCE vs SDK choice>
TELEMETRY:             none
TEST_LIBRARY:          <path with real .vst3 bundles, or "none — generate fixtures">
SCOPE_DEADLINE:        <e.g. "1 slot, 2 hosts, 4 weeks">
```

State the **stack decision and its licence consequence** in one line before Phase 0 ends: JUCE 8 is
AGPLv3 or commercial (fast, handles hosting + editor embedding), the VST 3 SDK alone is MIT since 3.8
(more work: you write the host classes and view embedding yourself). This is a business decision, not a
style preference — do not make it silently.

### NON‑GOALS (hard scope — say no early)

- **No inserting plugins into the DAW's chain.** Impossible from a plugin; do not promise it in UI copy.
- **No AU, VST2, AAX, CLAP.** VST 3 only, exactly as before.
- **No per‑DAW adapters, no UI automation, no accessibility permissions, no global hotkeys, no
  Stream Deck, no phone remote.** Those existed because the app was outside the DAW; a plugin does not
  need them. Deleting them is most of the win.
- **No scanning inside the host process.** Not on load, not in a background thread, not "just for
  metadata". The host's address space is sacred.
- **No hosting in the audio thread.** All module loading, view creation and state I/O happens on the
  message thread, marshalled off the audio path.
- **No out‑of‑process audio hosting in v1.** It is the correct long‑term answer for hostile plugins and
  for macOS library validation; it is also a second product's worth of IPC and shared memory. Ship
  in‑process with a blacklist and safe mode first, and document the risk plainly.
- **No plugin licensing circumvention.** If a hosted plugin refuses to run, report it; never work around
  activation.

### OPERATING RULES (non‑negotiable)

- **R1 — Nothing on the audio thread but arithmetic.** No `new`, no `malloc`, no locks, no `printf`, no
  filesystem, no string formatting, no `std::function` allocation, no mutex around a plugin list. Prove
  it with a realtime‑safety audit of the `process()` call graph.
- **R2 — Never load foreign code while the DAW is watching.** Indexing is a separate process; the
  plugin reads a JSON index. If a plugin must be loaded, it happens on an explicit user action, on the
  message thread, with a progress state.
- **R3 — The empty container is a wire.** With no slot loaded (or all slots bypassed) output must be
  bit‑identical to input, including channel count, in all supported layouts.
- **R4 — Latency and tail are contracts.** `getLatencySamples()` returns the summed hosted latency;
  `kLatencyChanged` from a hosted plugin triggers `restartComponent(kLatencyChanged)` on yours;
  `getTailSamples()` reflects the tail. Wrong numbers here desync every track in the project.
- **R5 — State is portable and honest.** Save hosted class UID, bundle path, display name, vendor,
  version and the hosted plugin's own state blob, plus your slot/bus/latency config. On restore, if the
  UID is missing, show a placeholder slot — never silently drop the slot or rewire the chain.
- **R6 — Safe mode always opens.** A modifier held at instantiation (or a preference) starts with all
  hosted plugins disabled, so a user can rescue a session a broken plugin would otherwise kill.
- **R7 — Fail to silence, never to noise.** Any slot failure produces silence on that slot's output plus
  a visible error state — not NaNs, not full‑scale DC, not feedback.
- **R8 — No metadata invention.** Name/vendor/category/UID come from `moduleinfo.json` or an
  out‑of‑process `PClassInfo2` probe. `UNKNOWN` is a valid value; guessing is not.
- **R9 — Stable parameter IDs.** Container parameters (slot bypass, mix, and any mirrored hosted
  parameters) must keep their IDs forever; automation written in v0.9 must still address the same thing
  in v2.0.
- **R10 — Honest UI.** Show `unknown`, `missing`, `incompatible` and `crashed` states as distinct,
  explained states. Do not grey a card without a reason the user can read.

---

### PHASE 0 — Decisions and two risk spikes (before feature code)

Write `SCOPE.md` and run exactly two spikes. Both can end the project; better now than in week six.

1. **Container shape.** Decide `PLUGIN_KIND` and `SLOTS` and write down the consequences: an `Fx`
   container can host instruments only if the host routes MIDI to it (many hosts do, some need the
   plugin to report an input event bus); an `Instrument` container gets MIDI and note events but is
   awkward as an insert on audio tracks. If unsure, build **two bundles from one codebase** (a
   compile‑time switch) rather than one confused plugin.
2. **Bus contract.** Choose your layouts (mono→mono, stereo→stereo, stereo+sidechain, and what you do
   when a hosted plugin has different channel counts). Write the negotiation table down; it is the
   single most common source of "it works in Bitwig but not in Cubase".
3. **Spike A — editor embedding.** Build a throwaway plugin that loads one known VST3 and embeds its
   `IPlugView` in a child area on macOS (NSView) and Windows (HWND). Success = the hosted UI paints,
   resizes, accepts keyboard focus and does not crash on editor close/reopen ×20. If this fails, nothing
   else matters — stop and report.
4. **Spike B — macOS library validation.** On a **hardened‑runtime host with library validation
   enabled** (Logic‑class behaviour; test with `codesign`‑restricted builds and at least one host that
   hardens hard), confirm that your signed plugin can `dlopen` a third‑party `.vst3` binary signed by a
   different team. If it cannot, you need the out‑of‑process hosting escape hatch (or a documented
   limitation) — decide that **now**, because it changes the architecture, the bundle contents and the
   release plan. Record the exact error (`code signature invalid`, `library load disallowed by system
   policy`) in the report.
5. Write the **IPC/contract for the scanner**: the plugin never scans; it reads `index.json` (+ optional
   SQLite) produced by the companion scanner, watches its mtime, and reloads on the message thread.
6. Report: the two decisions, the two spike results, and the one thing you are least confident about.

**Stop and report.**

### PHASE 1 — Scanner and index (separate process, zero host risk)

The scanner is a small CLI/app shipped beside the plugin. Deliver it before the plugin's UI, because the
plugin's whole data model depends on it.

1. Enumerate roots per the format's priority order (user → global → application), user roots first,
   first bundle per UID wins, losers recorded as `shadowed`:
   - macOS: `~/Library/Audio/Plug-Ins/VST3/`, `/Library/Audio/Plug-Ins/VST3/`,
     `/Network/Library/Audio/Plug-Ins/VST3/`
   - Windows: `%LOCALAPPDATA%\Programs\Common\VST3\`, `%ProgramFiles%\Common Files\VST3\`,
     `%ProgramFiles(x86)%\Common Files\VST3\`
2. Parse `Contents/Resources/moduleinfo.json` (3.7.8+) **and** `Contents/moduleinfo.json` (3.7.5–3.7.7)
   with a **JSON5** parser — the file legally contains comments and trailing commas, so a strict JSON
   parser fails on real bundles. Keep class entries with `Category == "Audio Module Class"` only.
3. Handle merged bundles (several arch folders in one `.vst3`), legacy single‑file `.vst3`, symlinks
   with cycle detection, and bundles with no `moduleinfo.json` (probe out of process, `PClassInfo2`).
4. Emit **`index.json`** plus an `images/` folder (vendor `Snapshots[]` first, generated tile as
   fallback). Keep it small: target <2 MB for 1,500 plugins, with image paths rather than base64 blobs.
5. Record health per plugin in the index: `ok`, `unreadable`, `arch_mismatch`, `not_loadable`,
   `license_required`, `blacklisted` — written by the scanner's own probe run, never guessed by the
   plugin.
6. Make the scanner runnable headlessly (`scanner --scan --json`) so CI and the evidence scripts can
   drive it, and make it incremental (fingerprint by path+size+mtime+inode) so a rescan is cheap.
7. Report: plugin counts per root, JSON5 parse failures, index size, cold/warm scan times.

**Stop and report.**

### PHASE 2 — Hosting engine (the heart of the product)

Build this before the pretty grid. An empty‑looking grid over a solid engine is shippable; a beautiful
grid over a flaky engine is not.

1. **Loading a hosted plugin.** Use the SDK's hosting path — `Module::create(path, ...)`,
   `GetPluginFactory`, `IPluginFactory::createInstance` with `IComponent`'s IID, then the
   `IEditController` (via `IComponent::getControllerClassId`, or the factory if the plugin has no
   separate controller). `PlugProvider` (SDK `public.sdk/source/vst/hosting/`) already encapsulates most
   of this — read it and the SDK's hosting samples before writing your own. Never address classes by
   index; always by CID.
2. **Wire the host side.** Provide `IHostApplication` (with a working `createInstance` for your own
   classes), `IPlugFrame` (`resizeView`), and `IComponentHandler` (+ `IComponentHandler2::setDirty`) to
   every hosted instance. Missing or stubbed host interfaces are the #1 cause of "this plugin works
   everywhere except inside your container" — the SDK's `hostchecker` plugin exists precisely to catch
   that, and you should run it against your container.
3. **Processor lifecycle.** `initialize` → `setActive` on activation → `setupProcessing(sampleRate,
   maxSamplesPerBlock, ...)` whenever the host's buffer size changes → `setProcessing(true)` per
   process cycle. Call `setProcessing` only when the state actually changes; per‑block toggling is a
   common CPU and glitch bug.
4. **Bus negotiation.** Implement `setBusArrangements` on the container by forwarding to hosted plugins
   where possible and falling back to your documented layout table. Handle `kIoChanged` from hosted
   plugins by re‑negotiating and notifying the host via `restartComponent(kIoChanged)`.
5. **The audio path.** Preallocate everything at `setupProcessing` time. Point the hosted plugin's
   `ProcessData` at your own `AudioBusBuffers` (no copies where avoidable; one scratch buffer pool if
   you must copy for layout conversion). Respect `silenceFlags` in both directions — a silent input bus
   lets well‑behaved plugins skip work. Propagate `processContext` (tempo, time signature, transport
   state, sample position, `kPlaying`) so hosted plugins can sync LFOs and tempo‑delay.
6. **Events.** Route `inputEvents` → hosted bus, and hosted `outputEvents` → host, with correct
   `sampleOffset` values and bus indices. If you host an instrument inside an effect container, be
   explicit about what you do with notes.
7. **Parameters.** Decide `PARAM_MIRRORING`: none, read‑only, or full. Full mirroring means mapping
   hosted parameter IDs into your own parameter namespace with stable, collision‑free IDs and pushing
   `performEdit`/`beginEdit`/`endEdit` through `IComponentHandler`; handle `kParamValuesChanged` and
   `kParamTitlesChanged` from hosted plugins, and expect some to fire from the audio thread (defer to
   the message thread).
8. **Latency and tail.** Sum hosted `getLatencySamples()`; compensate the chain internally if you can,
   otherwise report the sum. Handle `kLatencyChanged` by calling `restartComponent(kLatencyChanged)` on
   your controller. Implement `getTailSamples()` as the sum of hosted tails, clamped to `kInfiniteTail`
   rules.
9. **Bypass.** Container bypass must be click‑free (short crossfade or immediate with a flush of hosted
   delay lines), must still run the hosted plugin or explicitly stop calling it — pick one and document
   it, because both are wrong in some situation.
10. **Threading.** All plugin loads, editor creation/destruction, state save/load and index reloads
    happen on the message thread. Audio‑thread → message‑thread communication goes through a lock‑free
    queue or an atomic flag + `AsyncUpdater`‑style deferral. Hosted plugins that call
    `restartComponent` from `process()` must never deadlock your message loop.
11. Report: a worked `process()` call graph annotated with "allocates? locks? syscall?" for every node,
    plus a 10‑minute stress run in one host with no xruns and no denormals.

**Stop and report.**

### PHASE 3 — Embedding a hosted plugin's editor

1. Get the hosted editor: `IEditController::createView(ViewType::kEditor)`; if the plugin only has a
   generic view, still support it (many older plugins do). `IPlugView::isPlatformTypeSupported` tells you
   what it can attach to (`kPlatformTypeNSView` on macOS, `kPlatformTypeHWND` on Windows).
2. Attach to a child view you own, and set the frame: `setFrame(yourPlugFrame)` before `attached(parent,
   platformType)`. Implement `IPlugFrame::resizeView` by resizing your child container and re‑laying out
   surrounding chrome; never resize the top‑level editor from a hosted plugin without clamping.
3. **HiDPI and scale.** Handle `IPlugViewContentScaleSupport::setContentScaleFactor` when available; if
   not, be honest and render at 1× rather than blurring. Remember the hosted editor's preferred size and
   restore it per plugin.
4. **Focus and keyboard.** The hosted view must receive key events when it has focus, and your grid must
   not steal them. Escape returns focus to your UI; Tab does not trap. Provide a visible "focus is in
   hosted plugin" indicator, because otherwise shortcuts appear broken and users blame the DAW.
5. **Lifecycle, the crashy part.** Destroy order matters: `removed()` → release view → release
   controller → release component. Some plugins crash if the view outlives the controller, or if
   `removed()` is never called. Exercise open/close/reopen ×50 on each test plugin; log which plugin
   crashed at which step and blacklist it.
6. **Never create a view on the audio thread**, never block the audio thread waiting for a view to
   appear, and never hold a lock the audio thread needs while an editor is being constructed.
7. Report: a table of 10 real plugins × (paints, resizes, keyboard, reopen ×50) with failures named.

**Stop and report.**

### PHASE 4 — The grid, inside a plugin editor

The editor is a small window in a DAW, often 600×400, sometimes scaled. Design for that, not for a
1600 px web page.

1. **Layout.** Grid of cards (vendor snapshot, name, vendor, category chip, health dot, instrument/effect
   mark), a search field, filter row, and the hosted plugin's editor in a resizable panel with your
   chain around it. Remember per‑DAW window size and the split between grid and editor pane.
2. **Performance under audio.** Image decode off the audio thread and off the message thread (worker
   pool), cache decoded bitmaps with a hard memory cap, lazy‑load visible tiles only. Never let a
   scroll trigger disk I/O that competes with a plugin load.
3. **Search.** Tokenized fuzzy match over name, vendor, category, sub‑category and tags; prefix
   weighting; 50 ms debounce; `/` focuses; arrows move; `Enter` loads into the slot; `Esc` closes.
4. **States, all explained.** `ready`, `unknown` (not probed), `unreadable`, `incompatible` (arch),
   `license_required`, `blacklisted`, `crashed`, `missing` (project references it, plugin absent). Each
   with a one‑line reason and a suggested fix.
5. **Slot view.** Show the chain, per‑slot bypass, remove, reorder (drag), and a clear "no plugin" state.
   Show the summed latency and tail where the user can see them — this is the number that explains "why
   is my track late".
6. **Rescan affordance.** A rescan button that launches the scanner, shows progress, and hot‑reloads the
   index on completion — all off the audio thread and none of it blocking the editor.
7. **Accessibility and keyboard.** Full keyboard operation, visible focus, screen‑reader names on cards.
   A plugin editor that cannot be driven by keyboard is a plugin that cannot be used in a scoring
   session.
8. Report: editor memory with 1,500 cards, scroll smoothness while audio runs, and open/close time.

**Stop and report.**

### PHASE 5 — State, patches, missing plugins

1. **Your state stream.** Serialise a versioned container: schema version, slot count, per‑slot
   `{uid, bundle_path, name, vendor, version, bypass, mix, hosted_state_blob}`, bus/latency config, and
   UI preferences that matter to recall. Use `IComponent::getState`/`setState` for the hosted plugin's
   own blob (and `IEditController::setComponentState` on restore — forgetting this is why hosted plugins
   come back with wrong knob positions).
2. **Robust restore.** Never fail the whole load because one slot is missing. Insert a **missing slot
   placeholder** that (a) passes silence or the documented fallback, (b) shows the name/vendor it wanted,
   (c) offers "locate…" and "remove". This is the container version of "check a session before you open
   it".
3. **UID and version drift.** If the bundle at the saved path now has a different UID, or the plugin's
   `Compatibility` maps an old UID to a new one, say which happened and load the replacement only when
   the user accepts it. Silent substitution corrupts mixes in ways nobody can debug later.
4. **Patches.** Save/load your own patch format (`.json` + embedded blobs, or a zip). Name it something
   that cannot be confused with `.vstpreset`. Include the hosted UIDs so a patch is shareable with a
   clear "these 2 plugins are missing" report.
5. **A/B and undo.** Keep at least two container states for A/B, and make swap gapless (no clicks).
   Undo for slot changes goes through `IComponentHandler2::setDirty` so the DAW marks the project
   modified.
6. Report: a session round‑trip test (save → close DAW → reopen → identical audio), plus a
   missing‑plugin test where one of three slots is absent.

**Stop and report.**

### PHASE 6 — Safety: blacklist, hangs, safe mode

1. **Crash isolation where you can get it.** The scanner probes out of process (Phase 1). In‑process
   hosting remains the crash surface, so the plugin must record **which** plugin was being loaded/opened
   when the host last died: write a "loading started / loading finished" marker to a small journal file
   (message thread, before and after the risky call). On next start, if the journal is dirty, name the
   suspect and offer to blacklist it.
2. **Blacklist.** A per‑user file (next to the index) of UIDs marked `crashed_on_load`, `crashed_on_edit`
   or `hang_on_load`, with a "try again" action. Never blacklist silently and never blacklist
   permanently without a user‑visible reason.
3. **Hang protection.** Any hosted call that can block (load, `setActive`, editor creation, state I/O)
   runs on a worker thread with a timeout and a progress UI that can be cancelled. You cannot kill a
   thread safely inside the host process, so design for "abandon and mark suspect" rather than "kill".
4. **Safe mode.** A documented way to instantiate with all hosted plugins disabled (modifier key at
   load, plus a preference), so a session with a poisoned plugin can always be opened and repaired.
5. **Numeric hygiene.** Scrub NaN/Inf at slot boundaries, flush denormals in your own DSP, and
   hard‑limit output gain in the container's mix stage. A hosted plugin that emits NaN must not take the
   rest of the chain with it.
6. Report: the journal format, three induced failure scenarios (crash on load, hang on load, NaN output)
   and what the user sees in each.

**Stop and report.**

### PHASE 7 — Build, bundle layout, signing

1. **Output is a `.vst3` bundle**, per platform:
   - macOS: `Product.vst3/Contents/MacOS/Product` (`CFBundleExecutable`, `Info.plist`,
     `PkgInfo` optional) — universal `arm64 + x86_64` unless you have a reason not to.
   - Windows: `Product.vst3/Contents/x86_64-win/Product.vst3` (folder bundle; keep the inner `.vst3`
     name identical to the bundle name).
   - Add `Contents/Resources/moduleinfo.json` generated at build time — your own plugin should be as
     introspectable as the ones you index. (`moduleinfotool` from the SDK.)
2. **Ship the scanner too.** Decide where it lives (companion app, or a CLI inside
   `Contents/Resources/` that the plugin can invoke). If it lives in the bundle, respect the rule that
   `Contents/Resources` is read‑only and signed — copy out before executing if the platform requires it.
3. **Signing.**
   - macOS: sign the bundle (and any nested helper) with a Developer ID, hardened runtime, notarise and
     staple. Test in a DAW **not** run from Xcode. Expect and document the library‑validation behaviour
     from Phase 0 Spike B.
   - Windows: sign the `.vst3` binary; expect SmartScreen/AV reputation work; avoid packers.
4. **No installer for the plugin itself.** The convention is "copy the bundle into the VST3 folder".
   Provide a tiny installer or a "reveal target folder" helper only if you want to be kind — and never
   write into a vendor's bundle.
5. **Versioning.** Plugin version, state schema version and index schema version are three different
   numbers. Store all three in saved state and refuse to load a state schema from the future with a
   clear message instead of corrupting a session.
6. Report: bundle tree for both OSes, `codesign -dv --verbose=4` output, notarisation ticket, and the
   SDK `validator` result.

**Stop and report.**

### PHASE 8 — QA, performance, docs

1. **Validator first.** Run the SDK `validator` (`public.sdk/samples/vst-hosting/validator`) against
   your plugin in every configuration, then `pluginval` at strictness 10 if you are on JUCE. A container
   that fails validation will be blacklisted by hosts — that is the whole product dead on arrival.
2. **Host matrix.** Verify the same build in at least four hosts (Ableton Live, REAPER, Bitwig,
   Cubase/Studio One): instantiate, load a plugin, open its UI, play 5 minutes, change buffer size,
   change sample rate, save/reopen, offline bounce, freeze. Record pass/fail per cell — this table is the
   release gate.
3. **Realtime budget.** Measure: empty container CPU (must be ~0), one hosted plugin's added CPU, your
   grid's CPU while scrolling during playback, allocations per `process()` call (must be 0), worst‑case
   `process()` time at 64‑sample buffer. Publish the numbers in `README`.
4. **Docs.** `README` (what it is, what it is not — say plainly that plugins load inside the container,
   not onto the track), `HOSTS.md` (verified matrix + known quirks), `SAFE-MODE.md`,
   `TROUBLESHOOTING.md` (plugin won't appear in the list, hosted editor blank, session opens silent,
   plugin crashed on load), `PRIVACY.md` (the index never leaves the machine).
5. **Release checklist.** State‑schema migration test from the previous version, a session saved with an
   older build reopened and bounced, validator + pluginval green, host matrix complete, signatures
   verified on a clean VM.
6. Report: the host matrix, the measured budget, validator output, and every known limitation.

**Stop and report. Final report must include the host matrix and an explicit list of what the container
cannot do (per‑plugin DAW automation, DAW‑side sidechain routing, plugin‑specific PDC, etc.).**

### ACCEPTANCE CRITERIA

Demonstrate each with output or a recording.

1. Empty container and fully‑bypassed container are **bit‑identical** to input at 44.1/48/96 kHz, mono
   and stereo, 64–1024 sample buffers (null test, sample‑exact, in a real host).
2. Zero heap allocations and zero lock acquisitions inside `process()` — proven with an allocation
   interposer/counters over a 10‑minute run, not by inspection.
3. `getLatencySamples()` matches the sum of hosted latencies after every slot change, and the host's PDC
   stays aligned (null test through a parallel path against a reference).
4. Session round‑trip: save → quit DAW → reopen → audio identical, hosted editors reopen with correct
   knob positions, patch names intact.
5. Missing‑plugin case: a session referencing an absent plugin opens with a named placeholder, produces
   silence on that slot (not noise, not a drop of the whole chain), and offers "locate"/"remove".
6. Safe mode opens a session that would otherwise crash, with all slots disabled and clearly labelled.
7. Loading a plugin that crashes the host leaves a dirty journal; next launch names the suspect and
   offers blacklisting; blacklisted plugins are visibly marked in the grid, not hidden.
8. Hosted editor: paints, resizes, takes keyboard focus, survives 50 open/close cycles, on macOS and
   Windows, for 10 real plugins (table of results).
9. Grid with 1,500 cards: first paint <300 ms, scroll stays smooth **during playback**, memory ≤200 MB,
   and the audio thread's worst‑case `process()` time does not regress with the editor open.
10. SDK `validator` and `pluginval` (strictness 10) pass; no host blacklists the plugin.
11. Verified working in ≥4 hosts with a filled‑in matrix, including offline bounce and buffer‑size change
    during playback.
12. State schema migration: a state saved by the previous released version loads correctly, or is
    refused with a clear message — never silently misread.

### THINGS THAT WILL MAKE YOU WRONG (read twice)

- **You cannot insert into the DAW's chain.** If the UI implies otherwise, the product is lying and the
  reviews will say so. Say "loads inside the container" everywhere.
- **`setState` alone is not enough on restore.** Hosted plugins also need
  `IEditController::setComponentState` (or the controller will show defaults while the audio is
  correct — a bug that looks like a UI bug and is not).
- **Latency changes must be announced.** Forgetting `restartComponent(kLatencyChanged)` after adding a
  look‑ahead plugin desyncs every parallel path in the project, and the user will blame the DAW.
- **Some plugins change bus layout on activation.** `kIoChanged` at the wrong moment (between
  `setupProcessing` and `process`) can wedge a host; defer re‑negotiation to the message thread and
  report it.
- **`resizeView` from a hosted plugin can arrive from any thread** in badly written plugins. Marshal it.
- **Editor destruction order kills.** `removed()` → view → controller → component. Skipping `removed()`
  is the classic crash‑on‑close.
- **Don't call `setProcessing(true/false)` every block.** It forces some plugins to flush internal state
  and sounds like a stutter.
- **Silence flags are not optional.** Ignoring `silenceFlags` on input costs CPU in every plugin that
  honours them; ignoring them on output makes naive hosts denormal‑crash.
- **Scanning from inside the plugin will eventually kill a session.** Even "just reading metadata" via
  `GetPluginFactory` executes foreign code. Separate process, always.
- **The index is data, not truth.** A plugin can be deleted, moved, updated or replaced between scan and
  load. Check existence and UID at load time and report the difference instead of failing obscurely.
- **macOS library validation can refuse the third‑party binary** even though the file is fine. Know the
  answer before shipping (Phase 0 Spike B).
- **A JUCE‑based container and an AGPL/GPL product are a licensing event**, and a raw‑SDK container is a
  schedule event. Pick deliberately and write it down.
- **Don't promise per‑plugin DAW automation unless you built mirroring.** It is a real feature with real
  parameter‑ID design work, not a checkbox.
- **Don't ship without running the SDK validator.** Hosts blacklist failing plugins permanently on the
  user's machine, and you cannot undo that from your side.

---

## Appendix A — The hosting API map (what you must implement, and where it lives)

You are writing the **host side** of VST 3, inside a plugin. Everything a DAW normally does for a
plugin, you now do for the plugins you host.

### A.1 Roles and interfaces

| You implement | Header (SDK 3.8) | Why it is needed |
|---|---|---|
| `IHostApplication` | `pluginterfaces/vst/ivsthostapplication.h` | every hosted plugin asks the host for its name and for `createInstance` |
| `IPlugFrame` | `pluginterfaces/vst/ivstplugview.h` | `resizeView()` — hosted editors ask permission to resize |
| `IComponentHandler` | `pluginterfaces/vst/ivsteditcontroller.h` | `beginEdit`/`performEdit`/`endEdit`, `restartComponent(flags)` |
| `IComponentHandler2` | same header | `setDirty` (marks the DAW project modified), group edits |
| `IComponentHandler3` | same header | hosted context menus / parameter right‑click (optional but expected) |
| `IPlugInterfaceSupport` (supported) | `pluginterfaces/vst/ivstpluginterfacesupport.h` | answer "do you support interface X" honestly |
| `IUnitInfo` (optional) | `pluginterfaces/vst/ivstunits.h` | program lists / unit names for the container UI |

The **hosted** plugin implements (you call): `IComponent`, `IAudioProcessor`, `IEditController`,
`IPlugView`, optionally `IUnitInfo`, `IProgramListData`, `IKeyswitchController`,
`IPlugViewContentScaleSupport`, `INoteExpressionController`.

Load helpers you should not reinvent (SDK `public.sdk/source/vst/hosting/`):
`module.h` (`Module::create`), `pluginfactory.h`, `plugprovider.h` (`PlugProvider` — load a bundle, get
component + controller + class info), `hostclasses.h` (reference `HostApplication`, `PlugFrame`,
`ComponentHandler`), `parameterchangehandler.h`, `eventlist.h`, `processdata.h`.
Samples to read before writing your own: `public.sdk/samples/vst-hosting/` (`validator`, `audiohost`,
`editorhost` — names as of 3.8, confirm in your tree) and the `hostchecker` plugin, which exists to
expose exactly the host‑side mistakes you are about to make.

### A.2 Load and setup sequence (in this order, on the message thread)

1. `Module::create(path)` → `GetPluginFactory()` → `IPluginFactory`.
2. `IPluginFactory::getClassInfo(i, PClassInfo2)` — pick the `Audio Module Class` by **CID**, never by
   index; read sub‑categories here for the UI.
3. `createInstance(cid, IComponent::iid, (void**)&component)`.
4. `component->initialize(hostContext)` — this is where you hand over `IHostApplication`,
   `IComponentHandler`, `IPlugFrame`, `IPlugInterfaceSupport`.
5. Controller: `component->getControllerClassId(cid)` then `createInstance(cid, IEditController::iid)` —
   **or** if `queryInterface(IEditController)` succeeds on the component (single‑component plugins), use
   that. Call `controller->initialize(hostContext)`.
6. `controller->setComponentHandler(yourHandler)`; connect the two with
   `component->connect(controller)` and `controller->setComponentState(componentStateStream)` when
   restoring.
7. `component->activateBus(kAudio, kInput, i, true/false)` per your arrangement; same for outputs and
   for `kEvent` buses (MIDI).
8. `component->setActive(true)`; `processor->setupProcessing(SetupData{sampleRate, maxSamplesPerBlock,
   symbolicSampleSize, processMode})`; `processor->setProcessing(true)`.
9. Reverse this exactly on unload: `setProcessing(false)` → `setActive(false)` → `terminate()` on both →
   release → release module. Skipping `terminate()` leaks and, in some SDK builds, leaves handlers
   dangling.

### A.3 `ProcessData` — the contract you must not break

```
ProcessData {
  processMode          // kRealtime (also kPrefetch, kOffline)
  symbolicSampleSize   // kSample32 or kSample64
  numSamples           // frames in this block
  numInputs / numOutputs
  inputs / outputs     // AudioBusBuffers*, one per active bus
  inputParameterChanges / outputParameterChanges   // IParameterChanges*
  inputEvents / outputEvents                      // IEventList*
  processContext        // tempo, timeSig, projectTimeSamples, continousTimeSamples, state (kPlaying...)
}
AudioBusBuffers { numChannels, silenceFlags, channelBuffers32 / channelBuffers64 }
```

Rules that keep you out of trouble:

- `maxSamplesPerBlock` in `SetupData` is the **maximum**; hosts may call with fewer. Never size buffers
  to `numSamples` at call time.
- `silenceFlags` is a bitmask per channel. Honour it on input; set it correctly on output (a bus you
  genuinely left silent, e.g. a failed slot, should be flagged, not filled with zeros you then process).
- Do not hold pointers to `AudioBusBuffers` beyond the call; the host reuses its own memory.
- Parameter changes arrive as `IParamValueQueue` per parameter ID with sample offsets — apply them at the
  right offset, not all at the block start, or automation will zipper.
- Offline/faster‑than‑realtime rendering (`kOffline`, `kPrefetch`) will hit your plugin too; anything you
  do that assumes wall‑clock time (timers, UI) must be skipped in those modes.
- 64‑bit processing: if you support `kSample64`, every buffer hop must be 64‑bit aware. If not, return
  the error and let the host fall back — do not silently truncate.

### A.4 Buses and arrangements

- Query `getBusCount(kAudio, dir)` + `getBusInfo(i, BusInfo&)`; `BusInfo` carries `mediaType`,
  `busIndex`, `channelCount`, `busType` (`kMain` / `kAux`) and flags (`kDefaultActive`,
  `kIsControlVoltage`). `activateBus` before `setActive`.
- Your container advertises its own layouts (`kStereo` most of the time) and implements
  `setBusArrangements(inputs, numIns, outputs, numOuts)` — accept, adapt, or reject; rejecting with
  `kResultFalse` is correct behaviour and much better than mis‑routing.
- A hosted plugin may have a different channel count. Either refuse to load it into an incompatible slot
  (with a clear message) or convert explicitly with a documented gain policy. Hidden conversion is how
  mono plugins end up 6 dB quiet.
- Sidechains: if you expose one, it must be a real `kAux` input bus and the DAW must see it in its own
  routing UI. Do not fake it.

### A.5 Latency, tail, and the messages that carry them

- Report `getLatencySamples()` = sum of hosted latencies (+ any conversion delay you introduce).
- When a hosted plugin calls `restartComponent(kLatencyChanged)` on **your** handler, forward
  `restartComponent(kLatencyChanged)` to **your** controller. The host then re‑queries your latency.
- `getTailSamples()` = sum of hosted tails; use `kInfiniteTail` only when a hosted plugin really is
  infinite (a reverb with freeze), because it makes offline render longer.
- Other `restartComponent` flags you must handle sensibly: `kParamValuesChanged`,
  `kParamTitlesChanged`, `kIoChanged`, `kReloadComponent`. Treat unknown flags as "ignore + log", never
  as a crash.
- Some of these calls arrive from the audio thread. Post them to the message thread; never take a lock
  in `process()` to handle them.

### A.6 State blobs

- Persist hosted state with `IComponent::getState(IBStream*)` into your own stream; restore with
  `setState` **and** `IEditController::setComponentState` (same bytes are acceptable for most plugins).
- Save `IEditController::getState` too if the plugin has controller‑only data (custom UI state, program
  selection). If you skip it, users report "the knobs reset but the sound is right".
- Your own envelope: `{ schemaVersion, slots:[{uid, path, name, vendor, version, bypass, mix, state}] }`.
  Keep the binary blob opaque — never parse it.
- Cap total state size (e.g. 64 MB) and degrade gracefully; a plugin with a sampled instrument can
  legitimately be tens of MB per instance.

### A.7 Editor embedding per platform

- **macOS.** `IPlugView::isPlatformTypeSupported(kPlatformTypeNSView)`; call
  `view->setFrame(yourPlugFrame)` then `view->attached((void*)nsView, kPlatformTypeNSView)`. Parent a
  `NSView` you own; do not grab the window. Handle `view->getSize(ViewRect&)` and
  `canResize()`/`checkSizeConstraint()`. Remember `removed()` before releasing.
- **Windows.** Same flow with `kPlatformTypeHWND` and a child `HWND`. Watch for plugins that create
  their own top‑level window anyway (some do) — detect and report rather than fighting it.
- **Scale.** `IPlugViewContentScaleSupport::setContentScaleFactor` if `queryInterface` returns it; else
  render at 1× and say so. On macOS, `view->onSize()` after a screen change.
- **Focus.** Give the hosted view keyboard focus when the user clicks it; return focus to your grid on
  `Esc`; never let the hosted view capture global shortcuts it should not own.
- **Modal loops.** Some older plugins run modal dialogs (file choosers) from their own message loop.
  Test at least one; if it wedges your UI, document it and blacklist that plugin in your UI, not in the
  host.

### A.8 Out‑of‑process scanning (the rule that keeps sessions alive)

The index must be produced without loading foreign code into the DAW:

1. Companion scanner process (a small `.exe` / `.app`, or a CLI in `Contents/Resources/`) enumerates
   roots and parses `moduleinfo.json` (JSON5).
2. For bundles with no `moduleinfo.json`, the scanner loads them **in its own process**,
   `GetPluginFactory` → `PClassInfo2`, one plugin per child process with a timeout, and reports the
   result. A crash there costs a scanner child process, not a session.
3. Scanner writes `index.json` + `images/` to a per‑user app‑data directory (or next to a
   `portable.flag`). The plugin only reads it, watches the mtime, and reloads on the message thread.
4. The plugin never shells out during `process()`, never scans on load, and never blocks the editor
   waiting for a scan. "Rescan" is an explicit button that shows progress.
5. Paths in the index are absolute, but every load still re‑validates existence, UID and mtime — indexes
   go stale the moment the user installs something.

### A.9 Realtime audit checklist (run it against your own code)

- No `new`/`malloc`/`std::string`/`std::function`/`std::vector` growth inside `process()` or anything it
  calls; use fixed arrays sized at `setupProcessing`.
- No mutex, no `std::atomic` with sequential consistency in the hot path where `memory_order_relaxed`
  suffices, no `shared_ptr` copies, no reference counting on audio thread.
- No `printf`/logging/`std::cout`, no file or socket I/O, no `gettimeofday` per block if you can help it
  (the host gives you the context).
- No exceptions across the audio callback; catch at the boundary and fail to silence.
- Denormals handled (FTZ/DAZ flags or `+0.0f` sprinkles), NaN/Inf scrubbed at slot boundaries.
- Editor/UI never touched from audio thread; communication via lock‑free FIFO or atomic + deferred
  callback.
- State save never happens on the audio thread and never blocks it (`getState` can be called by the host
  at any time — implement it as a copy from a snapshot you maintain off‑thread).

## Appendix B — Bundle layout and build output

```
# macOS
Product.vst3/
  Contents/
    Info.plist                 # CFBundleExecutable=Product, CFBundleIdentifier, version
    PkgInfo
    MacOS/Product              # universal arm64 + x86_64 (or two slices)
    Resources/
      moduleinfo.json          # generated with moduleinfotool
      ui/                      # your icons, snapshots, translations
      scanner/                 # optional CLI scanner (see Phase 7.2)

# Windows
Product.vst3/
  Contents/
    Resources/moduleinfo.json
    x86_64-win/Product.vst3    # inner name must equal the bundle name
```

- Hosts scan the standard folders (macOS `~/Library/Audio/Plug-Ins/VST3` and
  `/Library/Audio/Plug-Ins/VST3`; Windows `%ProgramFiles%\Common Files\VST3`). Tell users where to copy
  it, and make your docs match reality — "it didn't appear" is the most common first‑run ticket and is
  almost always a wrong folder or an architecture mismatch.
- Generate `moduleinfo.json` for your own plugin: correct `Category: "Audio Module Class"`,
  `Sub Categories: ["Fx", "Tools"]` (or `["Instrument"]`), `Cardinality`, and a `Snapshot` image so you
  look right inside other people's browsers.
- If you ship the scanner in `Contents/Resources/`, remember it is inside a **signed** bundle: copying it
  out before executing is often required, and the copy must be hash‑verified against the bundled file.
- Keep the plugin small (no sample content). A container that is 200 MB because of UI assets is a
  container people uninstall.

## Appendix C — Index schema (`index.json`)

```jsonc
{
  "schema": 1,
  "generated_at": 1770000000,
  "scanner_version": "1.0.0",
  "roots": [{ "path": "C:\\Program Files\\Common Files\\VST3", "kind": "global", "priority": 2 }],
  "plugins": [
    {
      "uid": "BD58B550F9E5634E9D2EFF39EA0927B1",   // Audio Module Class CID, uppercase hex
      "name": "Pro-Q 4",
      "vendor": "FabFilter",
      "version": "4.01",
      "sdk": "VST 3.7.5",
      "category": "EQ",                            // canonical, mapped from sub-categories
      "sub_categories": ["Fx", "Fx|EQ"],
      "is_instrument": false,
      "capabilities": ["stereo", "only_rt"],       // flags from PlugType, not categories
      "bundle": "C:\\Program Files\\Common Files\\VST3\\FabFilter Pro-Q 4.vst3",
      "arch": ["x86_64-win"],
      "legacy_single_file": false,
      "size_bytes": 41234567,
      "image": "images/BD58B5....webp",            // relative to index.json
      "image_source": "moduleinfo",                // moduleinfo|probed|generated
      "status": "ok",                              // ok|unreadable|arch_mismatch|not_loadable|license_required|blacklisted
      "status_detail": null,
      "probed_at": 1770000000
    }
  ]
}
```

Design rules: one row per `Audio Module Class` (a bundle can contribute several); `uid` is identity and
must be treated as opaque; `bundle` is a location that may vanish; `status` is data with a reason, never
a boolean. If you outgrow JSON, move to SQLite later — but keep the file format as the interchange
format so the scanner and plugin can be updated independently.

## Appendix D — The editor, concretely

The editor is a plugin window: small, resizable, sometimes scaled by the DAW, sometimes hosted in a
floating window the user resizes once and forgets.

**Default size** ~980×620; **minimum** ~720×420; remember the last size per user, and restore it on
open without fighting the host's own window management.

**Layout (single‑slot MVP)**

```
┌──────────────────────────────────────────────────────────────┐
│ [Search……………………………] [Category ▾] [Vendor ▾] [⟳ Rescan]    │
├──────────────────────────────────────────────────────────────┤
│  card  card  card  card  card  card                          │
│  card  card  card  card  card  card       ← grid, scrolls    │
│  card  card  card  card  card  card                          │
├──────────────────────────────────────────────────────────────┤
│ SLOT 1  [Pro-Q 4 · FabFilter]  [Bypass] [↗ UI] [✕]           │
│ Latency 0.0 ms · Tail ∞          ← the numbers that matter   │
│ ┌──────────────────────────────────────────────────────────┐ │
│ │            hosted plugin editor (embedded)               │ │
│ └──────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

Rules:

- The grid and the embedded editor compete for space; make the split draggable and remember it.
- **Card = picture + name + vendor + category chip + health dot.** One glance, no hover required. Hover
  adds version, format, path, latency, and the "why is this greyed out" reason.
- Search is the primary interaction; the grid is a fallback for browsing. `/` focuses, `Enter` loads,
  `Esc` returns focus to the DAW.
- Empty slot is a first‑class state with a call to action, not a blank panel.
- Errors are sentences: "FabFilter Pro‑Q 4 could not be loaded: this bundle only contains an arm64
  binary and your host is running x86_64." Not "Error 0x80004005".
- Never hide a plugin the user owns. If it is blacklisted, show it, marked, with a retry button.
- Keep the container's own controls (bypass, mix, latency display) visible even when the embedded editor
  fills the panel — a container whose state is invisible is a container people distrust.

## Appendix E — Test strategy and fixtures

**E.1 Hosts (the release gate).** Verify in ≥4: Ableton Live 12, REAPER, Bitwig Studio, Cubase or Studio
One. Per host, record: instantiates, loads 3 plugins, embedded editor paints, keyboard focus, plays 5
min, buffer size change, sample rate change, save/reopen, offline bounce, freeze. Any cell that fails is
a known limitation in `HOSTS.md` — never a silent gap.

**E.2 Plugin fixtures.** Generate synthetic `.vst3` bundles for the scanner (valid, JSON5‑with‑comments,
missing moduleinfo, controller‑only, legacy single file, merged bundle, corrupt JSON, symlink loop,
duplicate UID). For hosting, build two tiny SDK sample plugins (MIT with the SDK) in CI: one effect with
a GUI, one instrument — and write two deliberately hostile ones: **crash on load** and **NaN output**.

**E.3 Audio correctness.** Null tests, not screenshots: empty container vs straight wire at 44.1/48/96
kHz, mono and stereo, 64–1024 buffers, sample‑exact. Then a parallel‑path null test against a reference
delay to prove PDC works after a high‑latency plugin is added and removed.

**E.4 Realtime.** Allocation counters over 10 minutes of playback; worst‑case `process()` timing
histogram at 64‑sample buffer; verify 0 allocations and no lock waits during the run.

**E.5 Editor stress.** 50 open/close cycles per test plugin, plus resize to minimum, maximize, and a
display scale change. Log the plugin, step and crash signature when something dies; feed that straight
into the blacklist format.

**E.6 State.** Save → quit → reopen → bounce, twice, with one plugin swapped between saves. Then a
missing‑plugin variant and a UID‑drift variant (older bundle with a different CID).

**E.7 Validation.** SDK `validator` on every configuration; `pluginval` (strictness 10) if JUCE. Treat a
validator failure as a release blocker, because hosts do.

**E.8 Evidence.** `./scripts/evidence/phase-N.sh` produces the logs, the JSON and the numbers, committed
under `evidence/`. A phase without evidence is not finished.

----- END PROMPT -----

---

## Scope notes

**What "same functionality as Plugindex" can and cannot mean here.** Keep: the picture grid, automatic
categorisation from VST3 metadata, vendor snapshots, search/filter, health/duplicate reporting, a key or
click to get a plugin into the signal path. Lose, permanently: inserting a plugin as a *native DAW device
on the track*, the DAW's own per‑plugin sidechain routing and per‑plugin PDC, and DAW‑level hotkeys that
work without the plugin being focused. Replace: "put it on the track" becomes "put it inside the
container", which the DAW sees as one plugin with one latency and one set of mirrored parameters.

**Because the container is in the signal path,** every millisecond of CPU you add is a millisecond the
user pays on every track where it is instantiated. Keep the empty container at ~0% CPU, and say so in the
README with a measured number.

**Licensing.** The VST 3 SDK is MIT since 3.8 — usable in a closed commercial plugin, no fees, no
agreement. JUCE is AGPLv3 or commercial; if you build on JUCE, either publish source under AGPL or buy a
licence. VSTGUI ships inside the SDK, but check its headers in your SDK version before assuming the same
terms. "VST" is a trademark: using the word descriptively is normal, using the logo obliges you to
follow Steinberg's usage guidelines.

**You are now responsible for other vendors' code.** Hosting means loading binaries you did not build
into a process you do own. That is a security and stability posture, not just features: scan out of
process, blacklist what crashes, offer safe mode, and never auto‑load a plugin from a path the user did
not choose.

**Privacy.** The index is a list of software the user bought. It stays local, it is not telemetry, and it
is never uploaded — not even "anonymised". Say so in `PRIVACY.md`.

**Independence.** Do not use the Plugindex name, copy, screenshots or design, and do not imply
affiliation with any DAW or plugin maker. Your grid can be inspired by the idea of browsing by picture;
it should look like your product.

---

## Variants (pick one before Phase 0)

1. **Single‑slot container (recommended MVP, ~3–4 weeks).** One host slot, grid, embedded editor,
   state/safe mode, 2 hosts verified. Shippable and genuinely useful: it is a "swap the plugin without
   touching the DAW browser" tool.
2. **4‑slot serial chain.** Adds reorder, per‑slot bypass/mix, latency summing, and the real PDC work.
   This is the version people compare to MetaPlugin/PatchWork — be ready for that comparison.
3. **Instrument container.** MIDI in, note events, hosted instruments; much better for composers, but
   bus and event routing is a different (harder) problem than effect chaining.
4. **Read‑only mirroring.** Ship without automation mirroring, expose only container params, and say so.
   Adding full mirroring later is a schema‑compatible change if your parameter IDs are designed for it
   on day one.
5. **Two bundles from one codebase** (`Fx` and `Instrument`). Best long‑term, doubles the test matrix;
   only do it once the Fx container is stable.
6. **Out‑of‑process hosting (v2).** Shared‑memory audio + IPC sandbox. The correct answer for hostile
   plugins and for macOS library‑validation restrictions, and a project of its own. Design your
   interfaces so the audio path could be swapped for a proxy later — do not build it now.

---

## How to use this prompt

1. Fill `INPUTS`, choose a variant, paste §2 with Appendices A and E (A is the API map the phases cite;
   E is the release gate).
2. Insist on the two Phase 0 spikes. **Editor embedding** and **macOS library validation** decide the
   architecture, and both are cheap to test and expensive to discover late.
3. Run the SDK `validator` from the first build that makes noise. Hosts blacklist failing plugins on the
   user's machine permanently; you cannot undo that later.
4. Keep the scanner in its own process no matter how tempting the shortcut is. The index is data; the
   DAW's address space is not yours.
5. Report per phase. If a phase's evidence is "it works in my host", it is not evidence — the product is
   defined by the four hosts you verified and the cells you marked as failing.
