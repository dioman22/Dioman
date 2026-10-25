# Running DeepSeek V4.1 Flash in Claude Code (outside Cline)

Goal: use `deepseek-flash` (DeepSeek-V4.1-Flash) from the Claude Code CLI, configured through
`settings.json`, with no Cline client in the path.

## Why you cannot keep pointing at the Cline gateway

Claude Code speaks the **Anthropic Messages** protocol (`POST /v1/messages`). The Cline gateway only
speaks the OpenAI chat-completions protocol:

```
POST https://api.cline.bot/api/v1/chat/completions  → 200 / works
POST https://api.cline.bot/api/v1/messages          → 404 {"error":"Not Found","success":false}
```

So `cline-cloud/deepseek-v4.1-flash` and `deepseek/deepseek-v4.1-flash` are both unusable from
Claude Code — not because the model is gone, but because that gateway serves no Anthropic endpoint.
Outside Cline you talk to DeepSeek directly.

## Endpoint

| Item | Value |
| --- | --- |
| Anthropic base URL | `https://api.deepseek.com/anthropic` |
| Endpoint Claude Code calls | `https://api.deepseek.com/anthropic/v1/messages` |
| Model | `deepseek-flash` (DeepSeek-V4.1-Flash-1017) |
| Alternate model | `deepseek-v4-pro` |
| Credential | DeepSeek API key from `https://platform.deepseek.com/api_keys` |

Verified on 2026-10-25: the endpoint accepts both `x-api-key` and `Authorization: Bearer`, and it
returns errors in genuine Anthropic shape, e.g.

```json
{"error":{"message":"Authentication Fails, Your api key: ****-key is invalid",
          "type":"authentication_error","param":null,"code":"invalid_request_error"}}
```

That shape matters — Claude Code parses it as an Anthropic error, so an auth failure surfaces as an
auth failure rather than a generic parse error.

## Recommended: user-scope `~/.claude/settings.json`

This is the reliable location. Create `~/.claude/settings.json`:

```json
{
  "env": {
    "ANTHROPIC_BASE_URL": "https://api.deepseek.com/anthropic",
    "ANTHROPIC_AUTH_TOKEN": "sk-your-deepseek-api-key",
    "ANTHROPIC_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_OPUS_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_HAIKU_MODEL": "deepseek-flash",
    "CLAUDE_CODE_SUBAGENT_MODEL": "deepseek-flash",
    "CLAUDE_CODE_EFFORT_LEVEL": "max",
    "CLAUDE_CODE_AUTO_COMPACT_WINDOW": "786432"
  }
}
```

This is the variable set DeepSeek publishes for Claude Code. Notes on each choice:

- `ANTHROPIC_AUTH_TOKEN` sends `Authorization: Bearer`. Unlike `ANTHROPIC_API_KEY`, it does **not**
  require a one-time approval under `/config`, so it is the least fiddly option.
- `deepseek-flash[1m]` asks Claude Code for the 1M-token context window. The `[1m]` suffix is
  Claude Code's own context-window syntax, not part of the DeepSeek model name. Set it through
  `env`/settings as shown: passing it on the `--model` flag is known to drop the suffix. On a build
  that does not support the suffix, use plain `deepseek-flash`.
- `ANTHROPIC_DEFAULT_*_MODEL` is what makes `/model` (Sonnet/Opus/Haiku slots) resolve onto DeepSeek
  instead of erroring on an unknown Claude name.
- `CLAUDE_CODE_AUTO_COMPACT_WINDOW=786432` keeps auto-compaction inside the model's real window.

Never commit this file — it holds your key. Keep it in `$HOME`, not in a repository.

## Alternative: project-scope `.claude/settings.json`

This repository ships one at `.claude/settings.json` that keeps the secret out of git by reading it
from the environment through `apiKeyHelper`:

```json
{
  "apiKeyHelper": "printenv DEEPSEEK_API_KEY",
  "env": {
    "ANTHROPIC_BASE_URL": "https://api.deepseek.com/anthropic",
    "ANTHROPIC_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_OPUS_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_SONNET_MODEL": "deepseek-flash[1m]",
    "ANTHROPIC_DEFAULT_HAIKU_MODEL": "deepseek-flash",
    "CLAUDE_CODE_SUBAGENT_MODEL": "deepseek-flash",
    "CLAUDE_CODE_EFFORT_LEVEL": "max",
    "CLAUDE_CODE_AUTO_COMPACT_WINDOW": "786432"
  }
}
```

Then export the key in the shell that launches Claude Code:

```sh
export DEEPSEEK_API_KEY="sk-your-deepseek-api-key"
cd /path/to/project && claude
```

Two caveats that send people in circles:

1. A **project** `env` block is applied only *after* first-run onboarding and the folder-trust
   prompt. If Claude Code stops at the login screen, the project file is not in play yet — put the
   credential in `~/.claude/settings.json` or export it in the shell.
2. `apiKeyHelper` runs under `/bin/sh` and its stdout becomes the credential. Empty output (key
   unset) becomes an empty credential, which reads as a 401.

## Verify it took

```sh
claude
# then inside the session:
/status
```

On the Status tab you should see an **Anthropic base URL** line showing
`https://api.deepseek.com/anthropic`, plus an **Auth token** line naming `ANTHROPIC_AUTH_TOKEN` (or
`apiKeyHelper`). If the base URL line is absent, Claude Code is not pointed at DeepSeek and nothing
else will fix itself.

A direct protocol check, no Claude Code involved:

```sh
curl -s -X POST https://api.deepseek.com/anthropic/v1/messages \
  -H 'content-type: application/json' \
  -H "x-api-key: $DEEPSEEK_API_KEY" \
  -d '{"model":"deepseek-flash","max_tokens":32,
       "messages":[{"role":"user","content":"say ok"}]}'
```

A `200` with `{"content":[{"type":"text","text":"ok"...}]}` means base URL, key and model are all
correct.

## Model names Claude Code may send anyway

DeepSeek maps Claude names server-side, so an unmapped pick will not break a session:

- `claude-opus*` → `deepseek-v4-pro` (billed at the V4 Pro price)
- `claude-haiku*`, `claude-sonnet*` → `deepseek-flash`
- any other non-`claude` name → `400`

That is also why pinning `ANTHROPIC_DEFAULT_*_MODEL` to `deepseek-flash` is worth doing: it keeps the
Opus slot off V4 Pro pricing.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Still asked to log in | Credential not visible before first-run | Set `ANTHROPIC_AUTH_TOKEN` in `~/.claude/settings.json` or the shell |
| `401 Authentication Fails` | Bad or empty key | Re-check the key; confirm `apiKeyHelper` prints something |
| `400` on the model name | Non-`claude` name not recognised | Use `deepseek-flash` or `deepseek-v4-pro` |
| 404 on `/v1/messages` | Base URL still points at Cline | Use `https://api.deepseek.com/anthropic` |
| Key ignored, no prompt | `ANTHROPIC_API_KEY` was declined once | Approve under `/config` → *Use custom API key*, or switch to `ANTHROPIC_AUTH_TOKEN` |
| `/model` errors picking Sonnet | No defaults set | Add `ANTHROPIC_DEFAULT_SONNET_MODEL` |

## What does not transfer from Cline

`cline-cloud/*` and `deepseek/*` are Cline-gateway identifiers. They mean nothing to DeepSeek's own
API, which accepts `deepseek-flash`, `deepseek-v4-pro`, and the legacy `deepseek-v4-flash` /
`deepseek-v4-flash-vision-exp` aliases only. See [`model-selection.md`](./model-selection.md) for
the namespace and gating details.

