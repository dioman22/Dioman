# Model selection troubleshooting

Reference for the error:

```
Error: (success) There's an issue with the selected model (deepseek/deepseek-v4.1-flash).
It may not exist or you may not have access to it.
```

All values below were verified live against the providers on 2026-10-25.

## Short answer

Both IDs name the same model, which exists and is live:

- `deepseek/deepseek-v4.1-flash` — callable through the Cline gateway **and** OpenRouter. Works.
- `cline-cloud/deepseek-v4.1-flash` — a **gate-locked alias** served only to Cline product
  surfaces. Returns `403` everywhere else.

So the error never means "this model does not exist" in the literal sense. It means the request
used an ID the target endpoint will not authorize for the caller, which happens when:

1. the ID is a `cline-cloud/*` alias and the client is not a current Cline product surface, or
2. the ID's namespace does not match the selected provider (OpenRouter-shaped slug against the
   DeepSeek direct API), or
3. the client's catalog predates the model and the gateway answers `404 model not found`.

The trailing `(success)` is a client rendering artifact of the gateway response envelope
(`{"error": ..., "success": false}`). It does not mean the call succeeded.

## Correct model ID per provider

| Provider        | Base URL                        | Correct model ID                   |
| --------------- | ------------------------------- | ---------------------------------- |
| Cline           | `https://api.cline.bot/api/v1`  | `deepseek/deepseek-v4.1-flash`     |
| Cline (pinned)  | `https://api.cline.bot/api/v1`  | `deepseek/deepseek-v4.1-flash-1017`|
| Cline (latest)  | `https://api.cline.bot/api/v1`  | `~deepseek/deepseek-flash-latest`  |
| OpenRouter      | `https://openrouter.ai/api/v1`  | `deepseek/deepseek-v4.1-flash`     |
| DeepSeek direct | `https://api.deepseek.com`      | `deepseek-flash`                   |
| DeepSeek direct | `https://api.deepseek.com`      | `deepseek-v4-pro`                  |

Notes:

- The Cline gateway requires the `vendor/model` slash form. A bare `deepseek-flash` is rejected.
- The `~` prefix on `~deepseek/deepseek-flash-latest` is part of the real ID, not a UI marker.
  The same string without `~` is rejected as an invalid model ID.
- `cline-cloud/<model>` IDs (for example `cline-cloud/deepseek-v4.1-flash`) are served only to
  Cline product surfaces. Calling them from a script or an older client returns 403.
- DeepSeek's own API retired `deepseek-v4-flash` and `deepseek-v4-flash-vision-exp`; those names
  still route to V4.1 Flash there for compatibility. On the **Cline gateway**,
  `deepseek/deepseek-v4-flash` is instead product-surface gated (`403`), so do not use it as a
  fallback.

## When the error names `cline-cloud/…`

`cline-cloud/*` is a **restricted catalog namespace**. The gateway serves it only to Cline's own
product clients and answers everything else with a deterministic `403`:

```
cline-cloud/deepseek-v4.1-flash
→ 403 {"error":{"code":"API_REQUEST_ERROR_CODE",
        "message":"Error 403: cline-cloud/deepseek-v4.1-flash is only available via
                   Cline product surfaces. If you are using an old version of Cline,
                   please update to the latest version"}}
```

Reproduced 3/3 with no variation, while `deepseek/deepseek-v4.1-flash` returned `200` 3/3 in the
same loop. So this is a **policy gate on the namespace, not a missing model** — the same weights
are reachable, just under the `deepseek/` name instead.

The client buckets both `403` (no access) and `404` (not found) into the single message
"It may not exist or you may not have access to it", which is why a gate and a typo look
identical in the UI.

Practical consequence: a `cline-cloud/*` selection only works inside a current Cline build that
authenticates as a product surface. Anywhere else — scripts, older builds, third-party clients —
you must select the `deepseek/*` equivalent.

## Observed failure modes

| Request                                           | Result                                                          |
| ------------------------------------------------- | --------------------------------------------------------------- |
| `deepseek/deepseek-v4.1-flash`                    | `200` — works, 3/3                                              |
| `deepseek/deepseek-v4.1-flash-1017`               | `200` — works, 3/3 (Fireworks upstream)                         |
| `~deepseek/deepseek-flash-latest`                 | `200` — works                                                   |
| `deepseek/deepseek-v4-pro`                        | `200` — works                                                   |
| `~deepseek/deepseek-pro-latest`                   | `200` — works                                                   |
| `deepseek/deepseek-v3.2`                          | `200` — works                                                   |
| `deepseek/does-not-exist`                         | `404 {"error":"model not found","success":false}`               |
| `nonexistent/model-xyz`                           | `404 {"error":"model not found","success":false}`               |
| `deepseek-flash` (on the Cline gateway)           | `400 invalid model format. Expected format: modelType/model`     |
| `deepseek/deepseek-flash-latest` (no `~`)         | `400 ... is not a valid model ID`                                |
| `deepseek/deepseek-v4.1-flash:batch`              | `500 inference request failed ... from Openrouter`              |
| `deepseek/deepseek-v4-flash`                      | `403 ... only available via Cline product surfaces`             |
| `cline-cloud/deepseek-v4.1-flash`                 | `403 ... only available via Cline product surfaces`, 3/3        |

The `404 model not found` row and the `403 ... product surfaces` row are both turned into
"It may not exist or you may not have access to it."

### Picking a model that actually authenticates

```
Is the ID a cline-cloud/* one?
├─ yes → it is gate-locked. Swap to the deepseek/* equivalent:
│         cline-cloud/deepseek-v4.1-flash → deepseek/deepseek-v4.1-flash
│         (or update the Cline client so it authenticates as a product surface)
└─ no  → does the gateway list it?   curl https://api.cline.bot/api/v1/models
          ├─ no  → stale client catalog: update / refresh the client
          └─ yes → is it 403? entitlement (ClinePass/credits) — check the billing message
                   is it 200? then the failing call is using a different provider base URL,
                             most often DeepSeek direct, which needs `deepseek-flash`
```

## Fix checklist

1. **Match the ID to the active provider** using the table above. The most common cause is an
   OpenRouter-shaped ID (`vendor/model`) left in place after switching the provider to DeepSeek
   direct, which needs `deepseek-flash` instead.
2. **Update the Cline client.** The gateway answers `403 ... If you are using an old version of
   Cline, please update to the latest version` for recently added models. V4.1 Flash landed on
   2026-09-10 and the `-1017` snapshot on 2026-10-17, so builds older than either date will not
   have it in their catalog.
3. **Refresh the model catalog** (or reload the window) if the picker was populated before the
   model was added. A stale catalog produces the same 404.
4. **Pin the snapshot** with `deepseek/deepseek-v4.1-flash-1017` if you need deterministic
   behaviour; the un-suffixed ID floats to whatever the provider serves as current.
5. **Check entitlement separately.** ClinePass and free-promotion problems have their own distinct
   messages ("No access to ClinePass subscription models yet …", "free limit reached on model …",
   "you have reached your … clinepass limit …"). Seeing one of those is a billing issue, not a
   model-ID issue.

## Using the model outside Cline

The Cline gateway is OpenAI-compatible only — `POST https://api.cline.bot/api/v1/messages` and every
other Anthropic path return `404`, so Anthropic-protocol clients (Claude Code, the Anthropic SDK)
cannot use it directly, and no `cline-cloud/*` id will help.

For Claude Code there are two options:

- **A hosted Anthropic-compatible gateway** (any base URL that serves `/v1/messages`) — speaks the
  Anthropic protocol directly, so `settings.json` alone is enough and **no bridge is needed**. Billing
  goes to that gateway, not Cline.
- **Cline's model via a local bridge** — `scripts/anthropic_cline_proxy.py` translates Anthropic
  `/v1/messages` to the gateway's `/chat/completions`. Needed only if you want Cline's routing and
  balance; there is no Anthropic endpoint on `api.cline.bot` to point at.

Setup for both, plus verification steps: [`claude-code-setup.md`](./claude-code-setup.md).

For **Cline itself** on DeepSeek: no bridge and no third-party gateway — use Cline's built-in
**DeepSeek** provider, or `OpenAI Compatible` pointed straight at `https://api.deepseek.com`:
[`cline-deepseek.md`](./cline-deepseek.md).

## Reproducing the checks

```bash
# List every model the Cline gateway offers
curl -s https://api.cline.bot/api/v1/models \
  | python3 -c "import sys,json;print('\n'.join(sorted(m['id'] for m in json.load(sys.stdin)['data'] if 'deepseek' in m['id'])))"

# Confirm a specific ID resolves
curl -s -o /dev/null -w '%{http_code}\n' -X POST https://api.cline.bot/api/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"model":"deepseek/deepseek-v4.1-flash","messages":[{"role":"user","content":"say ok"}],"max_tokens":64}'
```

Use `max_tokens` of at least 64. With a very small budget the thinking-enabled model returns
`500 {"error":"empty response content"}` even though the model resolved correctly.
