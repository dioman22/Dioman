# Prompt: Build a VST 3‑Only Plugin Library Manager (a "Plugindex, but VST3" app)

A copy‑paste brief for an AI coding agent that has to **design, build and ship a desktop app which
indexes only `.vst3` bundles, shows them as a visual grid, and inserts the one you pick into the
DAW's selected track with a click or a single key** — plus the library‑health, hotkey, remote and
project‑audit features that make such a tool worth keeping installed.

This is a *product* prompt, not a snippet request. It is written around three convictions, and the
phases exist to enforce them:

1. **VST3‑only is a scope advantage, not a limitation.** One bundle layout, one metadata file
   (`moduleinfo.json`), stable 16‑byte class UIDs that survive across formats and project files, and
   an SDK that has been MIT‑licensed since VST 3.8. Nothing here needs AU, VST2, AAX or CLAP
   heuristics. Say what you lose up front — Logic Pro loads AU only and Pro Tools loads AAX only, so
   a VST3‑only tool cannot serve them — and spend the saved effort on depth.
2. **Indexing and hosting are different jobs with different risk.** Reading metadata is static and
   safe. Loading a plugin executes a third party's code, which will crash, hang, or demand a dongle.
   So plugin hosting lives in a **separate short‑lived helper process: one plugin per process, hard
   timeouts, kill on hang**. That is an architectural constraint, not a bug fix.
3. **Every DAW claim is an adapter with declared capabilities and a `doctor` command.** Only REAPER
   exposes a real "insert this plugin" API. Most other routes are UI automation, and UI automation
   that types blind into someone's session is a data‑loss bug waiting to happen — so "refuse to act
   when the expected window is not focused" becomes a *tested* requirement, not a comment.

Legal scope is part of the prompt, not a footnote — see [Scope notes](#scope-notes). Build your own
product with your own name and design; "same functionality" is fine, copying Plugindex's name, copy,
screenshots or assets is not.

---

## 1. Compact version (quick triage)

> You are a senior DAW‑integration and desktop‑systems engineer. Build a **VST 3‑only** plugin
> library manager: a standalone app (not a plugin, never in the signal path) that scans the standard
> `.vst3` locations on macOS/Windows, reads each bundle's `moduleinfo.json` (JSON5 — not strict JSON)
> for class name, vendor, version, category, sub‑categories, class UID and vendor snapshot images,
> and presents the library as a searchable grid of cards. It must then **insert a chosen plugin into
> the DAW's currently selected track** from a click or a global hotkey, through a per‑DAW adapter:
> REAPER via ReaScript `TrackFX_AddByName` (native, no permissions), everything else via
> accessibility/UI automation with strict focus verification and refusal on mismatch. Also deliver:
> auto‑categorization from VST3 sub‑categories with persistent user overrides, duplicate and health
> reporting, out‑of‑process load testing and CPU measurement, per‑DAW key bindings with conflict
> reporting, a localhost remote (PWA + Stream Deck + OSC), and a session audit that reads
> `.rpp`/`.als`/`.bwproject` files to report which plugins a project needs and which are missing or
> superseded. Never load a plugin in the UI process; never write into a vendor's bundle; never invent
> metadata — if `moduleinfo.json` is absent, probe `GetPluginFactory`/`PClassInfo2` in the helper and
> mark the bundle `unreadable` if that fails. Work in the phases below, stop and report after each,
> and prove every acceptance criterion with command output. Ask for missing inputs instead of
> assuming them.

---

## 2. Long‑form prompt

Fill the `INPUTS` block, then paste everything between the BEGIN/END markers. **Paste Appendices A–F
with it** — the phases cite `§A.3` (moduleinfo schema), `§A.4` (UIDs and `Compatibility`), `§A.5`
(sub‑category strings), `§A.6` (architecture matrix) and `§F` (turning the result into a real
double‑clickable Windows `.exe`), and those sections exist so the agent does not reconstruct them
wrongly from memory. Attach nothing else unless you have real bundles to test against; the agent
generates its own fixtures (§E).

----- BEGIN PROMPT -----

### ROLE

You are acting as one engineer wearing three hats:

- **Desktop systems programmer** — macOS bundles, codesigning and TCC; Windows PE and folder‑bundles;
  process isolation; scanning the filesystem at scale without blocking anything.
- **DAW integration engineer** — ReaScript; per‑DAW accessibility/UIA automation; focus and modal
  management; global hotkey registration; the discipline of "verify before you type".
- **Product and front‑end engineer** — a fast, keyboard‑first library UI, honest empty/error states,
  and a data model that survives rescans and reinstalls.

You are not writing a demo. The user is a working producer whose session is the thing at risk.

### OBJECTIVE

Ship an application that does all of the following, verifiably:

1. **Index** every `.vst3` bundle it can find in the standard locations, without executing plugin
   code for metadata that already exists on disk.
2. **Present** the library as a visual grid — vendor snapshot image, name, vendor, category, arch
   badges, size, measured CPU, health dot — searchable and filterable the way a producer actually
   thinks: "the FabFilter ones", "the reverbs", "the ones that load in Bitwig".
3. **Insert** the chosen plugin into the DAW's selected track, in the next free slot, from a click
   or a global hotkey, and optionally bring its editor up.
4. **Categorize** automatically from VST3 sub‑categories, with user overrides that survive rescans,
   renames and reinstalls.
5. **Keep the library healthy** — duplicates, won't‑load, arch mismatches, legacy single‑file
   bundles, total footprint, and a periodic re‑check.
6. **Measure** real cost per plugin (one core, 48 kHz, 256‑sample buffer) in an isolated helper, and
   label the machine it was measured on.
7. **Map keys** to plugins per DAW, with conflict warnings against other bindings and the DAW's own
   shortcuts, plus an honest badge for "works in every DAW" vs "only in this one".
8. **Audit sessions** — read project files, list the VST3 classes they require, flag missing ones and
   ones superseded via the module's `Compatibility` declarations.
9. **Extend reach** — a localhost remote (phone PWA), a Stream Deck plugin and OSC, all bound to
   127.0.0.1 behind a pairing token, with no account and no cloud.
10. **Stay out of the way** — not a plugin, no idle CPU, nothing in the signal path, no telemetry, and
    a documented uninstall that leaves no residue in DAW folders.

### INPUTS

```
PRODUCT_NAME:        <your name — do not use "Plugindex">
LICENSE_MODEL:       <paid-once / freemium / open source>
TARGET_OS:           <macOS 13+ / Windows 10-11 x64 / both / + Linux>
MVP_DAW:             <REAPER recommended — the only one with a real insert API>
SECOND_DAW:          <Ableton Live 11/12 recommended — largest VST3 user base>
ALSO_SUPPORT:        <Bitwig / Cubase-Nuendo / FL Studio / Studio One-Studio Pro / none yet>
HOST_STACK:          <default: Tauri 2 (Rust core) + TypeScript UI + SQLite>
UI_FRAMEWORK:        <default: React + Vite with a virtualized grid>
PACKAGING:           <notarized dmg / signed MSI or NSIS / both>
DELIVERY:            <portable single-file .exe / Setup.exe + MSI / both>   # see Appendix F
TELEMETRY:           none
TEST_LIBRARY:        <path to real .vst3 folders you may scan, or "none — generate fixtures">
SCOPE_DEADLINE:      <e.g. "REAPER + Ableton only, 3 weeks">
```

If a field is blank, propose a default in one line and continue — except `PRODUCT_NAME` and
`TARGET_OS`, which change packaging and permissions and which you must confirm before Phase 9.

### NON‑GOALS (hard scope — decline politely if asked later)

- **No AU, VST2, AAX, CLAP or LV2 scanning or hosting.** Out of the data model, not "later, maybe".
- **No Logic Pro, no Pro Tools integration.** Logic loads AU only; Pro Tools loads AAX only. A
  VST3‑only tool cannot serve them, and implying otherwise is the fastest way to lose trust.
- **No stock DAW devices, Max for Live devices, racks or AU‑only presets** in the index. "Everything
  I can load" is a different product.
- **No plugin in the signal path.** No VST3 wrapper, no insert shim, no audio‑thread code.
- **No cloud account, no sync, no telemetry in v1.** The plugin index is sensitive local data.
- **No analysing or defeating plugin licensing, dongles or trials.** If a plugin demands a license in
  the helper, record `license_required` and move on.

### OPERATING RULES (non‑negotiable)

- **R1 — Source of truth hierarchy.** `moduleinfo.json` → in‑helper `GetPluginFactory` +
  `PClassInfo2` → nothing. Never derive vendor, version or UID from a filename; filenames are display
  hints at most.
- **R2 — Never load a plugin in the UI process.** Every `LoadLibrary`/`dlopen` happens in the host
  helper (Phase 3), one plugin per process, with hard timeouts and kill‑on‑hang.
- **R3 — Read‑only on vendor bundles.** No writes, renames or "fix‑ups" inside a `.vst3`. Prove it
  with an FSEvents/`ReadDirectoryChangesW` watcher during a full scan: mtimes must not change.
- **R4 — The database is a cache, not the truth.** Disk is the truth. Ship `verify` (rebuild from
  disk, report drift) and a `--rebuild` that is always safe to run.
- **R5 — Never invent.** No UID, vendor, category or "it probably works". Write `UNKNOWN` and name
  the command or experiment that would resolve it.
- **R6 — Every DAW capability is declared.** Adapters expose `capabilities()` and `doctor()`. If
  insert is unimplemented, the UI says "not supported in <DAW>" — never a silent no‑op.
- **R7 — Verify focus before synthesizing input.** UI automation confirms target window, focused
  element and absence of modals before one keystroke is sent; if a check fails it aborts, explains,
  and sends nothing. This is a test, not a comment.
- **R8 — Fail open on licensing.** Any license check tolerates ≥60 days offline and never locks a
  user out of a library they can see on disk.
- **R9 — UIDs are 16 bytes.** Store as 32‑char uppercase hex; never truncate, renumber, or compare
  case‑insensitively. Preserve byte order when matching project files (§A.4).
- **R10 — Independent product.** No Plugindex or DAW name, logo, copy, screenshot or visual design.
  Nominative references only, with a "not affiliated" disclaimer.

---

### PHASE 0 — Decisions and contracts (half a day, before feature code)

Produce `SCOPE.md` and the adapter interface. Nothing else.

1. Write the **DAW support matrix** (start from §B, correct it against the installed builds) with one
   row per DAW: VST3 support, insert route, permissions required, and what is *verified* vs *best
   effort* vs *unsupported* — plus what the user sees when it is unsupported.
2. Write the **adapter contract in code, not prose**:

   ```rust
   trait DawAdapter {
       fn id(&self) -> &'static str;                  // "reaper"
       fn detect(&self) -> Detection;                 // running? version? pid? window?
       fn capabilities(&self) -> Capabilities;        // insert, insert_new_track, open_ui, presets, transport
       fn insert(&self, req: InsertRequest) -> Result<InsertOutcome, InsertError>;
       fn doctor(&self) -> DoctorReport;              // permissions, focus, verified-on-this-build
   }
   ```

   `InsertRequest` carries the **bundle path, the class UID, and the display name/vendor**, because
   adapters differ in what they can match on. `InsertError` is an enum — `NotRunning`,
   `NoSelectedTrack`, `PermissionDenied`, `FocusMismatch`, `ModalOpen`, `PluginNotInHost`, `Timeout`,
   `Unsupported`, `Unknown` — a stringly‑typed error here poisons every bug report later.
3. Write the **IPC schema** between UI, scanner and host helper: JSON‑RPC over a Unix socket
   (macOS/Linux) or named pipe (Windows), with request ids, per‑call deadlines and a streaming
   progress channel for scans. One protocol version field, validated on both ends.
4. Write the **fixture plan** (§E) so Phases 1–3 can prove themselves from day one.
5. Report: the matrix, the interface, and the one decision you are least sure about.

**Stop and report.**

### PHASE 1 — Scanner (static; no plugin code executed)

Deliver `vst3idx`, a CLI usable without the UI, plus tests.

1. **Enumerate roots** exactly per §A.2 — user, then global, then application‑local — applying the
   VST3 rule that **the first bundle found for a given processor UID wins**. Later ones are recorded
   as `shadowed`, never dropped: the user needs to see a duplicate in order to clean it up.
2. Handle the three shapes on disk: standard folder bundle, **merged bundle** (several arch folders
   in one `.vst3`), and **legacy single‑file `.vst3`** (a DLL/`.so`/Mach‑O with no `Contents/`).
   Symlinks are explicitly legal in this format — follow them, with cycle detection and a depth cap.
3. **Parse `moduleinfo.json` with a JSON5 parser.** The file is JSON5: comments, trailing commas and
   unquoted keys are legal and occur in the wild, so `JSON.parse` fails on real bundles. Cache parse
   results by `(path, size, mtime, inode/file‑id)`.
4. **Class selection.** Keep `Category == "Audio Module Class"` entries as plugins; store
   `Component Controller Class` entries as linked records, never as cards. Compute `is_instrument`
   from sub‑categories (`Instrument`, `Instrument|*`, `Fx|Instrument`) — an Audio Module Class is not
   automatically an instrument.
5. **Fallback when `moduleinfo.json` is absent** (common on pre‑3.7.8 builds): probe in the helper —
   `GetPluginFactory`, then `IPluginFactory::getClassInfo(i, PClassInfo2)` for name, vendor, version,
   sub‑categories and class flags; `IComponent::getControllerClassId` if you need the pairing. If the
   helper cannot, index the bundle with `status = unreadable` and show it greyed with a reason.
6. **Images.** Prefer `Snapshots[]` from `moduleinfo.json` (respect `Scale Factor` 1.0 and 2.0×;
   paths are bundle‑relative). When absent, render one in the helper offscreen (§A.7). When that
   fails, use a deterministic generated tile — vendor‑hashed colour plus wordmark — never a broken
   image icon.
7. **Fingerprint** each bundle so rescans are cheap: primary‑binary SHA‑256 per arch, arch list,
   moduleinfo hash, total size, codesign/notarization state (macOS), file ids. A warm rescan of an
   unchanged library must spawn **zero** helper processes.
8. Stream scan progress (root, bundles seen, parsed, failed) so the UI can show something true within
   200 ms of launch.
9. Report: bundles found per root, parse failures with reasons, warm/cold timings, and the three
   weirdest bundles you met.

**Stop and report.**

### PHASE 2 — Data model and categorization

1. Implement the schema in §C.1 (SQLite, WAL, migrations from day one). Note the deliberate
   normalisation: `classes.uid` is identity, `bundles.path` is location, and `(uid, bundle_id)` is
   what a "duplicate" actually is.
2. **Duplicates**, in order of confidence: same `uid` in two bundles → `duplicate_uid`; same primary
   binary hash → `duplicate_binary`; same `(vendor, name, version)` with different hashes →
   `likely_duplicate`. Show both sides and the reclaimable bytes; never auto‑delete.
3. **Taxonomy.** Canonical set: **Dynamics, Drive, EQ, Utility, Synth, Reverb, Modulation, Channel
   Strip, Multi FX, Filter, Delay, Spatial, Drums, Pitch, Sampler, Piano/Keys, Bass, Strings,
   Arp/Sequence, Harmony, Organ, Analyzer, Mastering, Restoration, Guitar, Vocals, Microphone,
   Network, Ambisonics, Instrument‑Other**. Map from sub‑categories per §A.5 with documented
   precedence (a plugin legitimately carries several, e.g. `Fx|EQ` + `Fx|Dynamics`). Treat `OnlyRT`,
   `OnlyOfflineProcess`, `NoOfflineProcess`, `OnlyARA`, `Mono`, `Stereo`, `Surround` as **capability
   flags**, not categories.
4. **Overrides win.** User‑set category/tag/display name lives in a separate table, merged at read
   time, so a rescan or reinstall cannot undo it. Export/import as JSON. Mark edited rows so the user
   can tell their choice from a guess.
5. **Heuristics are last resort**, used only when sub‑categories are missing or useless (`Fx` alone,
   `Fx|Instrument`), each carrying a `confidence` and rendered as "guessed". A helper‑derived signal
   (parameter count, bus layout) beside name keywords beats name keywords alone.
6. Unit‑test the mapper against **≥60 real sub‑category combinations**, including the nasty ones:
   `[]`, `["Fx"]`, `["Fx","Instrument"]`, `["Instrument","Synth","Sampler"]`, `["Spatial","Fx"]`,
   `["Analyzer"]`.
7. Report: schema, duplicate counts on the test library, mapper pass rate, and every plugin the
   mapper got wrong with the reason.

**Stop and report.**

### PHASE 3 — Host helper (the only place plugin code runs)

Deliver `vst3host`: a headless executable with a JSON‑RPC mode.

1. **Isolation contract.** One plugin per process; the parent enforces `load_timeout` (default 5 s),
   `render_timeout` (10 s) and `screenshot_timeout` (5 s); on timeout `SIGKILL`/`TerminateProcess` and
   record `hung_at = <step>`. The parent never blocks the UI; results arrive as events with a
   crash‑record row.
2. **Probe pipeline.** `GetPluginFactory` → `getClassInfo` → `createInstance<IAudioProcessor>` →
   `setupProcessing` (sampleRate 48000, maxSamplesPerBlock 256, declared speaker arrangement,
   kRealtime) → `setActive(true)` → render N blocks. Record every step so a failure is attributable
   to a step rather than to "the plugin".
3. **Classify failures precisely:** `arch_mismatch`, `library_not_loadable`/`missing_symbol`,
   `factory_returned_null`, `create_instance_failed`, `no_audio_processor`, `setup_rejected`,
   `hang_at:<step>`, `crash_at:<step>`, `license_required`, `gated_by_os` (quarantine/SmartScreen),
   `timeout`. **One helper failure is `unknown`, never `broken`** — the DAW may load it fine with a
   license, dongle or GUI present. Only a failure reproduced with the plugin's declared requirements
   met may be labelled broken.
4. **CPU measurement protocol** (§A.10): 48 kHz, 256‑sample blocks, silence in, output discarded;
   measure process CPU time over ≥10 s of audio; report the median of three runs as "% of one core",
   stamped with `measured_on = "<machine model>, <cpu>"` and `app_version`. Never display another
   machine's number as your own.
5. **Extras the UI will want:** parameter count and names (searchable — "has a mix knob"), bus
   layout, `IUnitInfo`/program lists, and `.vstpreset` headers (§A.8) for the variants browser.
6. **Screenshot capture** (§A.7) under the same isolation. Some editors only paint when visible: try
   an offscreen surface, then a 1×1 visible window parked off‑screen, then fall back to a generated
   tile. Record which method worked per plugin so it is not retried on every scan.
7. Tests: deliberately crashing, hanging, arch‑mismatched and `factory_returned_null` fixtures —
   20/20 runs must recover with the UI process still alive.
8. Report: pass/fail/skip counts, crash‑record samples, a measured‑CPU table for 20 plugins, and the
   helper's memory high‑water mark.

**Stop and report.**

### PHASE 4 — Library UI

The UI has one job: get from "I want that compressor" to "it is on my track" faster than the DAW's own
browser, without lying about anything.

1. **Surfaces.** (a) a floating, always‑on‑top grid summoned by a global hotkey and dismissed with
   `Esc`; (b) an optional docked/list mode; (c) a compact "chooser" overlay for hold‑to‑pick; (d)
   Settings (roots, adapters, permissions, key map, overrides). Remember geometry per DAW.
2. **Focus discipline.** Record the frontmost application *before* showing, and restore it on `Esc`
   or on insert, so focus returns to the DAW rather than to a stray window. If restore fails, show a
   non‑blocking hint instead of silently swallowing the user's next keystroke.
3. **Card contents.** Snapshot image (or generated tile), name, vendor, category chip(s), instrument
   vs effect, arch badges, bundle size, measured CPU (with machine label), health dot, favourite
   star, bound key. Load images lazily, decode off the UI thread, and cap the memory cache — 1,500
   cards × 2× snapshots will otherwise eat hundreds of MB.
4. **Grid performance.** Virtualize; 1,500 cards must scroll at ≥55 fps with first paint <400 ms from
   hotkey. Prefetch the next screen only.
5. **Search.** Tokenized, diacritic‑ and case‑insensitive, fuzzy over name + vendor + category +
   sub‑category + tag, prefix‑weighted, 50 ms debounce, and an empty state that says what to clear.
   `/` focuses search, arrows move, `Enter` inserts, `⌘/Ctrl+K` opens a command palette (rescan, open
   vendor folder, copy UID, run doctor).
6. **Filters.** Category, vendor, sub‑category, instrument/effect, arch, "loads in <running DAW>",
   health (`ok` / `unknown` / `unreadable` / `missing`), duplicates only, measured only, favourites,
   source root (user/global), and "used in a project" / "never used". Filters compose and serialize
   into saved views.
7. **Sort.** Name, vendor, size, measured CPU, recently inserted, recently installed.
8. **Card actions.** Insert; insert and open editor; insert on a new track; map to key; load a preset
   variant; reveal in Finder/Explorer; copy UID; rescan this bundle; set category/tags; open vendor
   site; and **"why is this greyed out?"**, which must always answer in plain language ("this bundle
   only contains an Apple‑silicon binary and your DAW is running under Rosetta").
9. **Multi‑select** applying the same actions in batch, plus "export selection as JSON" so users can
   share a shortlist.
10. **Accessibility.** Full keyboard operation, visible focus ring, VoiceOver/Narrator labels on cards
    (name, vendor, category, key), reduced‑motion support, and no colour‑only status encoding.
11. Report: a keyboard‑only walkthrough, the 1,500‑card performance numbers, and every place you had
    to show `UNKNOWN`.

**Stop and report.**

### PHASE 5 — Insert adapters

This is where the product either works or damages a session. Build it in this order.

**5.1 The contract, again.** No adapter may be registered without `capabilities()` and `doctor()`
returning real data. Anything unimplemented returns `Unsupported`, and the UI renders "not supported in
<DAW>" with a link to the doc page — never a silent no‑op, never a fake success.

**5.2 REAPER first (native, no permissions, fully verifiable).**

REAPER is the reference implementation because it exposes a real API. Insert path:

```lua
-- background ReaScript, driven by reaper.defer()
local track = reaper.GetSelectedTrack2(0, 0, false)
if not track then
  reaper.InsertTrackAtIndex(reaper.GetNumTracks(0), true)
  track = reaper.GetTrack(0, reaper.GetNumTracks(0) - 1)
end
local idx = reaper.TrackFX_AddByName(track, fxname, false, -1)  -- -1 => always add a new instance
if idx >= 0 then
  local _, name = reaper.TrackFX_GetFXName(track, idx, "")      -- verify the host matched our plugin
  reaper.TrackFX_SetOpen(track, idx, true)                      -- only when the user asked for the UI
end
```

- Semantics that matter: `recFX=false` addresses the track FX chain (`true` = record‑input/monitoring
  chain, which is the wrong slot for this product); `instantiate=-1` always adds a new instance, `0`
  only queries, a positive value adds only if absent; the return is `-1` on failure or the new chain
  position; positions may carry `0x2000000` when they refer to FX inside containers, so strip it
  before indexing. Verify by reading `TrackFX_GetFXName` back at the returned position — a mismatch
  means REAPER matched a different plugin and the insert must be reported as unverified.
- **Learn the exact name string from the host, not from a guess.** Enumerate with
  `reaper.EnumInstalledFX(index)` → `(name, ident)`; `name` is exactly what `TrackFX_AddByName`
  accepts and is conventionally `VST3: <Name> (<Vendor>)`. Build the CID → `fxname` map from that
  enumeration matched against your index by `(name, vendor)`, flag ambiguous matches, and never
  construct the string by concatenation.
- **Transport.** Prefer EXTSTATE (`SetExtState`/`GetExtState` with a monotonically increasing job id)
  or a JSON job file polled at 30 ms inside `reaper.defer`; both survive a busy DAW. Echo a
  `request_id` back so the app can attribute latency, and keep a watchdog so a stale job cannot fire
  when the user opens an old project an hour later.
- Install the script into REAPER's `Scripts/` folder from the app, record exactly which files were
  written (for the uninstaller), and never overwrite a file whose content hash is not ours.
- Latency budget: hotkey → plugin on the selected track, **p95 < 500 ms** over 50 trials.

**5.3 Every other DAW (UI automation, gated by focus verification).**

There is no public plugin‑insert API in Ableton Live, Cubase/Nuendo, Bitwig, FL Studio or Studio One,
so the route is "drive the DAW's own browser the way a human would": bring the host forward, focus its
plugin browser search field, type a distinctive prefix of the plugin name, wait until the result list
is non‑empty and stable, press `Return`, then verify the insert by reading the host UI before
reporting success.

Mandatory safety protocol for every keystroke‑based adapter — implement it once as a shared, tested
module rather than per adapter:

1. **Pre‑flight.** Target process running; expected main‑window title matches the known pattern; that
   window is frontmost; no modal sheet/dialog present (query AX/UIA for `AXSheet`/`AXDialog`,
   `Role=Dialog`, enabled‑and‑focused); expected search field exists and is focused, or can be focused
   deterministically.
2. **Abort on any mismatch** with `FocusMismatch` / `ModalOpen` and send **zero** keystrokes. A blind
   `⌘V` into a DAW can paste over a region, rename a track or fire a destructive command.
3. **Type, then verify.** After typing, confirm the result list narrowed (count changed or first row
   name matches) before pressing `Return`. If the result is ambiguous ("Pro‑Q 3" vs "Pro‑Q 4"),
   refuse, ask the user once for a better search token for that plugin, and cache the answer.
4. **Never paste** the search text. Type characters with a small inter‑key delay, and allow only one
   host interaction in flight at a time.
5. **Always tell the user what happened:** success, "inserted but could not verify", or the exact
   abort reason — copyable, naming the recipe step that failed.

Per DAW, document only what you actually verified (§B is the starting matrix): Ableton Live (browser
search + Return; macOS needs Accessibility, Windows uses UIA and needs the host at the same integrity
level), Bitwig, Cubase/Nuendo, FL Studio, Studio One/Studio Pro. Where a DAW exposes a supported hook
(MIDI Remote, controller scripts, macros), use it to *reduce* synthesized keystrokes, never as the sole
verification step.

**5.4 `doctor` per adapter.** Prints: host detected (name, version, pid), permission status
(Accessibility, Input Monitoring, Screen Recording where relevant), frontmost app, whether the expected
window and search field were found, whether insert is verified on this host build, and the one‑line
fix for each failing check.

**5.5 Tests.** The positive path per adapter is an `@integration` test requiring a real DAW instance and
is skipped in CI. Negative paths are unit tests against a mocked AX/UIA tree and **must** cover: wrong
app frontmost, modal open, search field missing, result list empty, two identical names — ten cases
minimum, each asserting "zero keystrokes emitted".

**5.6 Report:** the adapter matrix with honest status per DAW, latency numbers per adapter, and the
list of hosts you could not verify at all.

**Stop and report.**

### PHASE 6 — Global hotkeys and key mapping

1. **Registration.** macOS: `RegisterEventHotKey` for plain combos plus a `CGEventTap`/`NSEvent` global
   monitor when the DAW swallows the key — note that listening to keys needs **Input Monitoring** TCC,
   a *different* permission from **Accessibility**; the app must request both, at the right moment,
   with an explanation. Windows: `RegisterHotKey` first, low‑level `WH_KEYBOARD_LL` as fallback; never
   require elevation. Linux: X11 `XGrabKey`, and state the Wayland limitation honestly in the docs.
2. **Key‑map UI.** Drag a card onto a virtual keyboard, or press the key you want. Render bindings as
   physical key caps so layouts (QWERTY/AZERTY) do not lie; store both the physical key and the
   resulting character.
3. **Conflict intelligence — computed, not decorative.** Three conflict sources: (a) other plugin
   bindings in this app; (b) the DAW's own shortcuts where discoverable (REAPER's `reaper-kb.ini` can
   be parsed); (c) OS‑level shortcuts (macOS system shortcuts, Windows reserved combos). The "works in
   every DAW / only in this one" badge must be the intersection of adapter capability and these
   conflict sets, and must render "unverified" when a DAW's shortcut table cannot be read.
4. **Modes.** Tap = insert; hold = open chooser at cursor; `Shift`+tap = insert and open editor;
   `⌥/Alt`+tap = insert on a new track. Support key *pages* (a modifier layer) so more than 26 plugins
   can be bound without the user inventing chords.
5. **Per‑DAW bindings.** The same key may mean different plugins in different DAWs; the active binding
   set follows the running host, and the UI must say which set is live right now.
6. **Perf.** Keydown → inserted: p95 < 500 ms on the native adapter, < 2.5 s on UI automation, with a
   busy state if it exceeds 300 ms.
7. Report: the hotkey layer, a conflict report for a realistic 40‑binding set, and latency numbers per
   adapter.

**Stop and report.**

### PHASE 7 — Remote, Stream Deck and OSC

1. **Local server.** HTTP + WebSocket bound to `127.0.0.1` only, per‑pairing bearer token, QR code on
   the desktop to pair a phone. No cloud, no account. Rotate the token on unpair, rate‑limit, reject
   `Origin` mismatches, and expose no method that executes a shell or reads arbitrary paths.
2. **PWA remote.** Grid + search + insert + transport, installable, and only reachable on the LAN when
   the user explicitly enables "allow LAN" (default off, with a warning).
3. **Stream Deck plugin** (Elgato SDK): actions for insert‑by‑binding, open chooser, next/previous page
   and category jump; the property inspector lists the library so keys are configured visually.
4. **OSC** (UDP `127.0.0.1`) for TouchOSC/Open Stage Control, with a documented address map.
5. **Threat model note** in the repo: what an attacker on localhost could do, why the token exists, and
   why the WS API is deliberately not a general RPC bridge.
6. Report: pairing flow, token rotation, and a fuzz pass over the WS message handler.

**Stop and report.**

### PHASE 8 — Library health and session audit

**8.1 Health run** (on demand, plus a monthly background check that never blocks startup):

- **Duplicates** per Phase 2, with reclaimable bytes and the exact paths, plus "which copy does each
  DAW actually load?" — answered from the scan order, not from guesswork.
- **Won't load** from helper probes, with the step that failed and the distinction between
  `unreadable`, `arch_mismatch`, `license_required` and `unknown`.
- **Architecture reality check** using §A.6 and the running host's architecture: report "will load in
  <DAW> (native)", "will load under Rosetta", "will not load in this host".
- **Hygiene findings:** missing `moduleinfo.json`, legacy single‑file `.vst3`, stale or looping
  symlinks, macOS quarantine xattr, unsigned/not‑notarized bundles, bundles with no loadable arch for
  this machine, and bundles whose vendor/name disagree with their filename.
- **Footprint:** per‑vendor and total sizes, largest bundles, and "space you would get back" if
  duplicates were removed — as a number, not a promise (deletion is always the user's action, always
  to the Trash/Recycle Bin, always with the path listed first).
- **History:** keep runs so the user can see "went from 3 broken to 1" after an update.

**8.2 Session audit (the VST3‑only specialty).** Read project files and answer: *what does this
session need, what is missing, and what has been superseded?* Implement in evidence tiers and say which
tier each parser is at:

| Project file | Format | Confidence |
|---|---|---|
| REAPER `.rpp` | plain text; FX lines carry a quoted identifier beginning `VST3:` | high — parse directly |
| Ableton `.als` | gzip'd XML; VST3 device entries carry a UID and plugin name | high — after verifying against one real set |
| Bitwig `.bwproject` | zip of XML | high — after verification |
| Cubase `.cpr` / `.npr` | binary; class IDs appear as 16‑byte GUIDs | best effort — strings + GUID scan |
| Studio One/Studio Pro `.song` | mixed/binary | best effort |
| FL Studio `.flp` | binary event stream; plugin names appear as text events | best effort |

Rules: never modify a project file (open read‑only, copy to a temp dir if a decompressor needs a
seekable file); match by UID, not by name, and use `Compatibility` (§A.4) to resolve "project asks for
old UID → this bundle provides it as `New`"; report three buckets — **present**, **missing** (with the
vendor/name recovered from the file so the user can go buy/reinstall it), and **superseded** (old UID
now provided by a newer bundle). Feed the result into filters ("used in a project", "never used") and
into a "check a session before you open it" report the user can run from the app.

**8.3 Report:** a health run over the test library with real numbers, one audit of a real `.rpp` and
one of a real `.als` with the three buckets populated, and the reclaimable‑space calculation checked by
hand.

**Stop and report.**

### PHASE 9 — Packaging, permissions, performance, docs

1. **macOS.** Hardened Runtime, notarized DMG, **not** App Sandbox (you need to read
   `/Library/Audio/Plug-Ins/VST3` and drive other apps). Entitlements you will likely need:
   `com.apple.security.cs.disable-library-validation` (to load third‑party plugin binaries that are
   unsigned or signed by another team), `com.apple.security.cs.allow-unsigned-executable-memory` and
   `com.apple.security.cs.allow-jit` for plugins that JIT. Justify each one in the repo — reviewers and
   users both ask.
2. **Permission UX.** Request Accessibility and Input Monitoring only when the user first tries the
   feature that needs them, explain what it is for in one sentence, and deep‑link to the right System
   Settings pane (`x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility`).
   Detect the "granted but the process was already running" case and tell the user to relaunch — this
   single bug accounts for most support tickets in tools like this.
3. **Windows.** Sign the installer and binaries; prefer MSI/NSIS over MSIX (MSIX constrains UI
   automation and cross‑process work). Detect the elevated‑DAW case and explain that UI automation at a
   different integrity level will fail, instead of failing silently.
4. **Uninstall.** Remove only files the app created — with the manifest written at install time,
   including DAW‑installed helper scripts — and say so in the UI.
5. **Performance budgets, enforced as tests:** cold scan of 1,500 bundles ≤20 s; warm rescan ≤2 s;
   idle CPU ≈0%; UI memory ≤250 MB with 1,500 cards; hotkey→insert p95 <500 ms (native).
6. **Docs.** `SETUP-<DAW>.md` per supported DAW (what to install, what to grant, how to verify),
   `TROUBLESHOOTING.md` (focus loss, plugin not found in host, permission denied, plugin loads in DAW
   but not in the helper), `PRIVACY.md` (exactly what leaves the machine: nothing but the license
   check), and `ARCHITECTURE.md` (the process model and why the helper exists).
7. **Release checklist** in the repo: version bump, migration test from the previous DB, fixture suite
   green, integration suite run manually on each supported DAW, signature verification, and a scan of a
   clean VM.
8. Report: installers, budgets with measured numbers, docs, and the clean‑VM scan result.

**Stop and report. Final report includes the adapter matrix and every known limitation.**

### ACCEPTANCE CRITERIA

Each item must be demonstrated with command output or a recording, not asserted.

1. `vst3idx scan` over a library of ≥1,000 real bundles indexes **100%** of those with a valid
   `moduleinfo.json`, creates **0** spurious duplicate rows, and exits 0. Counts printed per root.
2. The fixture library (§E, 40 synthetic bundles) produces a **byte‑identical golden JSON** report:
   valid, JSON5‑with‑comments, missing moduleinfo, controller‑only, legacy single file, arch mismatch,
   corrupt JSON, 200 MB bundle, symlink loop, merged bundle, unicode names, duplicate UID across roots.
3. Crash injection: 20/20 runs where a fixture plugin crashes, hangs on `GetPluginFactory`, hangs in
   `setActive` or hangs in `process` end with the UI alive, a crash record written, and the plugin
   marked `unknown` (not `broken`).
4. Insert into REAPER's selected track from a global hotkey works 50/50 times, p95 <500 ms, and lands
   in the **next free slot of the selected track** — proven by reading `TrackFX_GetFXName` at the
   returned index, not by screenshot.
5. UI‑automation adapters refuse to type when focus or context is wrong: 10/10 negative tests (wrong app
   frontmost, modal open, search field absent, ambiguous result, host not running) abort with a typed
   error and **zero** keystrokes emitted (asserted against the mocked input layer).
6. Session audit on a fixture `.rpp` and `.als` reports exactly the expected plugin set, including one
   missing plugin and one UID resolved through `Compatibility`.
7. Overrides survive: change a category, rescan, restart the app → override still applied, and `verify`
   reports 0 drift between DB and disk.
8. Grid with 1,500 cards: first paint <400 ms from hotkey, scroll ≥55 fps, memory ≤250 MB.
9. Zero writes into vendor bundles: an FSEvents/`ReadDirectoryChangesW` watcher during a full scan
   records no modifications under any `.vst3`; mtimes unchanged.
10. No network egress except the license check, verified with a proxy/socket log; the WS server refuses
    non‑localhost origins and unpaired tokens.
11. `doctor` for every registered adapter prints host, permissions, focus state and verified/
    unsupported status — and the status matches what the acceptance run actually observed.
12. `--rebuild` from an empty DB reproduces the same index as an incremental scan (same UIDs, same
    bundle rows, same overrides applied).
13. The shippable artifact is a real Windows `.exe` (Appendix F): on a fresh VM with no dev tools and no
    admin rights the portable binary scans, renders the grid and inserts into REAPER; the installer
    uninstalls to zero residue; overrides and key bindings survive uninstall → reinstall.

### THINGS THAT WILL MAKE YOU WRONG (read twice)

- **`moduleinfo.json` is JSON5, not JSON.** Comments, trailing commas and unquoted keys are legal.
  `JSON.parse`/`serde_json::from_str` on the raw file will fail on real bundles. Use a JSON5 parser and
  keep a copy of the original bytes for hashing.
- **Not every class is a plugin.** `Component Controller Class` entries are not loadable units, and an
  `Audio Module Class` is not necessarily an instrument — read the sub‑categories.
- **A `.vst3` is usually a folder, but not always.** Since SDK 3.6.10 the folder is the format; single‑
  file `.vst3` DLLs from earlier SDKs still exist in the wild. Never hard‑code `Contents/Resources`.
- **`moduleinfo.json` moved.** 3.7.5–3.7.7 put it in `Contents/`; 3.7.8+ puts it in
  `Contents/Resources/` (to stay safe with macOS code signing). Read both, prefer `Resources`.
- **Merged bundles lie to naive code.** One `.vst3` can hold `x86_64-win`, `arm64ec-win`, `MacOS`,
  `x86_64-linux` and more. "The binary" is not a single file; pick per host architecture and report all.
- **A helper failure is not a verdict.** A plugin may load in the DAW and fail in your helper (license,
  dongle, ARA, GUI dependency, host‑specific quirk). Mark `unknown`; never tell a user their working
  plugin is broken.
- **Never load plugins in‑process "for speed".** One bad plugin and the user loses an unsaved session.
  The helper exists precisely because this will happen.
- **`IPluginFactory` class order is not stable.** Never address a class by index across runs; always by
  CID.
- **UID byte order will bite you.** `Compatibility` and project files may express the same class
  differently (hex string vs four uint32s, and endianness varies by writer). Validate your comparison
  against one known plugin whose UID you can read from both a project file and its bundle before
  trusting the matcher at scale.
- **Don't trust names for identity.** Two vendors ship "Compressor"; one plugin ships under two names in
  two DAWs. Identity is the UID; names are display.
- **macOS permissions are two, not one.** Accessibility (AX tree, synthetic events) and Input Monitoring
  (global key listening) are separate TCC grants; a fresh grant often needs an app relaunch to take
  effect. Screen Recording, if you ever capture UI as a fallback.
- **Synthetic keystrokes into a DAW are dangerous.** Without the pre‑flight checks in §5.3 you can
  paste over a region, rename a track or trigger a destructive command. Refusing is a feature.
- **Don't install things into DAW folders silently.** If you write a helper script, record its path and
  hash; uninstall must remove exactly what you added and nothing else.
- **`TrackFX_AddByName` reuse semantics.** `instantiate > 0` will silently reuse an existing instance
  instead of adding one — wrong behaviour for a "put it on this track" action. Use `-1`.
- **Don't promise cross‑DAW key portability you cannot compute.** If a DAW's shortcut table is
  unreadable, the badge says "unverified", not "safe".
- **Don't copy the reference product.** Plugindex is a trademarked commercial product (DE Productions
  LLC); its name, copy, screenshots and design are not yours to reuse. Independent implementation,
  own naming, own visual language.

---

## Appendix A — VST 3 facts the agent must not fumble

Source of truth: the VST 3 Developer Portal and the current SDK. When this appendix and the installed
SDK disagree, the SDK wins — but log the disagreement rather than silently following either.

### A.1 What is physically on disk

A VST 3 plugin is a **bundle** with the `.vst3` extension. Since SDK 3.6.10 that means a folder, not a
DLL. Three layouts exist in the wild; handle all three.

**macOS** — a real macOS bundle:

```
MyPlugin.vst3/
  Contents/
    Info.plist                          # CFBundleExecutable, CFBundleIdentifier, version
    PkgInfo                             # optional
    MacOS/
      MyPlugin                          # Mach-O (often universal: arm64 + x86_64)
    Resources/
      moduleinfo.json                   # 3.7.8+ location
      Snapshots/
        <CID>_snapshot.png              # 1.0x
        <CID>_snapshot_2.0x.png         # 2.0x
      <other vendor resources: .srf, PDFs, XMLs>
```

**Windows** — a folder that only *looks* like a bundle; the inner file keeps the `.vst3` name:

```
MyPlugin.vst3/
  Contents/
    Resources/moduleinfo.json
    x86-win/MyPlugin.vst3           # 32-bit x86
    x86_64-win/MyPlugin.vst3        # 64-bit x64
    arm64ec-win/MyPlugin.vst3       # Arm64EC
    arm64x-win/MyPlugin.vst3        # Arm64X (Arm64 + Arm64EC in one image)
    arm64-win/MyPlugin.vst3         # Arm64 classic
    arm-win/MyPlugin.vst3           # Arm classic (deprecated)
  desktop.ini                       # optional custom icon
  Plugin.ico                        # optional
```

**Linux** — same idea: `Contents/<uname -m>-linux/MyPlugin.so`, e.g. `x86_64-linux`, `i386-linux`,
`aarch64-linux`, `armv7l-linux`.

**Merged bundle** — one `.vst3` containing several platform/arch folders at once (`MacOS/`,
`x86_64-win/`, `x86_64-linux/`, …), usually with shared `Resources/`. Distribution‑friendly, and the
reason "find the binary" must never assume a single hit.

**Legacy single file** — a `.vst3` that is directly a DLL/`.so`/Mach‑O with no `Contents/`. Still
shipped by older SDKs. Index it, flag `legacy_single_file`, and probe the helper for metadata since
there is no `moduleinfo.json`.

### A.2 Where to look (and in what order)

The format defines three priority levels — **User**, **Global**, **Application** — and one hard rule:
**the first bundle found for a given processor UID wins.** Record the losers as `shadowed` instead of
discarding them.

| Prio | macOS | Windows | Linux |
|---|---|---|---|
| 1 | `~/Library/Audio/Plug-Ins/VST3/` | `%LOCALAPPDATA%\Programs\Common\VST3\` | `~/.vst3/` |
| 2 | `/Library/Audio/Plug-Ins/VST3/` | `%ProgramFiles%\Common Files\VST3\` | `/usr/lib64/vst3/` (Fedora) |
| 3 | `/Network/Library/Audio/Plug-Ins/VST3/` | `%ProgramFiles(x86)%\Common Files\VST3\` | `/usr/lib/vst3/` |
| 4 | `<App>.app/Contents/VST3/` | `<AppFolder>\VST3\` | `/usr/local/lib64/vst3/` |
| 5 | — | — | `/usr/local/lib/vst3/` |
| 6 | — | — | `<AppFolder>/vst3/` |

- Scanning is **recursive**, and **symlinks are explicitly allowed** — follow them with cycle detection
  (device+inode seen‑set) and a depth cap (e.g. 8).
- `%ProgramFiles(x86)%\Common Files\VST3` holds 32‑bit plugins on 64‑bit Windows; a 64‑bit host cannot
  load those. Your arch check (§A.6) must reject them *before* the helper tries.
- Users routinely keep a second library on a sample drive. Support extra roots in Settings and mark
  them `custom`, so priority is explicit and visible.
- Do **not** scan `…/Plug-Ins/Components` (AU) or `…/Plug-Ins/VST` (VST2), nor installer temp dirs.
  Noise hurts the product.
- Some installers nest a bundle inside a vendor folder; recursion covers it, so do not assume depth 1.

### A.3 `moduleinfo.json` — the metadata backbone

An **optional** JSON5 file, generated by `moduleinfotool` at build time for SDK‑built plugins. It
carries the same information as the module factory — so a host can know what a module provides
*without loading it* — plus an optional `Compatibility` list. That is exactly why this product can index
a 1,500‑plugin library in seconds without executing anything.

```json5
{
  "Name": "helloworld",
  "Version": "3.7.5.0",
  "Factory Info": {
    "Vendor": "Steinberg Media Technologies",
    "URL": "https://www.steinberg.net",
    "E-Mail": "mailto:info@steinberg.de",
    "Flags": { "Unicode": true, "Classes Discardable": false, "Component Non Discardable": false },
  },
  "Classes": [
    {
      "CID": "BD58B550F9E5634E9D2EFF39EA0927B1",
      "Category": "Audio Module Class",
      "Name": "Hello World",
      "Vendor": "Steinberg Media Technologies",
      "Version": "1.0.0.1",
      "SDKVersion": "VST 3.7.5",
      "Sub Categories": ["Fx"],
      "Class Flags": 1,
      "Cardinality": 2147483647,
      "Snapshots": [
        { "Scale Factor": 1, "Path": "Contents/Resources/Snapshots/BD58…_snapshot.png" },
        { "Scale Factor": 2, "Path": "Contents/Resources/Snapshots/BD58…_snapshot_2.0x.png" }
      ],
    },
    {
      "CID": "A0B1A6F4005D9B47967177E37A671891",
      "Category": "Component Controller Class",
      "Name": "Hello WorldController",
      "Vendor": "Steinberg Media Technologies",
      "Version": "1.0.0.1",
      "SDKVersion": "VST 3.7.5",
      "Class Flags": 0,
      "Cardinality": 2147483647,
      "Snapshots": [],
    },
  ],
  "Compatibility": [
    { "New": "BD58B550F9E5634E9D2EFF39EA0927B1", "Old": ["AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"] },
  ],
}
```

Parsing rules that matter:

- **JSON5** — comments, trailing commas, unquoted keys. Use a JSON5 parser and keep the original bytes
  for hashing.
- `Category` is either `"Audio Module Class"` (a loadable processor) or `"Component Controller Class"`
  (an edit controller). Only the former becomes a card.
- `Snapshots[].Path` is **bundle‑relative** and usually points into `Contents/Resources/`. Honour
  `Scale Factor`; prefer 2.0× on HiDPI displays.
- A `Classes` entry may override `Vendor`/`Version` per class. That is legitimate (a licensed engine
  inside a vendor's plugin) — do not "correct" it to the factory vendor.
- `moduleinfotool` (`public.sdk/vst-utilities/moduleinfotool`) generates and validates these files;
  `ModuleInfoLib` (`public.sdk/source/vst/moduleinfo`) is the reference reader. Read that reader's
  source when a field is ambiguous — it is the same code hosts use.
- If the file is missing, **do not** guess from the filename. Probe the module (Phase 1.5).

### A.4 UIDs, class flags and `Compatibility`

- A class ID is a **16‑byte FUID**, written as 32 uppercase hex characters (`CID` in `moduleinfo.json`).
  Treat it as an opaque 128‑bit value: do not parse it into numbers, do not reorder it, do not
  normalise case for storage (uppercase on write, case‑insensitive only on comparison).
- **`Compatibility`** declares that a class replaces one or more older classes: `{ "New": <CID>,
  "Old": [<CID>, …] }`. This is what lets a host load an old project that references a legacy UID. It
  is also the single most valuable field for session audits: "this project wants UID X; the installed
  bundle provides it as `New`" is a *provable* statement, unlike name matching.
- **`Class Flags`** — bit 0 (`kSimpleModeSupported`) is the one you will see; do not interpret the rest
  as capabilities without checking the SDK header for the version you target.
- **`Cardinality`** is the maximum number of instances the plugin supports (`2147483647` = effectively
  unlimited). Rarely useful in UI, but record it: a cardinality of 1 means "this plugin cannot be on
  two tracks at once", which is worth surfacing in health checks.
- **Fallback metadata path** (no `moduleinfo.json`): in the helper, `GetPluginFactory()` →
  `IPluginFactory::countClasses()` / `getClassInfo(i, PClassInfo2&)` gives `category`, `name`,
  `vendor`, `version`, `subCategories` (semicolon‑joined string), `classFlags`. `PClassInfo` (v1) lacks
  `subCategories`; prefer v2. `IComponent::getControllerClassId()` pairs the controller.
- Never index by class position: factory order is not guaranteed stable across versions or loads.

### A.5 Sub‑category strings → your taxonomy

These are the **exact** literals from `Steinberg::Vst::PlugType` (VST 3.8 SDK). They appear verbatim in
`Sub Categories` arrays; note the space in `"Channel Strip"`, `"Pitch Shift"` and the hyphen in
`"Up-Downmix"`. Never case‑fold before comparison on the vendor side — compare exactly, and treat
unknown strings as `Other` *plus a logged warning*, because new ones appear with SDK releases.

| VST 3 string | Canonical category | Note |
|---|---|---|
| `Fx\|Dynamics` | Dynamics | compressor, gate, limiter, maximizer |
| `Fx\|Distortion` | Drive | amp sim, clipper, saturation |
| `Fx\|EQ` | EQ | |
| `Fx\|Filter` | Filter | wah, tone booster |
| `Fx\|Reverb` | Reverb | |
| `Fx\|Delay` | Delay | |
| `Fx\|Modulation` | Modulation | chorus, phaser, flanger, tremolo |
| `Fx\|Channel Strip` | Channel Strip | |
| `Fx\|Pitch Shift` | Pitch | tuning, correction |
| `Fx\|Spatial` | Spatial | mono→stereo, width |
| `Fx\|Surround` | Spatial | flag `surround` |
| `Fx\|Mastering` | Mastering | dither, noise shaping |
| `Fx\|Restoration` | Restoration | denoise, declick |
| `Fx\|Analyzer` | Analyzer | not insertable as a normal insert |
| `Fx\|Tools` | Utility | gain, tuner, mixer |
| `Fx\|Generator` | Utility | tone/noise generator |
| `Fx\|Drums` | Drums | |
| `Fx\|Bass` | Bass | |
| `Fx\|Guitar` | Guitar | |
| `Fx\|Vocals` | Vocals | |
| `Fx\|Microphone` | Microphone | |
| `Fx\|Network` | Network | |
| `Fx\|Instrument` | Instrument‑Other | loadable as instrument **and** effect; set `is_instrument` |
| `Fx\|Instrument\|External` | Instrument‑Other | wrapped hardware; set `is_external` |
| `Instrument\|Synth` | Synth | |
| `Instrument\|Synth\|Sampler` | Synth | also sampler; pick Synth, tag Sampler |
| `Instrument\|Sampler` | Sampler | |
| `Instrument\|Piano` | Piano/Keys | |
| `Instrument\|Drum` | Drums | |
| `Instrument\|External` | Instrument‑Other | `is_external` |
| `Instrument` | Instrument‑Other | bare instrument |
| `Spatial` / `Spatial\|Fx` | Spatial | panner/mix converter |
| `Up-Downmix` | Spatial | |
| `Ambisonics` | Spatial | tag `ambisonics` |
| `Analyzer` | Analyzer | standalone analyzer |
| `Fx` | Multi FX | bare `Fx` means "uncategorised effect" — low confidence |
| `OnlyRT` | — | **capability flag** `realtime_only` |
| `OnlyOfflineProcess` | — | **flag** `offline_only` |
| `NoOfflineProcess` | — | **flag** `no_offline` |
| `OnlyARA` | — | **flag** `ara_only` — will not load as a plain insert |
| `Mono` | — | **flag** `mono_only` |
| `Stereo` | — | **flag** `stereo_only` |
| `Surround` | — | **flag** `surround_only` |

Precedence algorithm (deterministic, testable):

1. Split on `|` and index **both** the full string and each segment.
2. If any string starts with `Instrument` or equals `Fx|Instrument`: `is_instrument = true`.
3. Choose the category from the **most specific** effect string present, using this order:
   `Dynamics, Distortion, EQ, Filter, Reverb, Delay, Modulation, Channel Strip, Pitch Shift,
   Mastering, Restoration, Spatial, Surround, Drums, Bass, Guitar, Vocals, Microphone, Network,
   Analyzer, Tools, Generator`.
4. If only `Fx` remains → `Multi FX`, `confidence = low`, `guessed = true`.
5. Everything in the flag rows is stripped out before step 3, and stored as flags.
6. Anything unrecognised → `Other`, `confidence = none`, and a `warnings[]` entry naming the raw string.

Multi‑category display is allowed (a channel strip that also declares `Fx|EQ` can show both chips), but
**exactly one primary category** must be chosen so the grid has a stable sort and filter.

### A.6 Which architectures can actually be loaded

The host process architecture decides, and the combinations are not symmetric:

| Host process | Can load | Cannot load |
|---|---|---|
| Windows x64 | `x86_64-win`, and `x86-win` (32‑bit plugins in a 32‑bit host; x64 host → no) | `arm64-win`, `arm64ec-win` |
| Windows arm64 (Arm64 classic) | `arm64-win`, `arm64x-win` | `arm64ec-win`, `x86_64-win`, `x86-win` |
| Windows arm64 (Arm64EC) | `arm64ec-win`, `arm64x-win`, `x86_64-win` | `arm64-win` |
| macOS arm64 | arm64 slice, or universal | x86_64‑only, unless under Rosetta |
| macOS x86_64 | x86_64 slice, or universal | arm64‑only |
| Linux x86_64 | `x86_64-linux` | `aarch64-linux`, `i386-linux` |

Practical rules for the index:

- Parse the **arch folders present** (Windows/Linux) and the **Mach‑O slices** (macOS) to build the
  `arch[]` list. A universal Mach‑O is one file with two slices — do not count it as "two bundles".
- Compute three booleans per bundle: `loads_native_in_host`, `loads_via_translation` (Rosetta on Apple
  silicon, emulation on Windows ARM), `loads_nowhere_on_this_machine`. The third one is the only case
  where the UI may grey a card out — and it must say why.
- Out‑of‑process hosting can work around *some* of these limits (an Arm64 host can drive an Arm64EC
  plugin through IPC), but **this product never hosts plugins for audio**, so it must not pretend it
  can. Report the limitation as it applies to the DAW, not to your app.
- On Windows, a 32‑bit‑only plugin in a 64‑bit DAW is the single most common "why won't it show up"
  question. Make that a first‑class filter and an explanatory tooltip.

### A.7 Snapshot images (the card art)

- `moduleinfo.json` may list `Snapshots` with `Scale Factor` 1 and 2 and a **bundle‑relative** `Path`.
  These are vendor‑provided editor captures — the legitimate, licence‑clean source for card art.
- Frosted by design: not every plugin ships them. When absent, capture one yourself in the helper
  (Phase 3.6), which is still the vendor's UI rendered by the vendor's own code on the user's own
  machine — the image never leaves the machine and is never redistributed.
- Capture order: (a) `Snapshots[]`; (b) offscreen render to a bitmap/surface in the helper;
  (c) a 1×1 or 320×180 window positioned off‑screen and blitted, for editors that only paint when
  mapped; (d) generated tile. Record the winning method in `plugins.image_source` so the expensive
  path is not retried on every scan.
- Normalise to a card aspect ratio (e.g. 4:3 and 1:1 variants), store as WebP/PNG with a max dimension
  of 640 px, and key the cache by `(uid, method, mtime)`.
- **Never upload these images anywhere.** They are vendor assets and often the most recognisable part
  of a plugin's branding; keep them local, and if a user exports a shortlist, export metadata only.

### A.8 Presets and "load variants"

- `.vstpreset` files are the interchange format: a header with the magic `VST3`, a version, the
  **class ID**, then a chunk list (`Comp` = component state, `Cont` = controller state). Parse the
  header to know which class a preset belongs to — that is how "load variant" can be offered per card
  without loading anything.
- Conventional host locations: macOS `~/Documents/VST3 Presets/<Vendor>/<Plugin>/`, Windows
  `%USERPROFILE%\Documents\VST3 Presets\<Vendor>\<Plugin>\`. Linux is host‑defined; verify on the
  target distro instead of assuming a path.
- Plugins that expose `IUnitInfo` / `IProgramListData` also have factory programs; enumerate them in the
  helper so "load variant" works even with no preset files on disk.
- Loading a preset into a live DAW is adapter‑specific and mostly unsupported; make "load variant" a
  *folder reveal + copy path* action unless a DAW adapter can genuinely do better. Do not fake it.

### A.9 Licensing: what you may actually do

- **VST 3 is MIT‑licensed since SDK 3.8** (Steinberg, 2025). No fee, no membership, no documents to
  sign, perpetual, and usable in commercial closed‑source products as long as the MIT notice is
  preserved. The older GPLv3 and Steinberg proprietary options are no longer offered for 3.8+.
- **VST 2 is not part of that licence.** Irrelevant here (VST3‑only) — but do not let a contributor talk
  you into "just add VST2 support, it's easy"; it is a licensing question, not a coding one.
- **The `VST` wordmark and the VST Compatible logo are trademarks.** Using them is optional; if you use
  them you must follow Steinberg's usage guidelines, which ship with the SDK. If you would rather avoid
  the whole question, describe compatibility in plain words ("supports VST 3 plugins") without the logo.
- **Plugin names, UI snapshots and vendor logos belong to the vendors.** Rendering them locally for the
  user's own library is one thing; shipping a public catalogue of their artwork is another. Keep images
  local, and get permission before any public directory feature.
- **Your DAW integrations are your own code.** Do not ship a DAW's scripts or binaries; write your own
  helper scripts, and make every write reversible (Phase 5.2, Phase 9.4).

### A.10 The CPU measurement protocol (so numbers mean something)

The headline claim of any "how heavy is this plugin" feature is only worth having if it is comparable.
Fix the protocol and print it next to every number:

1. 48,000 Hz, `maxSamplesPerBlock = 256`, `kRealtime`, the plugin's **declared** speaker arrangement.
2. Feed digital silence for the warm‑up block(s), then measure over **≥10 seconds of audio** (≈1,875
   blocks). Discard output to a null sink.
3. Measure **process CPU time**, not wall time: `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` /
   `GetProcessTimes`. Divide by elapsed wall time → "% of one core". This is stable on a busy machine,
   which wall‑clock timing is not.
4. Run three times, report the **median**, and also store min/max so you can spot a noisy machine.
5. Measure **per class**, not per bundle (a bundle may hold an instrument and an effect).
6. Stamp `measured_on` (machine model + CPU), `app_version`, `sample_rate`, `block_size`, and the date.
   Any UI that shows a number from another machine must label it as such — never mix "our Mac" numbers
   with "your Mac" numbers in the same ranking.
7. Refuse to measure plugins that `OnlyOfflineProcess` or `OnlyARA`; mark them "not measurable as an
   insert" instead of producing a meaningless number.
8. Do not measure in the UI process, do not measure while a scan is running, and cap the measurement
   queue so a 1,500‑plugin library does not turn the user's machine into a space heater overnight.

## Appendix B — DAW adapter matrix (starting point, verify everything)

VST3 support is widespread; **insert APIs are not**. This table is the honest starting position — every
row must be re‑verified against the specific build you test, and the app must show the verified status,
not this table.

| DAW | Loads VST3 | Insert route | Permissions | Realistic status |
|---|---|---|---|---|
| **REAPER** | yes | ReaScript `TrackFX_AddByName` (+ `EnumInstalledFX`, `TrackFX_SetOpen`) | none | **native, verifiable** — build this first |
| **Ableton Live 11/12** | yes | no public insert API → focus browser search, type, `Return` | macOS Accessibility; Windows UIA at same integrity level | best effort, verify per version |
| **Bitwig Studio** | yes | controller API insufficient for arbitrary inserts → UI automation | macOS Accessibility / Windows UIA | best effort |
| **Cubase / Nuendo 13+** | yes | MIDI Remote cannot insert plugins → UI automation of the plugin selector | macOS Accessibility / Windows UIA | best effort |
| **FL Studio** | yes | MIDI scripting cannot insert → UI automation of the plugin picker | macOS Accessibility / Windows UIA | best effort |
| **Studio One / Fender Studio Pro** | yes | macros + UI automation | macOS Accessibility / Windows UIA | best effort |
| **Logic Pro** | **no** (AU only) | — | — | **out of scope — say so plainly** |
| **Pro Tools** | **no** (AAX only) | — | — | **out of scope — say so plainly** |

Notes that save weeks:

- **DAW plugin caches are not your friend.** Hosts keep their own plugin list and may show a plugin your
  scanner does not, or hide one your scanner does (blacklisted after a crash, wrong arch, quarantined,
  not rescanned yet). Never present your index as "what the DAW sees" — present it as "what is on disk",
  and reconcile explicitly where a host list is readable (REAPER's plugin cache ini, for example) as a
  *separate* column.
- **"Selected track" must be read from the host, not remembered.** Users change selection constantly; a
  cached track id puts plugins on the wrong track, and that feels unrecoverable. For REAPER,
  `GetSelectedTrack2(0, 0, false)` is the authority, read at insert time.
- **Focus is the entire safety story** for UI automation. Spend most of Phase 5 there, not on finding the
  search field.
- **DAW updates move UI.** Version‑keyed recipes (`recipe` rows with a host version range) turn a broken
  release into a one‑row data fix instead of a new build.
- **A DAW you cannot verify must still be listed** with status `unverified` and a "report what happened"
  link. Silent omission makes the app look broken; honest labelling makes it look trustworthy.
- **Stock devices, racks and Max for Live** are not VST3 and stay out. If a user asks, the answer is
  "that's the other product".

## Appendix C — Data model and IPC sketch

### C.1 SQLite schema (starter, extend as needed)

```sql
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE roots (            -- scan locations, in priority order
  id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, kind TEXT NOT NULL,  -- user|global|app|custom
  priority INTEGER NOT NULL, enabled INTEGER NOT NULL DEFAULT 1);

CREATE TABLE bundles (
  id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,
  root_id INTEGER REFERENCES roots(id),
  name TEXT, kind TEXT,                     -- folder|merged|legacy_single_file
  size_bytes INTEGER, mtime INTEGER, fs_id TEXT,
  moduleinfo_path TEXT, moduleinfo_hash TEXT,
  arch_json TEXT,                           -- arch folders / Mach-O slices present
  binary_sha256_json TEXT,                  -- per-arch hashes
  codesign_state TEXT,                      -- signed|unsigned|notarized|unknown (macOS)
  legacy INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL,   -- ok|unreadable|missing
  status_detail TEXT, first_seen INTEGER, last_seen INTEGER);

CREATE TABLE classes (          -- one row per Audio Module Class -> a card
  id INTEGER PRIMARY KEY, bundle_id INTEGER NOT NULL REFERENCES bundles(id) ON DELETE CASCADE,
  uid TEXT NOT NULL,                        -- 32-char uppercase hex CID
  name TEXT, vendor TEXT, version TEXT, sdk_version TEXT,
  category TEXT, sub_categories_json TEXT, class_flags INTEGER, cardinality INTEGER,
  is_instrument INTEGER, is_external INTEGER, capability_flags_json TEXT,
  shadowed INTEGER NOT NULL DEFAULT 0,      -- another bundle won this UID
  image_path TEXT, image_source TEXT,       -- moduleinfo|offscreen|window|generated
  probe_status TEXT, probe_detail TEXT, probe_step TEXT,
  cpu_pct REAL, cpu_measured_on TEXT, cpu_measured_at INTEGER,
  UNIQUE (bundle_id, uid));

CREATE TABLE controllers (      -- Component Controller Classes: linked, never cards
  id INTEGER PRIMARY KEY, bundle_id INTEGER NOT NULL REFERENCES bundles(id) ON DELETE CASCADE,
  uid TEXT NOT NULL, name TEXT, component_uid TEXT);

CREATE TABLE overrides (        -- user intent, survives rescans and reinstalls
  uid TEXT PRIMARY KEY, display_name TEXT, category TEXT, tags_json TEXT,
  hidden INTEGER DEFAULT 0, notes TEXT, updated_at INTEGER);

CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT UNIQUE);
CREATE TABLE class_tags (class_id INTEGER REFERENCES classes(id) ON DELETE CASCADE,
                         tag_id INTEGER REFERENCES tags(id), PRIMARY KEY (class_id, tag_id));

CREATE TABLE key_bindings (
  id INTEGER PRIMARY KEY, uid TEXT NOT NULL, daw TEXT,      -- NULL = all DAWs
  key_code TEXT NOT NULL, modifiers TEXT, mode TEXT, page INTEGER DEFAULT 0,
  verified TEXT,                                            -- verified|unverified|conflict
  UNIQUE (daw, key_code, modifiers, page));

CREATE TABLE adapter_state (    -- per-DAW verification + recipe
  daw TEXT PRIMARY KEY, version_seen TEXT, status TEXT, permissions_json TEXT,
  recipe_json TEXT, last_doctor_at INTEGER, last_error TEXT);

CREATE TABLE health_runs (id INTEGER PRIMARY KEY, started_at INTEGER, finished_at INTEGER,
  counts_json TEXT, report_json TEXT);

CREATE TABLE projects (id INTEGER PRIMARY KEY, path TEXT, daw TEXT, mtime INTEGER, audited_at INTEGER);
CREATE TABLE project_needs (project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE,
  uid TEXT, name_hint TEXT, vendor_hint TEXT, state TEXT);   -- present|missing|superseded

CREATE TABLE presets (id INTEGER PRIMARY KEY, uid TEXT, path TEXT, name TEXT,
  factory INTEGER, mtime INTEGER);

CREATE TABLE insert_events (id INTEGER PRIMARY KEY, uid TEXT, daw TEXT, at INTEGER,
  latency_ms INTEGER, via TEXT, ok INTEGER);                 -- powers "recently used"
```

Design notes: keep the UID in its own index separate from `bundles.path`; `verify` recomputes `classes`
from disk and diffs; keep `overrides` and `key_bindings` **UID‑keyed** so reinstalling a plugin to a new
path does not destroy the user's categorisation and hotkeys. That single decision is the difference
between "my library tool" and "my library tool after I reorganised my plugins".

### C.2 JSON‑RPC method surface

`scan.start` / `scan.cancel` (streams `scan.progress`), `index.query` (filter/sort/page), `plugin.get`,
`plugin.probe` (helper), `plugin.measure` (helper), `plugin.screenshot` (helper), `plugin.insert`,
`plugin.reveal`, `health.run`, `project.audit`, `keys.bind`, `keys.conflicts`, `adapter.list`,
`adapter.doctor`, `presets.list`.

Every method returns `{ ok: true, data }` or
`{ ok: false, error: { code, message, detail } }`, where `code` comes from the typed
`InsertError`/probe enums. No free‑text error strings cross the IPC boundary — they cannot be tested,
localised, or mapped to a fix.

## Appendix D — UX rules the UI must obey

**The grid is a keyboard instrument, not a web page.** Design the keyboard path first and let the mouse
follow it.

- **States are honest and visible.** Every card is in exactly one of: `ready`, `unknown` (not yet
  probed), `unreadable` (could not read metadata), `unsupported-in-host` (arch/DAW), `error` (probe
  failed, with the step), `measuring`. Grey is never the only signal — pair with an icon and a label.
- **"Why is this greyed out?" is a first‑class control**, not a tooltip. It must answer in the user's
  language, name every applicable reason, and offer the fix ("show me the Rosetta‑compatible copy").
- **No fake freshness.** If a number came from another machine, it is labelled with that machine. If a
  plugin was never probed, the card says "not tested", not "0%".
- **Latency feels like intent.** If an insert takes >300 ms, show the step name ("opening browser…",
  "typing…"), because UI automation can legitimately take 2 s and silence looks like a hang.
- **Escape always means "get me back to my DAW".** One key, no confirmation dialog, focus restored.
- **Destructive actions never default to yes.** Deleting duplicate bundles goes to the Trash/Recycle
  Bin, with the paths listed first, and is never offered as "clean up for me".
- **Search is the product.** Type three letters of a vendor and the grid should already be theirs. Match
  on vendor, name, category, sub‑category, tags, and the DAW's own display name where you know it.
- **Empty states teach.** "No plugins found in `~/Library/Audio/Plug-Ins/VST3` — add a folder" beats a
  blank grid. Say which root was scanned and how many bundles were unreadable.
- **Accessibility is not optional.** Full keyboard operation, focus ring, screen‑reader labels on cards
  (name, vendor, category, health, bound key), 200% zoom without layout breakage, reduced motion.
- **Visual language is your own.** Do not imitate Plugindex's layout, iconography or copy. The feature
  set is the brief; the design should be yours.

## Appendix E — Fixtures and test strategy

**E.1 Synthetic library generator** (first thing you build in Phase 1, before the scanner is finished).
Emit 40 `.vst3` bundles into a temp dir, each a tiny folder with a `moduleinfo.json` and a dummy binary
file. Cover, at minimum:

| Fixture | Expectation |
|---|---|
| `valid-fx` | 1 card, category `Multi FX` or from its sub‑category, snapshot used |
| `valid-instrument` | `is_instrument = true` |
| `json5-comments-trailing-commas` | parsed successfully (this is the one that breaks naive code) |
| `missing-moduleinfo` | helper probe path exercised; `status = ok` with probed metadata |
| `controller-only` | 0 cards, 1 `controllers` row |
| `legacy-single-file` | indexed, `legacy = 1` |
| `merged-bundle` | one row, `arch[]` has ≥2 entries |
| `arch-mismatch` | `loads_nowhere_on_this_machine = true` on the target OS |
| `corrupt-json` | `status = unreadable`, `status_detail` names the parse error |
| `big-bundle` (sparse 200 MB file) | scan time unaffected; size reported |
| `symlink-loop` | scan terminates, no infinite recursion |
| `duplicate-uid-a` / `-b` in different roots | one `ready`, one `shadowed`, sizes shown |
| `unicode-name-日本語-Ünicode` | displayed correctly, searchable by transliteration if you support it |
| `empty-name-vendor` | card renders with placeholders, no crash |
| `huge-subcategory-list` | flags and primary category resolved deterministically |
| `factory-returns-null`, `crash-in-process`, `hang-in-setActive`, `hang-in-process` | helper recovers; `unknown`; crash record written |
| `superseded-uid` (`Compatibility` old→new) | project audit resolves old UID to the new bundle |

**E.2 Golden files.** The generator also emits the expected JSON report; the test compares byte‑for‑byte.
Any change to expectations must be a reviewed diff, which is how you avoid "the scanner got quietly
worse".

**E.3 Real‑world validation.** Build the SDK's sample plugins (e.g. the `again` sample) for load tests —
they are MIT‑licensed together with the SDK, so they can live in CI as a smoke fixture. Never commit a
third‑party vendor's plugin binary.

**E.4 Test layers.**

- `unit` — parsers, taxonomy mapper, UID comparison, duplicate detection, focus‑guard state machine.
- `fixture` — the generated library, golden JSON, no plugin execution.
- `helper` — probe/measure/crash isolation against SDK samples + synthetic crashers.
- `integration` — real DAW required, `@integration` tagged, skipped in CI, run manually before release on
  each supported DAW. Record the host version in the test output.
- `perf` — cold/warm scan, 1,500‑card render, hotkey→insert latency; fail the build on regression >20%.

**E.5 Evidence, not claims.** A single command produces each phase's evidence:
`./scripts/evidence/phase-N.sh` → logs + JSON + timings, committed under `evidence/`. That is how
acceptance criteria get *proven* instead of claimed, and how the next phase starts from facts.

## Appendix F — Shipping it as a real Windows `.exe`

This appendix exists because the requirement is usually stated as *"it has to work like a program, not a
web page"*. Take that literally: **a browser cannot do this job.** It cannot enumerate
`%ProgramFiles%\Common Files\VST3`, cannot load a plugin, cannot read a DAW's project file, and cannot
synthesize a keystroke into another application. If the deliverable is a website, the honest scope is the
catalogue in Variant 3. If the deliverable is the product described in §2, it must be a native
executable — and that has its own build, packaging and trust rules.

### F.1 Decide the artifact before you write code

| Artifact | Use case | Notes |
|---|---|---|
| `Setup.exe` (NSIS) | normal install, Start‑menu entry, auto‑update | needs a signature, writes an uninstall entry |
| `.msi` (WiX) | managed/enterprise deployment | more work, worth it only if asked |
| **portable single‑file `VST3Manager.exe`** | USB stick, no install, "just runs" | hardest to get right, most loved by users |
| sidecars `vst3idx.exe`, `vst3host.exe` | CLI + plugin helper | **must stay real files on disk** (F.4) |

Portable mode: detect it with a `portable.flag` file next to the exe; then store the SQLite DB, images and
logs in `.\data\`, falling back to `%LOCALAPPDATA%\<App>\` when the directory is read‑only (Program Files,
a locked USB, a read‑only share). Never write to the exe's own directory without checking first.

### F.2 Stack, chosen with `.exe` in mind

- **Tauri 2 → `x86_64-pc-windows-msvc`** — recommended: ~10 MB app, native file dialogs, real global
  hotkeys, native tray, WebView2 for the grid. Small enough that a portable build is believable.
- **Electron** — fastest to a working UI, 150 MB+, ships Chromium. Fine if you already know it; the
  sidecar/portable story is messier and startup is slower, which matters for a hotkey‑summoned tool.
- **Native (.NET/WinUI, Qt)** — best OS integration (UIA, hotkeys, tray), slowest path to a good grid.
- **Build it on Windows, or in CI on Windows.** Do not plan to produce the `.exe` on Linux as an
  afterthought: you need the MSVC toolchain, the WebView2 SDK, and a Windows machine to test UI
  automation anyway. A GitHub Actions `windows-latest` job that uploads the installer + portable zip on
  every tag is the difference between "it builds for me" and "it ships".
- Keep the core (scanner, DB, adapters) in Rust or Go behind the UI, so the CLI sidecars and the GUI are
  the same code — not two implementations that drift.

### F.3 WebView2 (the one dependency people forget)

- Windows 10/11 normally has the Evergreen WebView2 runtime, but not always (LTSC, Server, N editions,
  freshly imaged machines, some VMs). Check at startup and fail with a readable dialog + download link —
  never a white window.
- Installer: Evergreen bootstrapper (small, needs network once) or fixed‑version runtime (+~150 MB, works
  offline). Portable build: prefer fixed‑version or document the prerequisite loudly.
- Test on a clean Windows 10 VM with no dev tools installed. That single test catches most "it works on my
  machine" packaging bugs.

### F.4 The helper must remain a real process

Single‑file packaging tempts you to embed everything. **Do not embed the host helper.** It has to be a
real `.exe` on disk, because the whole isolation model depends on spawning it, timing it out and killing
it. Declare `vst3host.exe` as an external binary/resource, extract to
`%LOCALAPPDATA%\<App>\bin\<version>\` on first run, verify its hash, and start it from there. Same for the
REAPER script and any other sidecar. If extraction is impossible (locked temp, AV quarantine), say so
explicitly and degrade to "browse only" instead of pretending to insert.

### F.5 Manifest and elevation

- Request **`asInvoker`**, never `requireAdministrator`. An elevated app cannot drive a non‑elevated DAW
  through UIA, and an elevated DAW cannot be driven by a non‑elevated app — the mismatch is the bug.
- Default to a **per‑user** install (`%LOCALAPPDATA%\Programs\<App>`), with per‑machine as an option that
  warns about the elevation mismatch above.
- Write the uninstall manifest as you install (files, registry keys, DAW scripts) so Phase 9.4 can be
  honest. Add/Remove Programs entry with the app's real icon and version.

### F.6 Signing, SmartScreen and antivirus

- Sign `Setup.exe`, the app exe **and both sidecars** with the same certificate. An unsigned binary that
  also synthesizes keystrokes is a textbook AV false‑positive and a SmartScreen warning on every download.
- **Do not pack or crypt the executable** (UPX and friends). That is what malware does; it will get you
  flagged by reputation systems you cannot appeal to quickly.
- Start building download reputation early — the certificate is not the same thing as reputation. Document
  "how to verify our signature" in the README for the cautious user.
- A downloaded portable exe carries Mark‑of‑the‑Web. Loading plugins from it is fine, but spawning sidecars
  out of a temp path can be blocked; installing to a stable path avoids it. Test with MOTW present
  (`Unblock-File` off).

### F.7 Single instance, tray, and a CLI on the same binary

- Single‑instance lock per user session; a second launch focuses the existing window instead of starting a
  second scanner.
- Accept arguments on the shipped exe so scriptability is free: `--scan`, `--verify --json`,
  `--insert-uid <CID>`, `--doctor`, `--portable`. The evidence scripts (§E.5) drive **the shipped binary**,
  not a dev build — that is what makes the acceptance criteria mean something.
- Tray icon with "rescan", "pause hotkeys", "quit", and a visible paused state so the user can stop the
  keyboard listener without uninstalling anything.

### F.8 Acceptance for the `.exe` itself

- Fresh Windows 10 VM, no dev tools, no WebView2 preinstalled: double‑click the portable exe → scan → grid
  → insert into REAPER, **with no admin prompt at any point**.
- Runs from `C:\Users\Jörg\Müzik Tools\App.exe` (spaces and non‑ASCII in the path), and from a read‑only
  folder in portable mode (falls back to `%LOCALAPPDATA%` and says so).
- Second launch focuses the first window; `--scan --json` works headlessly and exits non‑zero on failure.
- `Setup.exe` installs per‑user, uninstalls to zero residue (files, registry keys, DAW scripts), and the app
  still works if the DAW is installed later.
- Signature valid on all binaries; AV clean on VirusTotal, or the false positives documented and appealed.
- **Uninstall → reinstall keeps overrides and key bindings.** That only works if they live in
  `%APPDATA%` (UID‑keyed, per §C.1) and not inside the install directory — a small schema decision with a
  large user‑visible consequence.

### F.9 If you actually meant "`.exe` plugins"

If the requirement was "index my `.exe` files" rather than "give me an `.exe`", you are not building this
product — see Variant 6, which defines that scope and its safety rules. Ask before guessing: the two
products share a grid and almost nothing else.

----- END PROMPT -----

---

## Scope notes

**Independence.** Plugindex is a real commercial product (DE Productions LLC) with its name, copy,
screenshots and design language protected as trademarks/copyright. "Same functionality" is a legitimate
brief; "same product" is not. Use your own name, your own copy, your own layout, your own icons. Do not
scrape their site, do not reuse their marketing text, do not reproduce their screenshots in your
listing.

**Trademarks.** Ableton, Logic Pro, Pro Tools, FL Studio, Cubase, Nuendo, Bitwig, REAPER, Studio One,
Fender Studio Pro and VST are trademarks of their owners. Nominative use ("works with Ableton Live") is
normal; logos and implied endorsement are not. Ship the standard disclaimer: independent product, not
affiliated with or endorsed by any DAW maker or plugin manufacturer. If you display the VST logo, follow
Steinberg's usage guidelines, which ship with the SDK.

**Vendor artwork.** Plugin UI snapshots and logos belong to the vendors. Render and cache them **locally
for the user's own library**; do not build a public hosted catalogue of other people's artwork without
permission. If you later add a community "plugin directory", that is a separate legal review.

**Copy protection.** Do not analyse, bypass or work around plugin licensing, dongles, trials or
activation. The helper exists to *test* whether a plugin loads, not to defeat anything. If a plugin
refuses to load outside a licensed host, that is a fact to record (`license_required`), not an obstacle
to route around.

**Privacy.** The index is a list of software a person has bought — commercially sensitive and personally
identifying. Keep it local, encrypt it if you ever sync it, and never send it anywhere by default. Say
this in `PRIVACY.md` in plain words.

**DAW files are the user's work.** Session audits open project files read‑only, never in place with a
write handle, and never "repair" them. A bug that corrupts a session is worse than any missing feature.

**Permissions honesty.** Accessibility and Input Monitoring are powerful. Ask for them only when the
feature needs them, explain in one sentence, never bundle them into a first‑run dialog that the user
clicks past, and make the app fully usable (browse, search, health) without either.

---

## Variants (pick one and say which)

1. **REAPER‑only MVP (recommended first slice, ~2 weeks).** Scanner + grid + search + categories +
   REAPER adapter + global hotkeys + health. No permissions, no UI automation, every acceptance
   criterion provable on one machine. Ship this, learn, then add Ableton.
2. **macOS‑first.** Accessibility on macOS is more consistent than UIA on Windows for this job, and
   TCC behaviour is documented. Add Windows once the focus‑guard module is stable.
3. **Web catalogue only (if your environment is a browser sandbox, e.g. AI Studio).** A searchable
   directory of VST3 plugins by maker/category with "how heavy" data — no scanning, no insertion.
   Be explicit in the product copy that it cannot see the user's library: the honest version of this is
   a *catalogue*, and it is a fine marketing site next to a real desktop app, but it is not the product.
4. **Native‑app‑only, no remote.** Drop Phase 7. Stream Deck and phone remote are the most expensive
   features per user, and the least essential to "get the plugin on the track".
5. **Extend later, not now.** ARA, CLAP, AU and VST2 each reopen the scope you just closed. If you ever
   add AU, note that Logic becomes reachable — and that the metadata model changes completely (`.component`
   bundles, `AudioComponents` plist entries, different UIDs), which is why it is a v2, not a flag.
6. **Loose `.exe` files instead of `.vst3` bundles — only if that is genuinely what you meant.** Then the
   scanner targets Windows PE binaries and must **classify before it acts**, because executing an unknown
   `.exe` is a security decision, not a UI action:
   - **Installer** (Inno Setup, NSIS, MSI/WiX, InstallShield) — detect by resource signatures. Action:
     extract and inspect first (`innoextract`, `lessmsi`, `7z`), or run silently (`/S`, `/SILENT`,
     `/VERYSILENT`, `/qn`) **only after explicit confirmation**, showing signer, SHA‑256 and MOTW state.
   - **Standalone build of a plugin** — version resource plus audio‑API imports, no `Contents/` payload.
     Action: launch, reveal in Explorer, pin.
   - **Packed/loader wrapper** — small stub, high entropy, embedded archive. Action: say what it is, never
     auto‑run (this is the case your `vst3-reverse-engineering-exe.md` prompt covers in depth).
   - **Unrelated app** — anything that does not identify as audio. Action: hidden from the grid by default.
   Signals to classify with: PE version resources (CompanyName/ProductName/FileVersion), the signer chain,
   embedded `VST3`/vendor strings, and whether an extracted payload contains `Contents/<arch>/*.vst3`.
   Default to **never execute**; extraction answers most questions without running anything. And be honest in
   the product copy: that is a file manager with an installer, not a plugin library manager — nothing in it
   can put a plugin on a track.


---

## How to use this prompt

1. Fill `INPUTS`, pick a variant, and paste §2 plus Appendices A–F into a fresh session.
2. Decide the artifact from Appendix F on day one (`DELIVERY` in `INPUTS`). A desktop app nobody can
   double‑click is not the product; if the target is a portable `.exe`, say so before Phase 1 so the
   sidecar/DB paths are designed for it rather than retrofitted.
3. Expect the agent to stop after Phase 0. Read `SCOPE.md` and the adapter interface — that is the
   cheapest place to disagree with it.
4. Run the evidence script after each phase. If a phase's evidence is "it works on my machine", it is not
   done.
5. Do not let it start Phase 5 before Phase 3's crash isolation passes 20/20. Everything downstream
   assumes the UI cannot be killed by a plugin.
6. When something is unsupported, keep it visible and labelled. The product's credibility is built out of
   the sentences it refuses to say.
