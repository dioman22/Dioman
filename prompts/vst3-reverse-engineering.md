# Prompt: Reverse Engineering a Windows VST3 Plugin (`.vst3` / `.dll` / `.exe`)

A copy‑paste brief for an AI agent doing **static + dynamic reverse engineering of a VST3 audio
plugin binary**. It is written to be dropped into a fresh session together with the artifacts
(the `.vst3` bundle, its installer if needed, a session log, or just a `.vstpreset`).

Three things make this prompt different from a generic "analyze this binary" ask:

1. **Every claim must carry an evidence tier and a locator** (address / export / string / test ID).
   Guesses are labelled, never blended into findings.
2. It enforces the **VST3 anchor chain** pivot: `GetPluginFactory` → `IPluginFactory` vtable →
   `createInstance` → `IAudioProcessor` vtable → `process()`. This is the only reliable path from
   a stripped binary to the DSP code, because the host itself needs exactly that path at runtime.
3. It gates the reimplementation on a **measured null test**, not on "looks equivalent".

Legal scope is part of the prompt, not a footnote — see [Scope](#scope-notes) at the bottom.

**Artifact is an `.exe`?** Start with the companion
[`vst3-reverse-engineering-exe.md`](./vst3-reverse-engineering-exe.md) instead: a Windows `.exe` may
be an installer, a standalone build of the plugin, a packed container, or a loader — and the pivot
changes completely. Use this file once you have the `.vst3`/`.dll` (or for the module case).

---

## 1. Compact version (quick triage, ~15 lines)

> You are a senior reverse engineer and DSP engineer. Target: the VST3 plugin described in INPUTS.
> Work in phases and stop to report after each one. For every claim, give an evidence tier
> (T1 = proven statically from bytes/disassembly, T2 = proven dynamically by observation, T3 =
> inference) plus a locator (RVA/address, export name, string, or test ID). Never invent
> addresses, UUIDs or parameter IDs — write `UNKNOWN` and name the experiment that would resolve
> it. Pivot using the VST3 anchor chain: exported `GetPluginFactory` → `IPluginFactory` vtable
> (`getFactoryInfo`, `countClasses`, `getClassInfo`, `createInstance`) → class UIDs →
> `IAudioProcessor::process` → DSP. Identify the host framework (JUCE / iPlug2 / VST3 SDK / DPF /
> HISE) before naming any function of the plugin's own code. Do not attempt to defeat licensing,
> dongles or code signing; if you meet protection, report what it is and stop. Deliver: (a) bundle
> + binary inventory, (b) class/parameter map, (c) DSP behavior spec derived from impulse, log
> sweep, noise, DC and step tests in a real host, (d) framework/provenance report, (e) faithful
> reimplementation plan with a null-test acceptance criterion. Ask for the missing inputs before
> assuming anything.

---

## 2. Long-form prompt

Fill the `INPUTS` block, then paste everything between the BEGIN/END markers. **Also paste
Appendix A with it** — the prompt cites `§A.2`, `§A.4` etc. for the vtable, `ProcessData` and
`ParameterInfo` layouts, and those sections exist so the agent does not reconstruct them wrongly.
Attach or point the agent at the artifacts; do not paraphrase the phases away (the ordering is the
value — measure before naming).

----- BEGIN PROMPT -----

### ROLE

You are acting as a three-person team in one: a **senior binary reverse engineer** (Windows PE,
MSVC/Clang ABI, vtable & RTTI recovery), a **VST3 SDK expert** (interface layouts, host↔plugin
contract, automation/state semantics), and a **DSP engineer** (system identification, filter and
dynamics topologies, null testing).

### OBJECTIVE

Produce a complete, reproducible specification of the target plugin at four levels, such that a
competent engineer could reimplement its audio path from your report alone:

- **L1 — Inventory:** what the bundle physically is, per file.
- **L2 — Structure:** module exports, factory/class/UID table, parameter and unit tables, state schema.
- **L3 — Behavior:** block-level signal flow, sample-rate/latency/oversampling behavior, per-parameter effect on the transfer function.
- **L4 — Provenance:** host framework, third-party DSP/UI libraries, obfuscation or protection, and what is genuinely the vendor's own code.

### INPUTS

```
TARGET_NAME        : <plugin + version + vendor, exactly as shown in the host>
BUNDLE_PATH        : <path to Foo.vst3 bundle directory>

HAVE               :
  [ ] the bundle (Contents/x86_64-win/Foo.vst3 + moduleinfo.json + Resources/)
  [ ] installer .exe
  [ ] .vstpreset / factory presets
  [ ] a running/hostable copy for dynamic tests (host name: ____)
  [ ] vendor manual / published specs (cite them separately from findings)
  [ ] network access for framework/library fingerprint lookup
  [ ] (if present) a PDB, MAP file, symbol server access, or unstripped variant

HOST_FOR_TESTING   : <REAPER / Cubase / Bitwig / JUCE AudioPluginHost / Carla / SDK validator>
SAMPLE_RATES       : <e.g. 44.1k, 48k, 96k, 192k>
CHANNEL_CONFIGS    : <e.g. mono→stereo, stereo→stereo, sidechain present?>
ALLOWED_TOOLS      : <Ghidra, rizin/r2, capstone, LIEF/pefile, Detect It Easy, x64dbg via Windows VM, Frida, python numpy/scipy>
LEGAL_BASIS        : <interoperability / security review / education / maintenance of purchased license>
DEPTH              : <SURVEY | DEEP | REIMPLEMENT>
```

If, and only if, a missing input blocks a phase, ask one consolidated question. Otherwise proceed
on stated assumptions and flag them in the report header as `ASSUMPTION:`.

### OPERATING RULES (non-negotiable)

1. **Evidence tiers.** Tag every statement:
   - `[T1]` proven from bytes: disassembly, data layout, export table, RTTI, signature.
   - `[T2]` proven by observation: host render, measured IR, log, null residual.
   - `[T3]` inference from naming, strings, framework idiom, or analogy to a known algorithm.
   A finding may only be upgraded, never assumed. `[T3]` items go in a separate "hypotheses"
   table with the experiment that would promote them.
2. **Locators.** Every binary claim carries `module+0xRVA` (preferred) or VA plus image base, and
   the tool that produced it. Every dynamic claim carries a test ID from your test log.
3. **No fabrication.** Unknowns are written `UNKNOWN`. Never guess a UID, parameter ID, address,
   constant, or vendor. A wrong UID silently poisons every later phase.
4. **Framework-first.** Do not name a function until the host framework is identified. In a JUCE
   or iPlug2 build, >80% of the code is framework boilerplate; "found the filter" before you have
   subtracted the framework is almost always a false positive.
5. **Anchor before wander.** Reach the DSP through the VST3 anchor chain (§A.2), not by string
   hunting, unless the chain is broken — then say why.
6. **Measurement over speculation.** Any claim about the audio path that can be settled by one
   render must be settled by that render. Prefer one 10-second test to ten paragraphs.
7. **Stop at protection.** If you find dongle/dongle emulation, license checks, VM obfuscation,
   packed sections, or anti-debug, describe it factually (what, where, how detected) and stop that
   branch. Do not bypass, patch, or key-generate anything.
8. **Determinism.** Record tool versions and `sha256` of every artifact, sample rates and build IDs,
   so every result is reproducible by someone else.
9. **Report after each phase.** Short section, then continue. No 40-page dump at the end with no
   checkpoints.

### PHASE 0 — Intake & triage (no disassembly yet)

- Inventory the bundle tree with file sizes and hashes. Note which file the host actually loads.
  On Windows a `.vst3` is a **bundle directory**, not a file: `Foo.vst3/Contents/x86_64-win/Foo.vst3`
  is the PE DLL. A bare single-file `.vst3` is legal too — say which case this is.
- Parse `moduleinfo.json` if present (SDK ≥ 3.7.0): it lists factory info, class names and **class
  UIDs** in text form. Treat it as `[T1]` for structure, but verify at least one UID against the
  binary, because it can be stale relative to the DLL.
- PE facts: machine (x86 / x64 / ARM64EC), subsystem, linker & rich-header build tools, timestamp,
  ASLR/DEP/CFG flags, section entropy (packing?), Authenticode presence and signer (never modify a
  signed binary), `.pdata` presence (real function boundaries for unwinding), imported DLLs
  (`ilok`, `PACE`, `eLicenser`, `VMProtect`, `WinLicense` imports are red flags).
- `Resources/` contents: presets, `.wav`/`.ir`/wavetable files, images, localization, licence text.
  These are legitimate `[T1]` design hints — an IR file means convolution; a wavetable set means
  table-based oscillators with a known table length.
- Compiler/linker fingerprint (see §A.5) → MSVC vs clang-cl vs GCC/MinGW. This changes every
  downstream calling-convention and vtable assumption.
- Output: **L1 inventory table** + a one-paragraph "what this is" + list of blockers.

### PHASE 1 — Static structure via the anchor chain

Follow §A.2. Deliver, with RVAs:

- Exports: the VST3 module contract is a fixed set — `GetPluginFactory` (mandatory), plus
  `InitDll` / `ExitDll` (Windows module init, may be absent), and `ModuleEntry` / `ModuleExit` in
  newer SDKs. `DllMain`-only plugins exist. Report the exact export list.
- `IPluginFactory` vtable: 3 `FUnknown` slots (`queryInterface`, `addRef`, `release`) then
  `getFactoryInfo`, `countClasses`, `getClassInfo`, `createInstance`. If the vtable order differs,
  either the SDK version is unusual or the binary is tampered with — investigate, don't assume.
- `countClasses` / `getClassInfo` behaviour: static array vs switch vs computed. From it, recover
  **every** class entry: UID, cardinality, category string (`"Audio Module Class"`,
  `"Component Controller Class"`, `"MIDI Effect Class"`, `"Plug Type"`), name, vendor, version,
  SDK version string. Cross-check counts against `moduleinfo.json`.
- `createInstance` dispatch: which branch maps to which UID → the constructor(s) → the vtable(s) of
  `IComponent` / `IAudioProcessor` (often one object) and `IEditController` (often a second object,
  or the same object reached through a `queryInterface` offset).
- Static string harvest, in **both** encodings: parameter titles and units are `String128`, i.e.
  UTF‑16LE `char16[128]`, so an ASCII-only strings pass misses every factory label. Search ASCII
  *and* UTF‑16. Also harvest: state XML element names, unit names, preset category strings,
  error/log strings — these often name internal modules outright (`"oversampler"`, `"ladder"`,
  `"ir_loader"`).
- Switch/table structures that fill `ParameterInfo` (layout in §A.4) → recover the full parameter
  list: id, title, shortTitle, units, stepCount, defaultNormalizedValue, unitId, flags. This is the
  most valuable artifact of Phase 1: the parameter set *is* the feature list, and it is exactly
  what the user needs to build a compatible UI.
- RTTI recovery if present (`.?AV...@@` MSVC type descriptors, `_ZTI` Itanium): class names for
  free. If stripped, match vtable slot order and count-against known SDK interfaces instead.

Output: **L2 structure table** (classes, UIDs, params, units) + the RVA of `process()`,
`getState`/`setState`, `getParameterInfo`, and the audio-thread call tree one level down.

### PHASE 2 — Framework & provenance subtraction

Identify the host framework **before** attributing any code to the vendor:

| Framework | Fingerprints to look for |
|---|---|
| JUCE | `"JUCE v"` version strings, `juce::` RTTI names, `JucePlugin_`/`JUCE_MODULE_AVAILABLE_*` leftovers, `AudioProcessorGraph`, `getStateInformation` XML with a distinctive root tag |
| Steinberg VST3 SDK | `Steinberg::Vst::` names, `gStandardFactory`/`InitDll` shape, SDK version string, `String128`-heavy code, `FDebug`/`TRACE` strings |
| iPlug2 / WDL-OL iPlug | `IPlug`/`IGraphics` symbols, `EIPlug`, `IPlugAPIBase`, leftover `.cpp` path strings |
| DPF (DISTRHO) | `DISTRHO::Plugin`, `d_states`, `DPF` strings, often a GCC/MinGW build fingerprint |
| HISE / Scriptnode | `HISE` strings, embedded Javascript bytecode blobs, `ScriptProcessor` |
| Cabbage / Csound | Csound opcode names, `csound` strings, embedded `.csd` XML |
| Faust / gen~ / Max | generated-code idioms: `fSamplingFreq`, `fminf`/`fmaxf` clamps, no RTTI, no exceptions |
| SynthEdit / GMPI | `SynthEdit` module names, `gmpi` strings |

Then classify every function you touch as **framework**, **third-party library** (FFT, resampler,
denormal guard, `r8brain`, `ipp`, `fftw`, `rubberband`, `elastique`), or **vendor code**. State an
explicit budget: "N functions total → M attributed to framework → K candidates that could hold the
audio path."

Attribution techniques: bindiff/function-hash against a Hello-World plugin you build yourself with
the same framework+SDK version; recurring call-graph motifs (`processBlock`-shaped function calling
`prepareToPlay`-shaped and `reset`-shaped ones); string-table locality.

Output: **L4 provenance table** + the shortlist of vendor functions worth reading, ranked.

### PHASE 3 — Dynamic characterization (the phase most RE reports skip)

Host it and measure. Run the SDK `validator` and/or `pluginval` first: they enumerate buses,
latency and parameters and print the class/param table as text — cheap `[T2]` corroboration of
Phase 1 before you trust your static reading. Then render in a real host with a controlled source.

Minimum test set — name them T01..Tn and log every setting:

- **T01 Latency & tail.** `getLatencySamples()`, `getTailSamples()` (note `kInfiniteTail`).
  *Verify* by finding the first non-zero sample of the IR. Latency commonly changes with sample
  rate and/or oversampling — measure at every rate.
- **T02 Impulse.** Single-sample unit impulse; repeat at −60 dBFS to test level dependence.
  Record IR length, decay shape, pre-ringing (pre-ringing ⇒ linear-phase FIR or upsampled IIR),
  polarity, and time-variance via the double-impulse test.
- **T03 Log sine sweep (Farina).** Yields the harmonic IR ⇒ where saturation sits (pre- or
  post-filter) and distortion order vs frequency. Mandatory for anything marketed as "analog".
- **T04 White / pink noise.** Estimate magnitude response; fit candidate transfer functions and
  report the fitted parameter values, not just "looks like a lowpass".
- **T05 DC & step.** DC offset behaviour, HPF presence, slow settling (⇒ DC blocker / servo).
- **T06 Aliasing / oversampling probe.** Drive a sine near Nyquist and look for folded partials; a
  step change in alias suppression across settings or rates ⇒ internal oversampling (2×/4×/8×).
  Report the factor and, if inferable, linear vs minimum phase.
- **T07 Parameter sweeps.** Per continuous parameter, sweep the full normalized range with a fixed
  signal: is the effect monotonic, what physical quantity does it map to (cutoff in Hz vs
  normalized), what is the mapping curve (lin/log/exp), and is there **smoothing** (per-sample
  glide? block stepping? audible zipper noise?).
- **T08 Automation semantics.** Dense `IParamValueQueue` points mid-block vs at block edges — does
  the plugin interpolate? Do discrete params (`stepCount > 0`) quantize audibly?
- **T09 State & preset diffing.** Diff `getState` blobs across factory presets: this yields the real
  **state schema** (field order, sizes, versioning, XML vs binary, endianness), which usually maps
  1:1 onto the internal DSP object graph. Often more informative than disassembly — do not skip it.
- **T10 CPU scaling.** Time per block vs block size and channel count exposes FFT sizes,
  per-sample vs per-block design, and SIMD width.
- **T11 Bypass & tails.** Does bypass crossfade or hard-switch? Does a reverb tail ring out?
- **T12 Denormal / NaN hygiene.** Silence after loud content: denormal stalls or NaN propagation
  tell you whether a `ScopedNoDenormals` equivalent exists and whether the path is float/double.

Output: **L3 behaviour spec, part 1** — measured numbers only, one row per test, with artifact
paths + `sha256` so anyone can re-derive them.

### PHASE 4 — DSP identification & constant mining

Return to the binary with Phase 3 hypotheses in hand:

- Extract constant pools adjacent to the audio path: filter coefficient tables, window tables
  (Hann/Blackman/Hamming identifiable by their first values), Kaiser β, `tanh`/exp lookup tables,
  `2πf/fs` scalings, denormal thresholds (`1e-15`, `1e-20`), pole clamps (`0.9999`), and known
  topology constants (`0.70710678` = 1/√2, `0.29289322` = 1−1/√2, `1.41421356` = √2).
- Map candidates to topologies: TPT/ZDF SVF, Moog/Huovilainen ladder, Sallen-Key, biquad DF1/DF2T,
  one-pole, comb/allpass, waveguide, FDN/plate/spring reverb, chorus with interpolated delay, tape
  (wow/flutter LFO + hysteresis), compressor with peak/RMS/`tanh` detector, lookahead limiter,
  wavetable/band-limited oscillator (PolyBLEP vs minBLEP vs additive), FFT convolution, transient
  shaper, multiband splitter (LR vs FFT).
- For each block give: identified structure, constants with locators, and confidence tier. Where two
  topologies both fit, **design the discriminating experiment and run it** (e.g. self-oscillation
  and gain-at-cutoff separate ladder variants; a two-tone test separates hard from soft clipping).
- Note SIMD/intrinsics (`mulps`, `vaddps`, `vfmadd*`) and internal precision (float vs double).
  Both matter for reproducing exact behaviour near denormals and at self-oscillation.

Output: **L3 behaviour spec, part 2** — signal flow (text or mermaid), per-block equations or
pseudo-code, constant table with locators.

### PHASE 5 — Reimplementation & the validation gate

Produce a reimplementation scaffold (default C++17 on the VST3 SDK; plain DSP classes if only the
algorithm is wanted) that:

- reuses the recovered **parameter IDs, ranges, defaults, units and stepCount** (§A.4) so existing
  presets/automation map without manual translation wherever the state schema permits;
- recomputes every coefficient in `setupProcessing` from the actual sample rate — no hard-coded `fs`;
- reports the **measured** latency (T01) and reproduces the **measured** oversampling factor (T06);
- includes denormal/NaN guards equivalent to the observed behaviour (T12);
- keeps the original's internal processing block size if T10 shows the behaviour depends on it.

**Acceptance criterion is a null test, not visual inspection of code:**

| Signal | Conditions | Target residual |
|---|---|---|
| Impulse + white noise, linear path, all params default | latency-aligned, gain-compensated only if justified | ≤ −60 dBFS residual for a faithful linear clone |
| Log sweep at −12 dBFS, nonlinear path | latency-aligned | harmonic-IR overlay + stated dB deviation per order |
| Parameter grid | ≥ 8 points per continuous parameter | worst-case residual per point, reported |
| Preset round-trip | all factory presets | state → your impl → state, field-identical or explained |

Rules: align latency **first** (one sample of offset destroys the null); never average away a
failure — report residuals per band and explain any band worse than −40 dB; for time-variant
behaviour (LFO, drift, random modulation) compare statistics (spectra, harmonic IR, distribution)
instead of waveform null, and say so explicitly.

Output: **L3/L4 verified** — reimplementation plan, code scaffold, null-test report.

### PHASE 6 — Consolidate

Final report, these headings in this order:

```
0. Scope, legal basis, hashes, tool versions, assumptions, UNKNOWNs
1. Bundle & binary inventory                              (L1)
2. Module contract: exports, factory, classes, UIDs       (L2)
3. Parameters & units table                               (L2, CSV attached)
4. State / preset schema                                  (L2)
5. Framework & provenance budget                          (L4)
6. Measured behaviour                                     (L3 p1, tests T01..T12)
7. DSP signal flow & equations                            (L3 p2)
8. Protection / obfuscation findings (factual, no bypass)  (L4)
9. Reimplementation plan + null tests                     (L3/L4)
A. Hypotheses with discriminating experiments             (T3 only)
B. Locator index: every RVA cited, grouped by subsystem
C. Reproduction script: the exact commands run, in order
```

Attachments:

- `params.csv` — `id,title,shortTitle,units,stepCount,default,unitId,flags`
- `classes.csv` — `uid,category,name,vendor,version,sdkVersion,vtableRVA`
- `tests.csv` — `testId,signal,sampleRate,channels,params,measuredValue,residual,artifactPath,sha256`

### THINGS THAT WILL MAKE YOU WRONG (read twice)

- Naming a function from a nearby ASCII string. Parameter text is UTF‑16; ASCII hits beside DSP
  code are usually unrelated SDK or log text.
- Assuming one vtable per class. Many plugins share one object for `IComponent` +
  `IAudioProcessor`, and `IEditController` may be the *same* instance reached at an interface
  offset — there may be no second object to find.
- Treating a null test as passed without latency alignment, or on a time-variant plugin.
- Reading `process()` before checking `ProcessData` / `AudioBusBuffers` layout (§A.3): buffers can
  be silence-flagged (and the pointer then non-null but meaningless), and `numSamples` need not
  equal the host block size.
- Forgetting to check for a PDB, map file or public symbol server before spending a day on
  stripped code.
- Assuming the on-disk binary is what runs (installers patch; multi-arch or CPU-dispatched builds
  may load different code).
- Concluding "no DSP inside" because strings are absent — DSP can be fully table-driven.
- Confusing `getState` (component) with `setComponentState`/controller state: they are two different
  blobs with different owners, and presets store both.

----- END PROMPT -----

---

## Appendix A — VST3 facts the agent must not fumble

These are the details that cause most wrong turns in VST3 reverse engineering. They are here so
the agent does not have to rediscover them (and does not "reconstruct" them incorrectly).

### A.1 What is physically on disk (Windows)

```
Foo.vst3/                         <- the "plugin", a directory
├── Contents/
│   ├── x86_64-win/
│   │   └── Foo.vst3              <- PE DLL, the actual code (also arm64-win on ARM builds)
│   ├── Resources/                <- presets, .wav/.ir tables, images, licence text
│   └── moduleinfo.json           <- SDK >= 3.7.0: factory info + class list + UIDs, as text
└── desktop.ini                   <- optional
```

A plain single-file `Foo.vst3` (a DLL renamed) is also valid. Both cases occur in the wild; the
report must say which one it is looking at.

If what you have is the **installer `.exe`**, treat it as a container first, not a target: Inno
Setup, NSIS, MSI and self-extracting archives can usually be unpacked (`innoextract`, `7z x`,
`msiexec /a`) to reveal the real bundle, plus the 32-bit and 64-bit builds, the
`C:\Program Files\Common Files\VST3` install layout, and resources that the installed copy may not
ship. Hash and analyse the *extracted bundle*, and record which build the host actually loads.

### A.2 The anchor chain (the reliable pivot)

```
export GetPluginFactory()
   -> returns IPluginFactory*
      vtable: [0] queryInterface  [1] addRef  [2] release
              [3] getFactoryInfo  [4] countClasses  [5] getClassInfo  [6] createInstance
         -> getClassInfo(i) fills PClassInfo2/PClassInfoW: cid (16-byte UID), cardinality,
            category, name, vendor, version, sdkVersion
         -> createInstance(cid, IComponent::iid, &obj)
            -> IComponent vtable, e.g. [..] initialize, getState/setState, getBusInfo, ...
            -> queryInterface(IAudioProcessor::iid) -> process(), getLatencySamples(),
               setProcessing(), setupProcessing()
            -> queryInterface(IEditController::iid) -> getParameterInfo(), setParamNormalized(),
               createView(), getParamStringByValue()
```

Why it matters: the export table is stripped-proof (the host has to call `GetPluginFactory`), so
this chain works even on fully stripped, RTTI-less, LTO'd binaries where every other symbol is
gone. Everything downstream — DSP, state, UI — hangs off these vtables.

Practical note: vtable slot indices differ between SDK versions as interfaces grow, so read the
**count and order** of slots from the SDK headers of the version the plugin reports, and verify
slot meaning behaviourally (e.g. call the suspected `countClasses` and see if the return value
equals the plausible class count).

### A.3 `process()` argument layout (what to look at first)

`ProcessData` is the entry point for audio. Fields that matter, in order:

- `processMode` (`kRealtime`, `kPrefetch`, `kOffline`) — behaviour may differ per mode;
- `symbolicSampleSize` (`kSample32` / `kSample64`) — the buffers are either `float**` or `double**`
  and the code branches on this; getting it wrong makes every read look like garbage;
- `numSamples` (int32) — may be smaller than the host block;
- `numInputs` / `numOutputs` + `inputs`/`outputs` arrays of `AudioBusBuffers`
  (`numChannels`, `silenceFlags` bitmask, `channelBuffers32/64`);
- `inputParameterChanges` / `outputParameterChanges` (`IParameterChanges` → `getParameterCount` /
  `getParameterData` → `IParamValueQueue` with `getPointCount` / `getPoint(index, &sampleOffset,
  &value)` / `addPoint(...)`: normalized `[0..1]` values with a per-block sample offset);
- `inputEvents` / `outputEvents` (`IEventList`) for MIDI/note expression;
- `processContext` (`ProcessContext`: `state`, `sampleRate`, `projectTimeSamples`, `tempo`,
  `timeSig*`, `continousTimeSamples`, `cycleStart/End`, `kPlaying`/`kCycleActive`/`kTempoValid`
  flags) — tempo-synced DSP reads from here, and that is where LFO sync behaviour comes from.

DSP code usually starts right after these pointers are dereferenced and the `symbolicSampleSize`
branch resolves. That pattern — a two-way branch on `symbolicSampleSize` followed by nested
per-channel loops — is the strongest single signature of an audio path in the whole binary.

### A.4 `ParameterInfo` (the recovered parameter table)

Fields, in declaration order (SDK versions ≥ 3.0; verify against the headers for the reported
version):

- `ParamID id` (uint32)
- `String128 title` — UTF‑16 `char16[128]`
- `String128 shortTitle` — UTF‑16 `char16[128]`
- `String128 units` — UTF‑16 `char16[128]`
- `int32 stepCount` — 0 = continuous; N = N discrete steps (`N+1` values)
- `ParamValue defaultNormalizedValue` (double, `[0..1]`)
- `UnitID unitId` (int32)
- `int32 flags` — `kCanAutomate`, `kIsReadOnly`, `kIsWrapAround`, `kIsList`, `kIsHidden`,
  `kIsProgramChange`, `kIsBypass`

Consequence for the report: values are **normalized doubles**, so the "real" range lives in the
plugin's `plain`↔`normalized` mapping (`getParamStringByValue`, `normalizedParamToPlain` in the
SDK helper). Recovering that mapping for each parameter is what makes a reimplementation usable —
a cutoff knob that is "0.5" is useless; "632 Hz, log-mapped 20 Hz–20 kHz" is the deliverable.

### A.5 Compiler / linker fingerprints

| Clue | Meaning |
|---|---|
| `Rich` header, linker version `14.xx`, `_Init_thread_header`/`__security_cookie`, `__CxxFrameHandler4` | MSVC (version ≈ toolset) |
| `__clang_call_terminate`, `__cxa_*`, markedly different loop vectorisation, no `__CxxFrameHandler4` | clang-cl (MSVC ABI, different codegen idioms) |
| `___chkstk_ms`, `mingw`/`libgcc` imports, `__gxx_personality_seh0`, `_ZTV`/`_ZTI` RTTI | MinGW/GCC |
| `msvcrt` vs `ucrtbase` vs static CRT | runtime linkage, affects which helpers you can ignore |
| Section order `.text .rdata .data .pdata .reloc` + huge `.text` (~MBs) | static CRT + heavy templates (JUCE/iPlug2) |

Fingerprints decide three things: how RTTI looks (`??_7Class@@6B@` vtable symbols vs `_ZTV`),
whether you can trust `.pdata` for function boundaries, and which calling convention to assume for
member functions (`__thiscall` on x86 vs `__fastcall`-ish register passing on x64).

### A.6 Tooling cheatsheet

Static (runs on Linux, no Windows needed):

```bash
# inventory + hashes
find Foo.vst3 -type f -printf '%s\t%p\n' | sort -n
find Foo.vst3 -type f -exec sha256sum {} +
# PE facts / exports / sections  (LIEF >= 0.13; older: b.get_export().entries)
python3 -c "import lief; b=lief.parse('Foo.vst3/Contents/x86_64-win/Foo.vst3'); \
  print(b.header.machine_type); \
  [print(f'{e.name} 0x{e.address:x}') for e in b.exported_functions]"
objdump -p Foo.dll | sed -n '/Export/,/^$/p'   # or: rabin2 -E Foo.dll
# strings, both encodings
strings -a -n 6 Foo.dll | sort -u > ascii.txt
strings -a -e l -n 6 Foo.dll | sort -u > utf16.txt      # String128 titles live here
# RTTI / class names
strings -a Foo.dll | grep -E '^\?AV|^\.AV|^\.\?AV|^_ZTV' | sort -u
# sections / entropy, then Ghidra headless
rabin2 -S Foo.dll ; $GHIDRA/support/analyzeHeadless /tmp/proj vst3 -import Foo.dll
```

Dynamic:

- `validator` (VST3 SDK) and `pluginval` — class/param/bus enumeration, pass/fail logs.
- Host with IR rendering: REAPER (batch render via ReaScript) or the SDK's `audiohost`.
- `Frida` against the host process to hook `process()` and dump `ProcessData` — good for confirming
  field offsets without guessing.
- `numpy`/`scipy` for sweep deconvolution, transfer-function fitting, harmonic-IR extraction.

### A.7 `.vstpreset` files as a spec source

A `.vstpreset` (and the preset chunks inside `getState`) is a chunked container. A 48-byte file
header carries the `VST3` magic, a version, and the class UID as 32 ASCII hex characters; after it
comes a chunk table of 4-character IDs with `int64` offset and `int64` size, and then the data:

- `Info` — metadata (an XML `MetaInfo` block: name, category, vendor, comment, plus optional
  media/attributes entries);
- `Comp` — the component state, i.e. the bytes the plugin wrote in `IComponent::getState`;
- `Cont` — the controller state, i.e. `IEditController::getState`.

Extract and diff `Comp` and `Cont` across factory presets — the byte deltas *are* the internal
parameter/state layout, and the delta positions map onto fields of the DSP object graph. This is
frequently faster than reading `setState` in assembly, and it validates what you read there.
(State blobs may themselves start with an internal chunk header such as `Preset`/`Component` +
version, which tells you the state's own versioning scheme.)

### A.8 Minimum harness for the dynamic tests

1. Non-realtime render in a host, 32-bit float or 64-bit float WAV, plugin in `kOffline`-capable
   mode, block size recorded.
2. Signals: unit impulse; impulse at −60 dBFS; exponential sweep 20 Hz→20 kHz over 10 s at
   −12 dBFS; white noise 30 s; DC step; 1 kHz sine at −3/−12/−30 dBFS; two-tone 1 kHz + 1.1 kHz;
   near-Nyquist sine at `0.45·fs`.
3. For each: record the plugin's reported latency, then align by that latency **and** verify the
   alignment on the impulse before computing any residual.
4. Keep every intermediate file; the report must let someone else re-run the whole set from a
   script.

---

## Scope notes

This prompt is written for **legitimate reverse engineering**: interoperability (making presets,
sessions or automation portable), security review, maintenance of a plugin whose vendor is gone,
academic study, and learning DSP technique. Keep it inside these lines:

- **Do not** use it to defeat licensing, dongles, activation, or code signing. The prompt
  deliberately stops at protection (§ OPERATING RULES 7).
- **Do not** redistribute extracted proprietary code, tables, IRs or assets. Reimplement the
  *behaviour*; ship your own code and your own measurement-derived constants where they are not
  themselves protected expression.
- Jurisdiction matters: the EU Software Directive allows decompilation for interoperability under
  conditions; in the US, *Sega v. Accolade* / *Sony v. Connectix* support intermediate copying for
  interoperability, but anti-circumvention rules (DMCA §1201) still bite. If the output is going to
  production, get the legal basis confirmed by someone qualified, and record it in report §0.
- Cite the vendor manual/spec separately from your findings. A published spec is not evidence about
  the binary; if they disagree, that disagreement is itself a valuable finding.

---

## Variants

- **Only want the algorithm, not the plugin:** drop Phases 1–2, keep Phase 0 (find the DSP entry),
  Phase 3 (measure), Phase 4 (identify) and the Phase 5 null test. That is the fastest path from a
  `.vst3` to a working DSP implementation.
- **Only want compatibility (presets/automation):** keep Phase 1 (`params` + `classes` + state
  schema) and Phase 3's T09 only. Deliver `params.csv` and a state-schema note.
- **Suspected malware / supply-chain review:** add static IOC extraction, imported-API triage,
  network-string review, and a sandboxed dynamic run *before* Phase 3; do not host it on a machine
  with credentials.
- **Your artifact is a Windows `.exe`** (installer, standalone build, packed container, loader):
  use [`vst3-reverse-engineering-exe.md`](./vst3-reverse-engineering-exe.md) — it starts with case
  triage and gives the audio-callback and bindiff recipes this file assumes you do not need.





