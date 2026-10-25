# Cline on DeepSeek via an OpenAI-compatible gateway

Cline does not need a bridge or proxy for this. It has a native **OpenAI Compatible** provider.
Same idea as Claude Code on `https://anymodel.org/v1`, but Cline also accepts the OpenAI route, so
you skip the Anthropic translation entirely.

Verified endpoints on AnyModel (2026-10-25):

```
POST https://anymodel.org/v1/chat/completions  → 401 {"error":"Invalid API key"}   ← route exists
POST https://anymodel.org/v1/messages          → 401 {"error":"Invalid API key"}   ← Anthropic route (Claude Code)
GET  https://anymodel.org/v1/models            → 401 "API key required for remote API access"
```

401 rather than 404 means the routes are live; only a valid key is missing.

## Cline settings

Open Cline settings (⚙️) and fill in exactly these four things:

| Cline field | Value |
| --- | --- |
| **API Provider** | `OpenAI Compatible` |
| **Base URL** | `https://anymodel.org/v1` |
| **API Key** | your AnyModel API key |
| **Model** | the DeepSeek model id from your AnyModel models page |

Then, under **Model Configuration**, set the numbers to match the model you picked:

| Field | Notes |
| --- | --- |
| **Context Window** | e.g. `1000000` for a 1M-token DeepSeek route |
| **Max Output Tokens** | e.g. `65536`; must be large enough for the reasoning phase |
| **Image Support** | enable only if the model is multimodal |
| **Computer Use** | enable for tool/function calling |
| **Input / Output Price** | optional, only affects Cline's cost display |

## Model id format

AnyModel ids are `<provider-prefix>/<model>` — for example `cc/claude-opus-5-5`,
`cx/gpt-6-astra`, `glm/glm-5.3-flash`. Slashes are fine in Cline's Model field; it is sent verbatim.

Take the DeepSeek id from `https://anymodel.org/en/models` (or the `/v1/models` call with your key).
Do not reuse Cline-gateway ids here — `cline-cloud/deepseek-v4.1-flash` and
`deepseek/deepseek-v4.1-flash` belong to `api.cline.bot` and are meaningless to AnyModel.

## If the model field is rejected

- `model not found` → the id is not in AnyModel's catalogue; copy it again from the models page.
- `401` with a key that works in Claude Code → the key is fine, so check for a trailing space in
  **Base URL**; it must be exactly `https://anymodel.org/v1` with no trailing slash.
- Cline keeps talking to the old provider → re-save the provider dropdown after editing the key.

## Why this differs from the bridge in `scripts/`

| Route | What it is | Proxy needed |
| --- | --- | --- |
| Cline → AnyModel `/v1/chat/completions` | Cline's native OpenAI provider | no |
| Claude Code → AnyModel `/v1/messages` | hosted Anthropic-compatible endpoint | no |
| Cline → `api.cline.bot` | Cline's own gateway, OpenAI-only | no (Cline speaks OpenAI) |
| Claude Code → `api.cline.bot` | Cline's gateway, no Anthropic endpoint | yes — see `claude-code-setup.md` Option B |

The bridge exists for exactly one cell of that table: Anthropic-protocol client against Cline's
OpenAI-only gateway. Cline talking to an OpenAI endpoint is the easy case.
