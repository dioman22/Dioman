# Model selection troubleshooting

Reference for the error:

```
Error: (success) There's an issue with the selected model (deepseek/deepseek-v4.1-flash).
It may not exist or you may not have access to it.
```

All values below were verified live against the providers on 2026-10-25.

## Short answer

`deepseek/deepseek-v4.1-flash` **is a real, working model ID**. It is not a typo and it is not
retired. The error means the request reached a provider that does not resolve *that exact
string* for the provider you have selected — either because a different provider was active,
or because the client build is older than the model.

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
  still route to V4.1 Flash for compatibility but are gated the same way.

## Observed failure modes

| Request                                           | Result                                                          |
| ------------------------------------------------- | --------------------------------------------------------------- |
| `deepseek/deepseek-v4.1-flash`                    | `200` — works                                                   |
| `deepseek/deepseek-v4.1-flash-1017`               | `200` — works (Fireworks upstream)                              |
| `~deepseek/deepseek-flash-latest`                 | `200` — works                                                   |
| `deepseek/does-not-exist`                         | `404 {"error":"model not found","success":false}`               |
| `nonexistent/model-xyz`                           | `404 {"error":"model not found","success":false}`               |
| `deepseek-flash` (on the Cline gateway)           | `400 invalid model format. Expected format: modelType/model`     |
| `deepseek/deepseek-flash-latest` (no `~`)         | `400 ... is not a valid model ID`                                |
| `deepseek/deepseek-v4-flash`                      | `403 ... only available via Cline product surfaces`             |
| `cline-cloud/deepseek-v4.1-flash`                 | `403 ... only available via Cline product surfaces`             |

The `404 model not found` row is the one the client turns into
"It may not exist or you may not have access to it."

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
