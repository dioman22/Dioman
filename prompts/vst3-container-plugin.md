# Prompt: Build a VST3 Plugin That Indexes and Hosts Your VST3 Library

A copy‑paste brief for an AI coding agent building **a VST3 plugin whose editor is a Plugindex‑style
picture grid of your plugin library, and which hosts the plugin you pick inside its own signal chain**.

The difference from the desktop‑app version of this idea is not cosmetic:

1. **The container is the product.** A plugin cannot put another plugin onto a DAW track — no host API
   exposes that to a plugin. What it *can* do is list the library, show it as cards, and load the chosen
   plugin **inside itself** (`IComponent` + `IAudioProcessor` + `IComponentHandler` + `IPlugView`). So the
   workflow becomes: put the container on the track once, and swap the contents with a click. No
   accessibility permissions, no keystroke automation, no version‑keyed recipes — it works in Ableton,
   Cubase, Bitwig, REAPER, FL Studio and Studio One on day one, because it is just a plugin.
2. **Audio realtime is now your problem, permanently.** An app that stutters is annoying; a plugin that
   allocates, locks or blocks on the audio thread is a broken session. Every design decision is checked
   against "what happens when this runs 512 samples at a time under a 64‑sample buffer".
3. **The DAW already did the work — so do not scan.** Every host keeps a plugin registry it built when it
   scanned: REAPER's `reaper-vstplugins64.ini`, Ableton's `Live-plugins-*.db` (SQLite, a `plugins`
   table), FL Studio's categorised `Plugin database` folder, and equivalents in Bitwig, Cubase and
   Studio One. **That registry is the library.** The plugin reads it read‑only, shows exactly what the
   host will actually load, and never enumerates the filesystem itself. You gain: no scan time, no
   architecture‑mismatch noise, categories that match the host's own. You pay for it: undocumented,
   per‑host, per‑version formats — so the reader layer is versioned, fixture‑tested, and every card says
   which source it came from.

Output is a `.vst3` bundle — no installer, no `.exe`, no browser. See [Scope notes](#scope-notes) for
what "same functionality as Plugindex" honestly means in this shape.

---

## 1. Compact version (quick triage)

> You are a senior audio‑plugin engineer (VST 3 SDK, realtime DSP, GUI embedding). Build a **VST 3 audio
> effect** that is a *plugin browser + container*: its editor is a searchable grid of the VST3 plugins
> installed on the machine — vendor snapshot images, name, vendor, category from `moduleinfo.json`
> (JSON5), arch and health badges — and clicking a card loads that plugin **inside the container**.
> **It must not scan anything.** The library is the plugin registry the host already built: REAPER's
> `reaper-vstplugins64.ini`, Ableton's `Live-plugins-*.db` (SQLite, `plugins` table), FL Studio's
> categorised `Plugin database` folder, and the equivalent caches of Bitwig, Cubase and Studio One. Read
> it read‑only, show exactly that list, and per entry do only *targeted resolution* — open that one
> bundle's `moduleinfo.json` for UID, vendor and snapshot. No directory walks, no "scan on first run". If
> a host's registry cannot be read, say so on screen and offer an opt‑in out‑of‑process fallback instead
> of silently guessing. Requirements: bit‑transparent passthrough when the slot is empty or the container
> is bypassed; correct latency and tail reporting (`getLatencySamples`/`getTailSamples`), sample‑accurate
> MIDI/parameter routing, hosted‑plugin state saved in your own state stream with class UID + path +
> version, missing‑plugin detection with a safe mode, out‑of‑process handling of crashers, and a grid that
> stays responsive while audio runs. Do not allocate, lock, log or touch the filesystem on the audio
> thread. Do not write to any DAW database. Do not invent metadata; if the registry and `moduleinfo.json`
> are both silent, the scanner probe answers out of process and `UNKNOWN` is still an acceptable answer.
> Prove every claim with the SDK `validator`, a real DAW, and measured numbers — and stop to report after
> each phase.

---

## 2. Long‑form prompt

Fill the `INPUTS` block, paste everything between the BEGIN/END markers, and include Appendix A (the
hosting‑API map the phases cite), Appendix E (the test matrix) and Appendix F (which DAW registry to read,
and how far to trust it). Ask for the SDK version and the target hosts if they are missing rather than
assuming them.

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

1. **Reads** the plugin registry the running host already built — no filesystem scan, no first‑run
   enumeration — and lists exactly the VST3 entries that host would load, enriched by a targeted
   per‑entry lookup of `moduleinfo.json`.
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
10. **Ships** as a signed/notarised `.vst3` bundle for macOS and Windows, with a versioned registry‑reader
    layer and an optional out‑of‑process fallback scanner for hosts whose registry cannot be read.

### INPUTS

```
PRODUCT_NAME:          <your name>
PLUGIN_KIND:           <Fx container (default) / Instrument container / both as two bundles>
SLOTS:                 <1 (MVP) / 4 serial / 4 serial + 4 parallel sends>
STACK:                 <JUCE 8  |  raw VST 3 SDK + VSTGUI  |  raw VST 3 SDK + native UI>
TARGET_OS:             <macOS 11+ arm64+x86_64 / Windows 10-11 x64 / both>
LIBRARY_SOURCE:        host-registry (default) — the plugin must not scan
REGISTRY_READERS:      <REAPER + Ableton first / + FL Studio / + Bitwig / + Cubase / + Studio One>
FALLBACK:              <opt-in out-of-process scan when a registry is unreadable | none>
HOSTS_TO_VERIFY:       <Ableton Live 12, REAPER, Bitwig, Cubase, Studio One — pick ≥4>
VST3_SDK_VERSION:      <e.g. 3.8.0 — confirm before coding>
PARAM_MIRRORING:       <none (MVP) / read-only display / full automation mirror>
LICENSE_MODEL:         <paid / free / open source — matters for JUCE vs SDK choice>
TELEMETRY:             none
TEST_LIBRARY:          <path to real .vst3 bundles + a real DAW install, or "none — generate fixtures">
SCOPE_DEADLINE:        <e.g. "1 slot, REAPER + Live, 4 weeks">
```

State the **stack decision and its licence consequence** in one line before Phase 0 ends: JUCE 8 is
AGPLv3 or commercial (fast, handles hosting + editor embedding), the VST 3 SDK alone is MIT since 3.8
(more work: you write the host classes and view embedding yourself). This is a business decision, not a
style preference — do not make it silently.

### NON‑GOALS (hard scope — say no early)

- **No inserting plugins into the DAW's chain.** Impossible from a plugin; do not promise it in UI copy.
- **No AU, VST2, AAX, CLAP.** VST 3 only, exactly as before.
- **No per‑DAW automation hacks, no UI automation, no accessibility permissions, no global hotkeys, no
  Stream Deck, no phone remote.** Those existed because the app was outside the DAW; a plugin does not
  need them. Deleting them is most of the win. (Registry *readers* are not DAW automation: they read a
  file the host wrote, they do not drive the host.)
- **No library scanning in v1.** No directory walk, no "scan on first run", no "just read every bundle's
  metadata". The host's registry is the list. Anything else is the fallback, and the fallback is opt‑in,
  separate‑process, and clearly labelled on every card it produces.
- **No writing to any DAW database.** Read‑only opens, copy‑on‑lock, no `UPDATE`, no `VACUUM`, no
  `PRAGMA` that mutates, no repair, no "clean up dead entries". If you corrupt Live's database, you have
  broken the user's DAW — not just your plugin.
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
- **R2 — The host's registry is the source of truth, and you only read it.** Enumerate nothing: take the
  list the host already built. Resolve one entry at a time (a handful of candidate paths at most) to get
  its bundle path, then read *that one bundle's* `moduleinfo.json` for UID, vendor, category and
  snapshot. Read‑share, copy‑on‑lock, never write. Every card records its `source`
  (`reaper-ini` / `live-db` / `fl-db` / `scanner-fallback`) so the UI cannot lie about where the list
  came from. Loading foreign code happens only on an explicit user action, on the message thread, with
  a progress state.
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
- **R8 — No metadata invention.** Name/vendor/category/UID come from the host's registry where it
  provides them, then from that bundle's `moduleinfo.json`, then from an out‑of‑process `PClassInfo2`
  probe. `UNKNOWN` is a valid value; guessing is not.
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
5. Write the **registry‑reader contract**: `detect() → Reader`, `list() → [Entry]`,
   `resolve(entry) → Path | Ambiguous | Missing`, `state() → ok|unreadable|locked|unsupported_version`.
   The plugin owns no plugin list of its own; it owns a snapshot of the reader output plus the source
   label. Decide which reader answers when several DAWs are installed — it is the one belonging to the
   host you are running inside, and `IHostApplication::getName()` is how you know which that is.
6. Report: the two decisions, the two spike results, and the one thing you are least confident about.

**Stop and report.**

### PHASE 1 — Host registry readers (no scanning)

The library is not discovered, it is *read*. Build the reader layer first; the grid is only a view of it.

1. **Identify the host.** `IHostApplication::getName(String128&)` gives you the host name at runtime
   ("Ableton Live", "REAPER", "Cubase", …). For the version, read your own process's executable path
   (`GetModuleFileName(NULL)` / `_NSGetExecutablePath`) and its version resource / `Info.plist` — that is
   how you choose a reader variant when a DAW changes its format between releases.
2. **One reader per host, versioned, with fixtures.** Start with the two whose registries are documented
   and text/relational (REAPER, Ableton), add FL Studio (categorised folder), then Bitwig, Cubase, Studio
   One — see Appendix F for paths, formats and evidence tiers. A reader returns `Entry` records, never
   paths to plugins it has not been told about.
3. **Read‑only, always.** Never `UPDATE`/`VACUUM`/`PRAGMA` a DAW database. Open SQLite with
   `mode=ro` + `query_only`, handle `SQLITE_BUSY` with bounded retries, and when the host holds an
   exclusive lock, **copy the file (plus `-wal`/`-shm`) to temp and read the copy**. For text caches,
   open with read‑share flags and tolerate a partially written last line: a registry caught mid‑rewrite
   is a normal condition, not an error.
4. **Staleness.** Compare the registry's mtime with the host process start time and with your own launch
   time. If the file changed after you read it, reload on the message thread. Never trigger a host
   rescan, never delete a cache entry, never "fix" a registry.
5. **Targeted resolution (the only filesystem work you are allowed).** An `Entry` usually carries a
   display name and sometimes a path:
   - path present and exists → use it;
   - otherwise list the **immediate children** of the host's configured plugin folders and the format's
     standard folders (one level, no recursion), build a filename→path map, and match by name;
   - then open exactly that bundle's `moduleinfo.json` for UID, vendor, version, sub‑categories and
     snapshot;
   - nothing found → `unresolved`: show the card with the registry's own name and an honest reason, and
     disable loading. Do not fall back to guessing.
6. **Lazy enrichment.** Parse `moduleinfo.json` for visible cards only, cached by path+mtime. A 900‑entry
   registry must open the editor in under a second because you read ~40 bundles, not 900.
7. **Show the source.** Every card and the header display which registry it came from and whether it is
   current ("REAPER cache · 2 min ago", "Live database · locked, read from copy"). If the reader fails,
   the UI says which check failed — never an empty grid with no explanation.
8. **Fallback, explicitly opt‑in.** When a host's registry is absent or unreadable (`FALLBACK`), offer a
   separate‑process scan that produces the same `Entry` shape with `source: scanner-fallback`, and warn
   that it can list plugins the host itself rejects. It must never run automatically, and it must never
   run in the host process.
9. **Registry fixtures in CI.** Commit sanitised copies of real registries per DAW version (REAPER ini,
   Live SQLite, FL folder listing) and assert the parsed `Entry` list byte‑for‑byte. When a DAW update
   changes a format, this test tells you before a user does.
10. Report: which readers work on your machine, the entry count from each registry vs the count the DAW's
    own browser shows (they must match), and every entry you could not resolve with the reason.

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
6. **Refresh affordance.** A "reload from <host> registry" button that re‑reads the registry (and shows the
   timestamp it was written), plus a separate, clearly secondary "scan anyway (fallback)" action for when
   the registry is unreadable. Both off the audio thread, neither blocking the editor.
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

1. **Crash isolation where you can get it.** Reading the host's registry executes nothing, so the only
   crash surface is loading plugin code — the host's job and yours. In‑process hosting remains that
   surface, so the plugin must record **which** plugin was being loaded/opened
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
2. **Ship the fallback scanner only if you ship a fallback.** If you do, it lives beside the plugin (a
   small helper, or a CLI in `Contents/Resources/`). Respect the rule that `Contents/Resources` is
   read‑only and signed — copy out before executing if the platform requires it, and hash‑verify the
   copy. If you choose `FALLBACK: none`, say so in the docs and let the grid explain a missing registry
   instead of guessing.
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

1. **Parity with the host.** For each supported DAW, the entry count in the grid equals the count in that
   DAW's own plugin browser, and a spot‑check of 20 names matches exactly. Report the two numbers side by
   side; a mismatch is a failing build.
2. **No enumeration.** With the plugin editor opened 20 times in a DAW, a file‑access trace shows reads of
   the registry plus at most the visible cards' `moduleinfo.json` — **zero directory walks** of the VST3
   folders, zero reads of bundles that were not displayed. Attach the trace.
3. **Read‑only.** Checksums of the DAW's registry files (and `-wal`/`-shm`) are identical before and after
   a full session of opening the grid, searching, loading plugins and reloading. No `UPDATE`, no `VACUUM`,
   no mtime change caused by your writes.
4. Empty container and fully‑bypassed container are **bit‑identical** to input at 44.1/48/96 kHz, mono
   and stereo, 64–1024 sample buffers (null test, sample‑exact, in a real host).
5. Zero heap allocations and zero lock acquisitions inside `process()` — proven with allocation counters
   over a 10‑minute run, not by inspection.
6. `getLatencySamples()` matches the sum of hosted latencies after every slot change, and the host's PDC
   stays aligned (null test through a parallel path against a reference).
7. Session round‑trip: save → quit DAW → reopen → audio identical, hosted editors reopen with correct knob
   positions, patch names intact.
8. Missing‑plugin case: a session referencing an absent plugin opens with a named placeholder, produces
   silence on that slot (not noise, not a drop of the whole chain), and offers "locate"/"remove".
9. Safe mode opens a session that would otherwise crash, with all slots disabled and clearly labelled.
10. Loading a plugin that crashes the host leaves a dirty journal; next launch names the suspect and offers
    blacklisting; blacklisted plugins are visibly marked in the grid, not hidden.
11. Hosted editor: paints, resizes, takes keyboard focus, survives 50 open/close cycles, on macOS and
    Windows, for 10 real plugins (table of results).
12. Grid with the host's full registry: first paint <300 ms, scroll smooth **during playback**, memory
    ≤200 MB, and the audio thread's worst‑case `process()` time does not regress with the editor open.
13. Locked/partial registry: with the DAW holding an exclusive lock (or the cache truncated mid‑write),
    the grid still opens, labels the state (`locked`, `read from copy`), and never crashes or shows a
    half‑parsed list as if it were complete.
14. Unresolvable entry: a registry entry whose bundle was deleted shows as `unresolved` with the host's own
    name, is searchable, and refuses to load with a readable reason.
15. SDK `validator` and `pluginval` (strictness 10) pass; no host blacklists the plugin.
16. Verified working in ≥4 hosts with a filled‑in matrix, including offline bounce and buffer‑size change
    during playback.
17. State schema migration: a state saved by the previous released version loads correctly, or is refused
    with a clear message — never silently misread.

### THINGS THAT WILL MAKE YOU WRONG (read twice)

- **You cannot insert into the DAW's chain.** If the UI implies otherwise, the product is lying and the
  reviews will say so. Say "loads inside the container" everywhere.
- **"The host's list" is not "the VST3 files on disk".** A registry can contain VST2, AU, AAX, disabled,
  blacklisted and 32‑bit entries alongside the ones you can host. Filter by what you can actually load,
  but show the filter and the count ("312 of 604 entries are VST3") — silently dropping half the list
  makes the product look broken.
- **Pick the registry of the host you are inside.** `IHostApplication::getName()` decides; never "the
  newest database on the machine", never a merged list from all DAWs. Two DAWs, two truths.
- **Display names differ between host and bundle.** Live/FL may call it something the vendor does not.
  Match by path first, by name second, and never merge two entries into one card because they look alike.
- **A registry entry can outlive its bundle.** Installed, scanned, then uninstalled — the entry stays.
  That is `unresolved`, not a crash and not a reason to hide the card.
- **SQLite is not a file you can read casually.** WAL, `-shm`, exclusive locks mid‑write. `mode=ro` +
  `query_only`, bounded retries, copy‑then‑read when busy, and never hold a transaction open while the
  DAW runs.
- **`enabled`/`scanstate` are the host's opinion, not yours.** A disabled plugin is still a plugin the
  user owns; show it as disabled and let them fix it in the DAW.
- **Do not "helpfully" fall back to scanning.** If the registry is unreadable, say so. A silent scan that
  lists plugins the host rejects teaches the user the wrong thing about their own DAW.
- **Scanning from inside the plugin will eventually kill a session.** Even "just reading metadata" via
  `GetPluginFactory` executes foreign code. If you must probe, do it out of process.
- **Registry data is stale the moment a plugin is installed.** The DAW rescans on its own schedule. Show
  the registry's timestamp so "I installed it, why isn't it here?" has an answer on screen.
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
- **The registry is data, not truth.** A plugin can be deleted, moved, updated or replaced between the
  host's scan and your load. Check existence and UID at load time and report the difference instead of
  failing obscurely.
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

### A.8 Where the list comes from: registry first, scanner only as fallback

The plugin shows what the host already knows. Concretely:

1. On editor open (message thread), ask the reader for the host you are running inside
   (`IHostApplication::getName()`) for its `Entry` list. The reader opens the host's cache read‑only
   (SQLite `mode=ro`/`query_only`, or shared‑read with tolerance for a half‑written last line) and
   returns entries. **No directory walk, no bundle parsing at this stage.**
2. Resolve lazily, per visible card: take the entry's path if it has one, else match its name against the
   immediate children of the host's plugin folders, then read that one bundle's `moduleinfo.json` for
   UID/vendor/category/snapshot. Cache by `(path, mtime)`.
3. If the registry is missing, locked beyond recovery, or from an unknown version, surface that state and
   leave the grid empty *with an explanation*. The fallback scanner is a separate, explicitly triggered
   action that writes a cache in the same `Entry` shape — it never runs on load and never runs in the host
   process.
4. Nothing in `process()` touches the filesystem, the registry, the cache or the reader. Ever. The audio
   path sees only the already‑resolved slot state.
5. Re‑read the registry on every editor open and on explicit reload; a cache that survives a DAW rescan is
   a cache that lies. Compare mtime, not your own timestamps.

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

- Hosts build their own registry by scanning the standard folders (macOS
  `~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3`; Windows
  `%ProgramFiles%\Common Files\VST3`). Tell users where to copy your bundle, and make your docs match
  reality — "it didn't appear" is the most common first‑run ticket and is almost always a wrong folder
  or an architecture mismatch (and, for you, a registry that has not been rescanned since).
- Generate `moduleinfo.json` for your own plugin: correct `Category: "Audio Module Class"`,
  `Sub Categories: ["Fx", "Tools"]` (or `["Instrument"]`), `Cardinality`, and a `Snapshot` image so you
  look right inside other people's browsers.
- If you ship the scanner in `Contents/Resources/`, remember it is inside a **signed** bundle: copying it
  out before executing is often required, and the copy must be hash‑verified against the bundled file.
- Keep the plugin small (no sample content). A container that is 200 MB because of UI assets is a
  container people uninstall.

## Appendix C — The `Entry` model (registry snapshot)

Every reader produces the same shape, whether the source is a text cache, a SQLite table, a folder of
`.fst` files or the fallback scanner. The plugin holds a snapshot of these plus the registry state; there
is no authoritative index file of your own.

```jsonc
{
  "registry": {
    "host": "Ableton Live",            // from IHostApplication::getName()
    "host_version": "12.1.5",          // from your own process image's version resource
    "reader": "live-db@1",             // reader id + format version you parsed
    "source": "live-plugins-1.db",
    "state": "ok",                     // ok|unreadable|locked|not_found|unsupported_version
    "written_at": 1770000000,          // file mtime, shown to the user
    "read_from_copy": false            // true when the host held a lock
  },
  "entries": [
    {
      "uid": "BD58B550F9E5634E9D2EFF39EA0927B1",  // null until resolved from moduleinfo.json
      "name": "Pro-Q 4",                           // host's own display name, always present
      "vendor": "FabFilter",
      "version": "4.01",
      "sdk": "VST 3.7.5",
      "category": "EQ",                            // canonical, mapped from sub-categories
      "sub_categories": ["Fx", "Fx|EQ"],
      "is_instrument": false,
      "capabilities": ["stereo", "only_rt"],       // PlugType flags, not categories
      "bundle": "C:\\Program Files\\Common Files\\VST3\\FabFilter Pro-Q 4.vst3",
      "arch": ["x86_64-win"],
      "image": "images/BD58B5….webp",              // cached from the vendor snapshot
      "image_source": "moduleinfo",                // moduleinfo|probed|generated
      "resolution": "exact",                       // exact|by_name|unresolved|ambiguous
      "host_enabled": true,                        // the DAW's own enabled/disabled flag, if it has one
      "host_scanstate": 3,                         // whatever the registry says; do not reinterpret it
      "status": "ok",                              // ok|unresolved|unreadable|license_required|blacklisted
      "status_detail": null
    }
  ]
}
```

Design rules:

- `name` comes from the **host** — that is what the user sees in their own browser, and matching it is
  what makes the grid feel correct. `uid`/`vendor`/`category` are enrichment from the bundle.
- `uid` may be `null`. Render the card from registry data and resolve lazily; a card without a UID can be
  shown and searched but not loaded.
- `resolution` explains why a card cannot load, in one word you can translate into a sentence.
- `host_enabled` / `host_scanstate` are **the host's opinion**. Show them; never "correct" them, and
  never treat a disabled entry as absent — users disable plugins deliberately.
- Cache the enriched entries on disk if you like (path+mtime keyed), but the registry is re‑read on every
  editor open. A cache that outlives a DAW rescan is a cache that lies.

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

**E.2 Registry fixtures.** Commit sanitised snapshots of real DAW registries per version — a REAPER
`reaper-vstplugins64.ini`, a Ableton `Live-plugins-*.db`, an FL `Plugin database` folder listing (names
only, no binaries) — and assert the parsed `Entry` list exactly. Add hostile variants: truncated last
line, half‑written SQLite (copy mid‑WAL), locked database, unicode plugin names, an entry whose bundle is
gone, two entries resolving to the same bundle, a registry from a newer DAW version than you support. For
hosting, build two tiny SDK sample plugins (MIT with the SDK) in CI — one effect with a GUI, one
instrument — plus two deliberately hostile ones: **crash on load** and **NaN output**.

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


## Appendix F — DAW plugin registries (what to read, and how much to trust it)

The plugin shows what the host already knows. These are the concrete sources; **tiers matter**, and a
reader may only be shipped for a format you have personally seen on disk.

| Tier | Meaning |
|---|---|
| **A** | Verified: format observed on a real install / primary documentation. Safe to build and fixture‑test. |
| **B** | Documented but not guaranteed: build it, fixture‑test it, expect drift. |
| **C** | Unknown: do not write a reader from this table. Discover it (method below), record it, then promote it. |

### REAPER — tier A/B

- File: `reaper-vstplugins64.ini` (and the 32‑bit variant) in the REAPER resource path —
  `~/Library/Application Support/REAPER/` on macOS, `%APPDATA%\REAPER\` on Windows, or next to the
  executable in a portable install.
- Shape: an INI with a `[vstcache]` section, one line per plugin, keyed by **filename**:

  ```
  [vstcache]
  reacomp.dll=00F762C30558D301,1919247213,ReaComp (Cockos)
  MT_PowerDrumKit.dll=007A4E3F22A7D201,1297371204,MT-PowerDrumKit (MANDA AUDIO) (16 out)!!!VSTi
  ```

  Three comma‑separated values after `=`: a hex id, a second numeric field, and the **display name** —
  with a trailing `!!!VSTi` marking instruments and occasional `(8ch)` / `(16 out)` suffixes to strip.
- Caveat to verify on your build: the sample above is a VST2 cache (`.dll`, `.vst.dylib`). Confirm whether
  VST3 entries land in the same cache or a sibling file on the REAPER version you test, and whether the
  key is a bare filename (it usually is) — if so, resolve by filename against REAPER's configured VST
  paths, which are themselves in REAPER's settings.
- Never write to this file: REAPER rewrites it on rescan, and a bad edit produces "my plugins disappeared".

### Ableton Live — tier A

- Files: `Live-plugins-1.db` (SQLite) in the Live Database folder —
  `~/Library/Application Support/Ableton/Live Database/` on macOS,
  `%LocalAppData%\Ableton\Live Database\` on Windows. (Live's separate file index is
  `Live-files-<build>.db`, e.g. `Live-files-1218.db`, and is not the plugin list.)
- Schema observed on 12.x: a `plugins` table with `plugin_id`, `module_id`, `dev_identifier`, `name`,
  `vendor`, `version`, `sdk_version`, `flags`, `scanstate`, `enabled`, plus a `modules` table.
  That is name + vendor + version + SDK version + the host's own enabled flag — enough to render cards
  without touching a single bundle.
- Live keeps the database open while running. Use `mode=ro` + `query_only`, retry on `SQLITE_BUSY`, and
  **copy the `.db` with `-wal`/`-shm` to temp** when the lock is exclusive. Verify the filename and schema
  per Live version; treat any mismatch as `unsupported_version` rather than guessing columns.

### FL Studio — tier B

- Location: the user Plugin database at `Documents\Image-Line\FL Studio\Presets\Plugin database`
  (Windows) / `~/Documents/Image-Line/FL Studio/Presets/Plugin database` (macOS). The install ships
  default structures under `…\FL Studio\Data\System\Plugin databases` (inside the app bundle on macOS).
- Structure: subfolders **are** the categories (Effects, Generators, and the sorting variants), and each
  plugin appears as an `.fst` file. That gives you the host's own categorisation for free — a genuine
  advantage over deriving categories from sub‑categories.
- `.fst` is a binary FL format. Treat the **filename as the display name** and the **folder as the
  category**; do not attempt to parse `.fst` for vendor/UID. Resolve the bundle by name.

### Bitwig Studio, Cubase/Nuendo, Studio One — tier C (discover first)

Do not guess. Discover, using the tracing method, then write the reader and a fixture:

1. **Windows:** Process Monitor, filter to the DAW process, include `Write`/`CreateFile`/`SetEndOfFile`,
   then trigger a plugin rescan in the DAW. Whatever it writes in the app‑data tree is the registry.
2. **macOS:** `sudo fs_usage -w -f filesys | grep -i <daw>` while forcing a rescan, then look in
   `~/Library/Application Support/<Vendor>/`, `~/Library/Preferences/`, and containers.
3. Cross‑check by snapshotting the candidate directories before/after a rescan and diffing
   (`find … -newer`), then open anything that changed: SQLite (`file`, `sqlite3 .schema`), JSON, plist,
   XML.
4. Record path + format + a sanitised copy + the DAW version in `REGISTRIES.md` and as a test fixture.
   Only then does the reader ship, and it reports `unsupported_version` for anything it has not seen.

### Rules that apply to every reader

- **Read‑only. No exceptions.** No `UPDATE`, no `VACUUM`, no repair, no cleanup, no "mark as rescanned".
  If you corrupt a DAW's database you have broken their software, not just yours.
- **Never trigger a host rescan.** That is the DAW's job, and doing it behind the user's back is how you
  get a support ticket that starts with "why did my plugins rescan mid‑session".
- **Assume the file is being written right now.** Partial lines, locked databases, zero‑length files and
  a `.db` newer than its `-wal` are all normal. Retry, copy, or show `locked` — never crash, never cache
  the half‑read state.
- **Count parity is the test.** Your grid's entry count for a DAW must equal what that DAW's own browser
  shows, including entries you cannot resolve. Any difference is a reader bug, not a cosmetic issue.
- **Label the source on screen.** "REAPER cache · written 14:02" or "Live database · locked (read from
  copy)" — the user must be able to tell why a plugin they just installed is missing from your grid.
- **Version your readers and fail loudly.** `unsupported_version` with the DAW version printed is a
  helpful message; silently showing an empty grid is a bug report you will get twice.

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
into a process you do own. That is a security and stability posture, not just features: read the host's
registry read‑only, keep any probe out of process, blacklist what crashes, offer safe mode, and never
auto‑load a plugin from a path the user did not choose.

**Privacy.** The registry and any cache you build from it are a list of software the person bought. They
stay local, they are not telemetry, and they are never uploaded — not even "anonymised". Say so in
`PRIVACY.md`.

**Independence.** Do not use the Plugindex name, copy, screenshots or design, and do not imply
affiliation with any DAW or plugin maker. Your grid can be inspired by the idea of browsing by picture;
it should look like your product.

---

## Variants (pick one before Phase 0)

1. **Single‑slot container (recommended MVP, ~3–4 weeks).** One host slot, grid, embedded editor,
   state/safe mode, **two registry readers (REAPER + Ableton)** verified against their real browsers.
   Shippable and genuinely useful: it is a "swap the plugin without touching the DAW browser" tool.
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
7. **One DAW, done perfectly.** Ship only REAPER (its registry is a plain text file — no SQLite, no
   locks) or only Ableton (one SQLite table). Smallest QA matrix, highest reliability, and a real product
   for that community. The best first release if you are working alone.

---

## How to use this prompt

1. Fill `INPUTS`, choose a variant, paste §2 with Appendices A, E and F (A is the API map, E is the
   release gate, F is which DAW registry to read and how much to trust it).
2. Insist on the two Phase 0 spikes. **Editor embedding** and **macOS library validation** decide the
   architecture, and both are cheap to test and expensive to discover late.
3. Run the SDK `validator` from the first build that makes noise. Hosts blacklist failing plugins on the
   user's machine permanently; you cannot undo that later.
4. Never write to a DAW's database, and never trigger its rescan. If a phase's design needs either, the
   design is wrong. Any probing of foreign plugin code stays in a separate process.
5. Do not add a reader for a DAW whose cache you have not opened yourself. "It should be somewhere in
   Application Support" is how a plugin ships an empty grid to half its users.
6. Report per phase. If a phase's evidence is "it works in my host", it is not evidence — the product is
   defined by the four hosts you verified and the cells you marked as failing.
