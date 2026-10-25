# Cline's DeepSeek model inside Claude Code

Goal: run the **Claude Code** CLI on DeepSeek (V4.1 Flash) instead of Anthropic's models.

There are two ways, and only one of them needs a bridge.

## Option A — any hosted Anthropic-compatible gateway (no bridge)

If a gateway speaks the Anthropic Messages protocol itself, you only need `settings.json`. No proxy,
no local process. Shape it like this:

```json
{
  "hasCompletedOnboarding": true,
  "env": {
    "ANTHROPIC_BASE_URL": "https://your-gateway.example/v1",
    "ANTHROPIC_AUTH_TOKEN": "your-gateway-api-key",
    "ANTHROPIC_MODEL": "deepseek-flash",
    "ANTHROPIC_DEFAULT_OPUS_MODEL": "deepseek-flash",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "deepseek-flash",
    "ANTHROPIC_DEFAULT_HAIKU_MODEL": "deepseek-flash"
  }
}
```

Two details carry over to every hosted gateway:

- `"hasCompletedOnboarding": true` — skips the first-run wizard. Without it Claude Code can sit on
  the login screen and never read your project-level `env` block.
- Watch the `/v1` suffix. A base ending in `/v1` gets `/messages` appended; a base like
  `https://api.deepseek.com/anthropic` gets `/v1/messages` appended. A wrong suffix is the usual
  cause of a 404 on the messages route.

Use whatever model id that gateway publishes. Do not reuse Cline's ids
(`cline-cloud/...`, `deepseek/deepseek-v4.1-flash`) — they only mean something on Cline's own API.

**This is not Cline.** It bills the gateway, not Cline. If you want Cline running on DeepSeek, see
[`cline-deepseek.md`](./cline-deepseek.md) — that needs none of this.

## Option B — Cline's own gateway (bridge required)

Claude Code speaks the **Anthropic Messages** protocol (`POST /v1/messages`). The Cline API is
OpenAI-compatible only. Its own docs say: *"Programmatic access to AI models through an
OpenAI-compatible Chat Completions API."* Every Anthropic path on that gateway is a 404:

```
POST https://api.cline.bot/api/v1/chat/completions  → 200
POST https://api.cline.bot/api/v1/messages          → 404 {"error":"Not Found","success":false}
POST https://api.cline.bot/v1/messages              → 404
POST https://api.cline.bot/anthropic/v1/messages    → 404
POST https://api.cline.bot/api/anthropic/v1/messages→ 404
POST https://api.cline.bot/api/v1/anthropic/v1/messages → 404
```

So *for Cline's gateway* there is no `settings.json` that works directly; you need a protocol bridge.
This repo ships one:

```
Claude Code ──Anthropic──▶ anthropic_cline_proxy.py ──OpenAI──▶ api.cline.bot ──▶ deepseek-v4.1-flash
            :8787                                     /api/v1/chat/completions
```


**Why is the URL `127.0.0.1:8787` and not `api.cline.bot`?**

Because the two ends of that settings line are different things. `ANTHROPIC_BASE_URL` is the address
Claude Code *sends to*, and Claude Code can only send Anthropic-format requests. `api.cline.bot` does
not accept that format (404), so it cannot be the value. The bridge listens on `127.0.0.1:8787`,
accepts the Anthropic request, and forwards it to `api.cline.bot/api/v1/chat/completions` in OpenAI
format. The model is still Cline's — localhost is only the adapter's door.

You can watch the actual upstream traffic with `PROXY_DEBUG=1`:

```
[proxy] -> POST https://api.cline.bot/api/v1/chat/completions  model=deepseek/deepseek-v4.1-flash  stream=True  key=yes
[proxy] <- upstream says id=gen_01M5RGSRSEF9BY3JEZQADQBEQ4 model=deepseek/deepseek-v4.1-flash
```

The `gen_…` id is Cline's own gateway generation id — the same one a direct `curl` to `api.cline.bot`
returns. The `model=` is Cline's `vendor/model` id, not anything the bridge invented.

The one thing you cannot do is put `api.cline.bot` in `ANTHROPIC_BASE_URL` directly:

| `ANTHROPIC_BASE_URL` | What Claude Code sends | Result |
| --- | --- | --- |
| `https://api.cline.bot/api/v1` | `POST /v1/messages` | `404` — endpoint does not exist |
| `http://127.0.0.1:8787` (bridge running) | `POST /v1/messages` | `200` — bridge forwards to Cline |

## Step 1 — get a Cline API key

Only needed for Option B.

1. Sign in at `https://app.cline.bot`
2. **Settings → API Keys → Create new API key**
3. Copy it. (This is a Cline API key, not a DeepSeek key — the model is billed through Cline.)

## Step 2 — start the bridge

Standard library only; no `pip install`, no Node.

```sh
export CLINE_API_KEY="your-cline-api-key"
python3 scripts/anthropic_cline_proxy.py          # 127.0.0.1:8787
```

Useful options, all environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `CLINE_API_KEY` | *(empty)* | Cline API key, sent as `Authorization: Bearer` upstream |
| `CLINE_API_BASE` | `https://api.cline.bot/api/v1` | Upstream base URL |
| `CLINE_MODEL` | `deepseek/deepseek-v4.1-flash` | Model used for names that are not passed through |
| `PROXY_PORT` / `PROXY_HOST` | `8787` / `127.0.0.1` | Listen address |
| `CLINE_REASONING_EFFORT` | *(empty)* | Passed through as `reasoning_effort` (e.g. `high`) |
| `PROXY_FORWARD_REASONING` | `0` | `1` exposes the model's reasoning as Anthropic `thinking` blocks |
| `PROXY_DEBUG` | `0` | `1` logs the upstream URL, model and Cline generation id |

Sanity check:

```sh
curl -s http://127.0.0.1:8787/health
# {"ok": true, "upstream": "https://api.cline.bot/api/v1",
#  "default_model": "deepseek/deepseek-v4.1-flash", "authenticated": true}
```

## Step 3 — point Claude Code at the bridge

Put this in **`~/.claude/settings.json`** (user scope; this repo also ships a copy at
`.claude/settings.json`):

```json
{
  "env": {
    "ANTHROPIC_BASE_URL": "http://127.0.0.1:8787",
    "ANTHROPIC_AUTH_TOKEN": "local-bridge",
    "ANTHROPIC_MODEL": "deepseek-flash",
    "ANTHROPIC_DEFAULT_OPUS_MODEL": "deepseek-flash",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "deepseek-flash",
    "ANTHROPIC_DEFAULT_HAIKU_MODEL": "deepseek-flash",
    "CLAUDE_CODE_SUBAGENT_MODEL": "deepseek-flash",
    "CLAUDE_CODE_EFFORT_LEVEL": "max",
    "CLAUDE_CODE_AUTO_COMPACT_WINDOW": "786432"
  }
}
```

- `ANTHROPIC_AUTH_TOKEN` is any non-empty string here; the bridge ignores the client credential and
  uses `CLINE_API_KEY` upstream. It must be non-empty, otherwise Claude Code shows the login screen.
- `deepseek-flash` is a friendly alias the bridge maps to `deepseek/deepseek-v4.1-flash`. You can
  also pass a full `vendor/model` id and it is forwarded untouched.
- `ANTHROPIC_DEFAULT_*_MODEL` keeps the Sonnet/Opus/Haiku slots on the same model so `/model` does
  not error.

Prefer user scope: a **project** `env` block only applies after first-run onboarding and the
folder-trust prompt, so a fresh install that shows the login screen will not read it yet.


## Step 4 — verify

```sh
claude
/status
```

The Status tab should show **Anthropic base URL: `http://127.0.0.1:8787`** and an auth line naming
`ANTHROPIC_AUTH_TOKEN`. Then ask something trivial; you should get a reply from DeepSeek.

## What was verified end to end

Every row below was run against the live `api.cline.bot` gateway through the bridge, not mocked:

| Check | Result |
| --- | --- |
| `GET /health` | `200 {"ok": true, ...}` |
| `POST /v1/messages` (plain text) | `200`, `content:[{type:text,text:"bridge-ok"}]`, `stop_reason:"end_turn"` |
| `POST /v1/messages` `stream:true` | `message_start → content_block_start → content_block_delta → content_block_stop → message_delta → message_stop` |
| Tool call, non-stream | `content:[{type:"tool_use", name:"get_weather", input:{"city":"Paris"}}]`, `stop_reason:"tool_use"` |
| Tool call, stream | `content_block_start{tool_use}` + `input_json_delta` fragments + `message_stop` |
| Multi-turn: `tool_use` → `tool_result` → answer | `200`, final text answer produced |
| Gap between Claude Code's protocol and Cline's | none observed on these paths |

Two behaviours worth knowing, both handled by the bridge:

1. **Non-streaming upstream requests fail intermittently.** Asking the gateway for a single JSON
   reply often returns `500 {"error":"empty response content","success":false}` when the model
   spends its budget on reasoning, while streaming the same turn works. The bridge therefore always
   streams upstream and aggregates locally when the client wants one JSON body.
2. **`max_tokens` must be generous.** With a tiny budget the reasoning phase consumes everything and
   the reply is empty. Claude Code already sends large budgets; keep it above ~256 if you script it.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Login screen instead of a session | `ANTHROPIC_AUTH_TOKEN` empty or not in `~/.claude/settings.json` | Add a non-empty value at user scope |
| `502 api_error: ... Connection refused` | Bridge not running | Start `scripts/anthropic_cline_proxy.py` |
| `401` from upstream | `CLINE_API_KEY` missing/invalid | Set it and restart the bridge |
| `403 ... only available via Cline product surfaces` | Requested a `cline-cloud/*` id | Use `deepseek-flash` or `deepseek/deepseek-v4.1-flash` |
| `400 invalid model format. Expected format: modelType/model` | Bare id forwarded to the gateway | Let the bridge alias it, or pass `vendor/model` |
| Replies look truncated | `max_tokens` too small for the reasoning phase | Raise it, or set `CLINE_REASONING_EFFORT` |

## Alternative: no Cline at all

Two ways to drop Cline's gateway entirely:

1. **Hosted Anthropic-compatible gateway** — see Option A above (AnyModel and similar). Nothing to
   run locally; the gateway holds the DeepSeek relationship and bills you directly.
2. **DeepSeek's own Anthropic endpoint** — needs no bridge either:

```json
{
  "env": {
    "ANTHROPIC_BASE_URL": "https://api.deepseek.com/anthropic",
    "ANTHROPIC_AUTH_TOKEN": "sk-your-deepseek-key",
    "ANTHROPIC_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_OPUS_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_HAIKU_MODEL": "deepseek-flash",
    "CLAUDE_CODE_SUBAGENT_MODEL": "deepseek-flash"
  }
}
```

That endpoint is genuinely Anthropic-shaped: it accepts `x-api-key` and `Authorization: Bearer` and
returns Anthropic-format errors. `[1m]` is Claude Code's 1M-context suffix; drop it on builds that
reject it.

## Limitations of the bridge

- `POST /v1/messages/count_tokens` returns a `chars/4` estimate, not a real tokenizer count. Claude
  Code only uses it for display and compaction hints.
- Reasoning text is hidden by default; enable `PROXY_FORWARD_REASONING=1` to surface it as
  `thinking` blocks (unsigned, so some clients may object).
- Single-process, no auth on the listener: it binds `127.0.0.1` by default. Do not expose it.
