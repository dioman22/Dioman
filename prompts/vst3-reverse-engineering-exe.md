# Prompt: Reverse Engineering a VST3 Plugin Delivered as a Windows `.exe`

Companion to [`vst3-reverse-engineering.md`](./vst3-reverse-engineering.md). Use **this** file when
the artifact you actually have is an `.exe`, because an `.exe` on Windows is one of four very
different things and each one has a different pivot:

| Case | What it is | The pivot |
|---|---|---|
| **A** | **Installer** (Inno / NSIS / MSI / InstallShield / self-extracting) | Extract the payload, then use the main VST3 prompt on the extracted bundle |
| **B** | **Standalone build** of the plugin (own window, audio-device settings) | Hook the audio render callback → DSP. Often *easier* than the VST3 DLL: no vtable chain, and you can bindiff it against the DLL |
| **C** | **Packed / self-extracting** exe that drops the plugin on run | Unpack, recover the embedded PE(s), then A or B |
| **D** | `.exe` that **is** the plugin, or a loader stub for it | Check exports for `GetPluginFactory` → main VST3 prompt; or trace the drop/load |

Getting the case right first is the whole point: people waste days disassembling an Inno Setup
bootstrap, or hunting a VST3 factory in a standalone app that contains no VST3 code at all.

Legal scope is the same as the main prompt — see its **Scope notes**. Nothing here covers defeating
activation, dongles, or signing.

---

## 1. Compact version

> You are a senior Windows reverse engineer and DSP engineer. The target is a VST3-related `.exe`
> described in INPUTS. **Step 0 is case triage**: decide whether it is (A) an installer, (B) a
> standalone app build of the plugin, (C) a packed/self-extracting container, or (D) the plugin
> itself / a loader — using PE subsystem, exports (`GetPluginFactory`?), imports (WASAPI/ASIO/DSound
> vs. installer runtime), resources and signature strings. Justify the case with evidence, then
> follow only that case's path. For (A)/(C): extract the payload, hash it, and hand the extracted
> bundle to the standard VST3 workflow. For (B): recover DSP by breakpointing the audio callback
> (`IAudioRenderClient::GetBuffer`/`ReleaseBuffer` for WASAPI, `bufferSwitchTimeInfo`/`ASIOGetChannels`
> for ASIO) and bindiffing the exe against the VST3 DLL if available (Ghidra Version Tracking), so
> you inherit names from the DLL instead of naming functions blind. Tag every claim T1/T2/T3 with a
> locator (RVA, export, string, test ID). Never invent addresses, UIDs, parameter IDs or vendors.
> Stop and report at any protection/packing you cannot legitimately describe. Ask for missing inputs
> before assuming. Deliver: case verdict + evidence, inventory, DSP map with measured behaviour, and
> a reimplementation plan gated on a null test.

---

## 2. Long-form prompt

Fill `INPUTS`, then paste everything between the markers, plus **Appendix B** (exe-specific
commands) and **Appendix A of the main prompt** (VST3 structures) if the case turns out to involve
real VST3 code.

----- BEGIN PROMPT -----

### ROLE

You are a senior **Windows RE engineer** (PE format, installer runtimes, Win32 audio APIs,
MSVC/Clang ABI, bindiffing) and a **DSP engineer**. You are rigorous about which artifact you are
actually looking at: a distribution container, an application, or the plugin module.

### TARGET

```
EXE_PATH        : <path to the .exe>
SHA256          : <hash>
REPORTED_AS     : <what the user calls it: "installer" / "standalone" / "the VST3" / "unknown">
COMPANIONS      : <do you also have the .vst3 bundle or .dll of the same product+version? Y/N + path>
HOST_TOOLING    : <x64dbg / WinDbg / Frida / API Monitor / Ghidra / IDA / BinDiff / VM snapshot>
OS_ENV          : <Windows version, VM or bare metal, network on/off>
DEPTH           : <SURVEY | DEEP | REIMPLEMENT>
LEGAL_BASIS     : <interoperability / security review / maintenance of purchased license>
```

### STEP 0 — Case triage (mandatory, before any disassembly)

Decide **A / B / C / D** and justify with at least three independent pieces of evidence; §B.1 has
the exact checks. Decision cues:

- Exports include `GetPluginFactory` / `InitDll` → it is (or contains) a **VST3 module** → **D**.
- Imports/references to `mmdevapi`/`audioses`/`IAudioRenderClient`, `wdmaud`, `avrt`, `dsound`,
  `portaudio`, `rtaudio`, `ASIO`, plus a GUI subsystem and a settings path under `%APPDATA%` →
  **B** (standalone application).
- Inno Setup / NSIS / InstallShield / MSI custom-action strings, or an SFX stub (WinRAR/7-Zip
  `Setup=` resource, `.ndata`), and a huge overlay or resource section holding a compressed
  payload → **A**.
- High-entropy sections, `UPX0`/`UPX1`, Themida/VMProtect/Enigma markers, or an `MZ` header inside
  the overlay → **C**. (Do not attempt to defeat anti-tamper; report it and stop that branch.)
- A tiny exe (tens of KB) that writes to `%TEMP%`/`%PROGRAMFILES%` and spawns another process →
  **D (loader)**; trace the drop, then re-enter triage on the dropped file.

State the verdict as: `CASE: B — standalone (evidence: imports mmdevapi+avrt, GUI subsystem, no
GetPluginFactory export, settings dir "%APPDATA%\Vendor\Product", RVA 0x…)`. If the evidence
conflicts, list the discriminating test you ran.

### RULES (inherited, all cases)

1. **Evidence tiers + locators**, exactly as in the main prompt: `[T1]` bytes/disassembly, `[T2]`
   observation, `[T3]` inference; binary claims carry `module+0xRVA`; dynamic claims carry a test ID
   and the exact command/hook used.
2. **No fabrication** of addresses, UIDs, parameter IDs, vendor names or API names. `UNKNOWN` + the
   experiment that resolves it.
3. **Never analyse a container as if it were code.** For A/C the deliverable of Step 1 is the
   extracted payload + hashes, not a disassembly of a bootstrap.
4. **Record the runtime environment** for anything dynamic (OS build, driver, buffer size, sample
   rate, device) — standalone behaviour depends on the audio device.
5. **Stop at protection.** Packing / anti-debug / activation: describe, locate, stop. No bypass.
6. **Report per step.** Verdict, then inventory, then DSP, then measurements.

### STEP A — Installer / distribution container

Goal: get the *real* payload out, with hashes, and only then analyse code.

1. Identify the runtime: Inno Setup, NSIS, InstallShield, MSI, WiX, WinRAR/7-Zip SFX, or a custom
   downloader. §B.2 gives the extraction command per runtime.
2. Extract to a clean directory; list every file with size and `sha256`. Expect: the VST3 bundle,
   possibly VST2/AAX/CLAP variants, a standalone exe, presets, licence text, and a VC++ runtime
   redistributable (ignore the redist — it is Microsoft's, `[T1]` identifiable by its own signature).
3. Confirm which file the host actually loads (the `.vst3` under
   `C:\Program Files\Common Files\VST3`, or a per-user path). Diff an installed machine against a
   pre-install snapshot (`Procmon` bootlog or `regshot`) if the installer does more than copy.
4. Flag installers that **download** the payload at install time (no payload in the container): the
   blocker is network retrieval; record the URL and stop, or capture it with `mitmproxy` if allowed.
5. Hand the extracted bundle to the **main prompt** (Phase 0 onward). Output: `payload.csv`
   (`path,size,sha256,type,loads_in_host?`).

### STEP B — Standalone application build (the interesting case)

The standalone usually wraps the *same* DSP library as the plugin, so the exe is often the *easier*
target: no host needed, no factory/vtable chain, and its own settings files expose the parameter
list. Work in this order.

**B1. Shell / framework.** Find the entry (`main`/`WinMain`/`wWinMain` → CRT init → framework).
Fingerprint the app shell: JUCE (`JUCEApplication::main`, `StandalonePluginHolder`), Qt
(`QApplication`, `qt_` strings), wxWidgets, Steinberg's `AudioHost`, or the vendor's own. If it is a
generic framework wrapper, that wrapper is *known code* — the DSP lives behind a
`processBlock`-shaped virtual call into the app's own audio object, which is a much smaller haystack
than the whole exe.

**B2. Device layer.** Identify how audio is moved: WASAPI (`IMMDeviceEnumerator` →
`IAudioClient::Initialize` → `IAudioRenderClient::GetBuffer`/`ReleaseBuffer`, optional
`IAudioCaptureClient`), ASIO (driver DLL from `HKLM\SOFTWARE\ASIO`, `ASIOInit`, then the driver calls
back into `bufferSwitch`/`bufferSwitchTimeInfo`), DirectSound, kernel streaming, or a portable
library (`portaudio`, `rtaudio`). Record it as `[T1]` with the import table as the locator.

**B3. Render callback → DSP.** Pick the shortest breakpoint that fires once per block:

- WASAPI: break on `IAudioRenderClient::ReleaseBuffer`, then walk *back* to the routine that filled
  `pData`; or set a hardware write breakpoint on the returned buffer from `GetBuffer` and take the
  first writer's stack. The DSP loop is almost always 1–2 frames below it.
- ASIO: break in `bufferSwitchTimeInfo`; the output `ASIOBufferInfo**` addresses are the
  post-DSP data, and the callee chain down from the callback is the audio path.
- JUCE standalone: find the app's `AudioProcessor` implementation vtable and read its
  `processBlock` slot directly (the wrapper is public code, so slot order is known).
- No debugger available: `Frida` `Interceptor.attach` on `ReleaseBuffer` +
  `MemoryAccessMonitor` on the buffer range gives writer stack traces; `API Monitor` does the same
  with less scripting.

**B4. Parameter surface without VST3.** Enumerate the settings store: `%APPDATA%\<Vendor>\<Product>\`
(XML/JSON/ini/binary), `HKCU\Software\<Vendor>\<Product>`, and UI control IDs in the resource
section. Diff the file before/after moving each knob to map **UI control → settings field →
normalized range → physical range**. This replaces `getParameterInfo` and is usually `[T1]`-grade
evidence about names and defaults.

**B5. Bindiff against the plugin module (highest-leverage step).** If the same product+version is
available as a `.vst3`/`.dll`:

- Ghidra **Version Tracking**: create a session, add both programs, run correlators
  (`Exact Function Bytes`, `Exact Mnemonics`, `Structural`, `Data Reference`), then review
  unmatched vendors; or `diaphora` (IDA) / `BinDiff`.
- Exact-byte correlation typically matches hundreds of functions even across build configurations;
  small leaf DSP functions (filters, saturators, table lookups) survive best. Propagate names from
  the module to the exe, then read the DSP with real names instead of `FUN_1400xxxx`.
- Expect divergence: different optimisation flags/LTO change inlining and block size, the wrapper
  adds code, and the plugin may select different code paths (latency compensation, automation
  smoothing) than the standalone.

**B6. Measurement in a standalone.** No automation host, so:

- Capture output via WASAPI **loopback** (`ffmpeg -f dshow -i audio="<loopback device>"`, Audacity
  loopback, or a virtual cable). Capture input the same way if the plugin has an input.
- Record device period (= block size, commonly 441/480/512) and sample rate; report them, because
  block-size-dependent behaviour is common and it differs from the VST3 host case.
- Set parameters through the UI or by editing the settings file between renders; log the exact
  field values, not "knob at 12 o'clock".
- Then run the main prompt's tests **T01–T12** with these caveats: latency includes the device
  buffer (measure, don't trust the UI), there is no offline/faster-than-realtime mode, and
  time-variance from a live device clock makes long null tests noisier — prefer short, deterministic
  renders and repeat them.

**B7. Report the deltas.** Explicitly state which behaviours you verified in the standalone and
which can only be verified in a VST3 host (automation semantics T08, `setProcessing`/bypass T11,
state round-trip T09).

### STEP C — Packed / self-extracting container

1. Characterise the packing objectively: section entropy, section names (`UPX0`/`UPX1`/`.themida`),
   import table anomalies (single `LoadLibrary`/`GetProcAddress`), `DIE`/`diec` output. `[T1]` only —
   "looks packed" is not a finding.
2. Separate **self-extraction** (legitimate delivery) from **protection** (anti-analysis). Only the
   first is in scope: an SFX is just an archive with a stub, so `7z l`/`7z x` or a VM run with
   `Procmon` will hand you the dropped files.
3. If a payload is genuinely packed/obfuscated: locate the real entry after unpacking *in memory* if
   that is legitimate for your legal basis (ProcDump `-mp`, then fix imports with
   `Scylla` + `IAT` repair), and **say that you did it**. If the protection is an anti-tamper
   product, stop and report per Rules §5.
4. Scan for embedded PEs regardless: walk the overlay for `MZ` with a valid `e_lfanew` + `PE\0\0`
   and record offsets/hashes (§B.3). A "self-contained installer" often holds 2–5 payloads
   (x86, x64, VST2, AAX, standalone).
5. Re-enter triage (Step 0) on every recovered PE. Output: `embedded.csv`
   (`fileOffset,size,sha256,machine,verdict`).

### STEP D — The exe *is* the module, or is a loader

**D-if-module:** if the export table exposes `GetPluginFactory`, the exe is a VST3 module in exe
clothing (or a dual-purpose binary). Two consequences:

- The main prompt applies verbatim; the anchor chain still works because the export is callable.
- You can **call it as a library** instead of disassembling: copy the exe to a `.dll` name (or use
  `LoadLibraryEx` with a full path) in a scratch VM, `GetProcAddress(h, "GetPluginFactory")`, then
  enumerate `countClasses`/`getClassInfo` and every parameter via `getParameterInfo` — a ~60-line
  host gives you the whole `classes.csv` and `params.csv` without touching assembly. This is the
  single fastest evidence source in the entire workflow; do it before reading code. Caveats: the
  image must be relocatable/mappable as a DLL (typically fine), TLS callbacks and CRT init still
  run, and a protected binary may refuse — if so, say so and fall back to static analysis.
- Then `createInstance` → `IAudioProcessor::process` for the DSP entry.

**D-if-loader:** trace file and process events (`Procmon`: `Process Create`, `CreateFile` with
`WriteFile`, `RegSetValue`) to capture what it drops and where; hash the dropped files; re-enter
triage on each. Also check whether it simply launches an installed copy (no payload → nothing to
reverse; report that).

### DELIVERABLES (this prompt)

1. **Case verdict + evidence** (three independent items).
2. **Inventory**: `payload.csv` or `embedded.csv`, plus which file the host loads.
3. **Structure**: for a module/standalone, the DSP entry locator, the device-layer locator, and the
   parameter table with its source (VST3 `getParameterInfo` **or** settings-file diff).
4. **Behaviour**: main-prompt tests with the standalone caveats (B6/B7).
5. **Provenance**: shell framework, device API, third-party libraries, protection (factual).
6. **Reimplementation plan** + null-test table from the main prompt's Phase 5.
7. **Reproduction**: exact commands, hook scripts, VM/OS/driver/device and block-size notes.

### ANTI-PATTERNS (exe-specific)

- Disassembling the installer's bootstrap instead of extracting the payload.
- Assuming the standalone's DSP equals the plugin's: verify with a null test, and check whether the
  standalone applies extra smoothing, limiting, or a different oversampling setting.
- Trusting a UI label as a parameter value: read the bound settings field.
- Measuring latency from the app's reported device latency instead of the measured first non-zero
  sample — the exe path usually adds its own buffering on top.
- Treating `MZ` hits inside a resource/overlay as a function of the host exe.
- Loading an exe as a DLL from a path with dependencies present (`LoadLibrary` search order) —
  always use a full path in an isolated VM.
- Reporting "UPX" as a finding without unpacking, or unpacking when the section names were spoofed.

----- END PROMPT -----

---

## Appendix B — exe-specific commands and recipes

### B.1 Triage: what is this exe?

```bash
# Everything here runs on Linux; no Windows needed for Step 0.
rabin2 -I app.exe                      # machine, subsystem (GUI/CUI), NX, pic, canary
rabin2 -E app.exe | head -40           # exports -> GetPluginFactory? InitDll?
rabin2 -i app.exe | grep -iE 'mmdevapi|audioses|avrt|wdmaud|dsound|asio|portaudio|rtaudio|waveOut'
rabin2 -S app.exe                      # sections + virtual size; UPX0/UPX1 or absurd vSize = packed
diec app.exe                           # Detect It Easy CLI: compiler, packer, protector
strings -a -n 8 app.exe | grep -iE 'inno setup|nullsoft|installshield|7-zip|winrar|setup=' | head
binwalk -E app.exe                     # entropy plot: a flat high plateau is a payload blob
```

Decision: exports → **D**; audio-API imports + GUI subsystem → **B**; installer strings + big
resource/overlay → **A**; high entropy / protector names / embedded `MZ` → **C**.

### B.2 Installer extraction, by runtime

```bash
innoextract -l setup.exe && innoextract -d out setup.exe     # Inno Setup (recent builds may need
                                                             # a VM run + Procmon if unsupported)
7z x setup.exe -onsis_out                                    # NSIS
7z x setup.exe                                               # WinRAR / 7-Zip SFX
7z x setup.exe -odark_out                                    # WiX Burn / some InstallShield
msiextract pkg.msi -C msi_out                                # MSI on Linux (msitools)
# msiexec /a pkg.msi /qb TARGETDIR=C:\out                    # MSI on Windows (administrative install)
unshield x data1.cab -d is_out                               # legacy InstallShield CAB
```

For anything custom or unsupported: run it in a **clean VM snapshot**, capture with `Procmon`
(boot logging + filter `WriteFile`/`CreateFile`/`RegSetValue`), export the file events, hash
everything written. That method also exposes downloader installers (network-only payload), which you
must report as a blocker rather than chase.

### B.3 Find embedded PEs (payload recovery)

```python
#!/usr/bin/env python3
# scan.py: list embedded PE candidates in an exe/overlay
import sys
data = open(sys.argv[1], "rb").read()
i, hits = 0, 0
while True:
    i = data.find(b"MZ", i)
    if i < 0:
        break
    if i + 0x40 <= len(data):
        e_lfanew = int.from_bytes(data[i + 0x3C:i + 0x40], "little")
        if 0 < e_lfanew < 0x1000 and data[i + e_lfanew:i + e_lfanew + 4] == b"PE\0\0":
            print(f"offset=0x{i:x} e_lfanew=0x{e_lfanew:x}")
            hits += 1
    i += 1
print(f"{hits} candidate(s)")
```

Cross-check each candidate with `rabin2 -I @0xOFFSET` / carve it out and hash it. Report offsets, not
just "contains a PE".

### B.4 Hooking the render path (standalone case)

WASAPI is COM, so there are no importable `GetBuffer`/`ReleaseBuffer` symbols — hook the **vtable**
after the interface is created. Practical route:

1. Break `ole32!CoCreateInstance` / `combase!CoCreateInstance`; when `rclsid` is the
   **MMDeviceEnumerator** CLSID (`{BCDE0395-E52F-467C-8E3D-C4579291692E}`), note the returned
   `ppv` in the out-parameter / `rax`.
2. On that object, set a breakpoint on the vtable slot for `IMMDevice::Activate`, then on the
   returned `IAudioClient` set one on `GetService` (the slot that yields `IAudioRenderClient`).
3. On the `IAudioRenderClient`, break `GetBuffer` (get the pointer) and `ReleaseBuffer` (the buffer
   has just been filled). Set a hardware **write** breakpoint on the buffer range from `GetBuffer`
   to get the first writer's stack trace — that is the DSP top level.

x64 vtable byte offsets (index × 8, after the 3 `IUnknown` slots — **verify against your SDK's
`mmdeviceapi.h`/`audioclient.h`** for the target Windows version):

| Interface | Method | Byte offset |
|---|---|---|
| `IAudioClient` | `Initialize` | 0x18 |
| `IAudioClient` | `GetBufferSize` | 0x20 |
| `IAudioClient` | `GetCurrentPadding` | 0x30 |
| `IAudioClient` | `Start` | 0x50 |
| `IAudioClient` | `GetService` | 0x70 |
| `IAudioRenderClient` | `GetBuffer` | 0x18 |
| `IAudioRenderClient` | `ReleaseBuffer` | 0x20 |

ASIO is easier: the *host* supplies `ASIOCallbacks` (four function pointers: `bufferSwitch`,
`sampleRateDidChange`, `asioMessage`, `bufferSwitchTimeInfo`) to `ASIOInit`. Break `ASIOInit`,
read the callbacks struct out of the arguments, and you have the render callback addresses in app
code directly — then the DSP is the callee chain under `bufferSwitchTimeInfo`.

Thread identification: `avrt!AvSetMmThreadCharacteristicsW` is called once when the audio thread
starts; breaking there labels the thread so later breakpoints can be filtered to it (avoids
hitting the same code from the UI thread).

Static alternative if you cannot attach a debugger: search the disassembly for the vtable-call
pattern of two consecutive indirect calls on the same object with offsets `[reg+0x18]` then
`[reg+0x20]` (GetBuffer → ReleaseBuffer) inside a loop, and follow the loop body backwards.

### B.5 Settings-store diff (parameter surface without VST3)

```bat
:: Windows. Snapshot, move exactly one knob in the UI, snapshot again, diff.
robocopy "%APPDATA%\Vendor\Product" "%TEMP%\snap\before" /E /NFL /NDL /NJH /NJS >nul
reg export HKCU\Software\Vendor "%TEMP%\snap\before.reg" /y

:: ... move one control in the standalone UI, close the app so it flushes settings ...

robocopy "%APPDATA%\Vendor\Product" "%TEMP%\snap\after"  /E /NFL /NDL /NJH /NJS >nul
reg export HKCU\Software\Vendor "%TEMP%\snap\after.reg" /y

fc /b "%TEMP%\snap\before\settings.xml" "%TEMP%\snap\after\settings.xml"
```

Record one row per control: `uiLabel, settingsKey, storedValue, min, max, default, curve`. Where the
store is binary, diff bytes and treat the changing field offsets as the state schema (same idea as
the `.vstpreset` trick in the main prompt's §A.7).

### B.6 Bindiff workflow (exe ↔ plugin module)

1. Import both into one Ghidra project; analyse both fully (auto-analysis must complete on each
   before version tracking).
2. `Version Tracking` → new session → add source (module, with any recovered names) and destination
   (exe) → run correlators in order: `Exact Function Bytes`, `Exact Function Instructions`,
   `Exact Data`, `Function Reference`, `Structural`.
3. Accept confident matches in bulk, review ambiguous ones individually, then **apply names from
   the source to the destination** and use the destination for the DSP reading (its call sites from
   the wrapper are what you actually need).
4. Report the match rate and, crucially, the **unmatched functions with audio-path relevance** —
   those are where the standalone differs from the plugin (extra smoothing, metering, device
   resampling).

---

## Scope

Same as the main prompt: interoperability, security review, maintenance of a licensed product,
study. Extracting an installer's payload is normal for all of these; defeating activation,
anti-tamper or code signing is out of scope, and this prompt makes the agent stop and report
instead. If the output is going to production, record the legal basis in the report header.

## Which file to use

- Artifact is an `.exe` → **this file** (Step 0 first, always).
- Artifact is a `.vst3` bundle, `.dll`, or you have already extracted the payload → the
  [main prompt](./vst3-reverse-engineering.md).
- Both, same product+version → run the main prompt on the module and use this file's §B.6 bindiff to
  transfer names into the exe. That combination is the fastest route to a complete picture.





