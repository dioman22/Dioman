# Cline on DeepSeek

Cline runs DeepSeek natively. No proxy, no Claude Code, no third-party gateway.

## Settings (⚙️)

**API Provider:** `DeepSeek`

| Field | Value |
| --- | --- |
| DeepSeek API Key | your key from `https://platform.deepseek.com/api_keys` |
| Model | `deepseek-flash` |

`deepseek-flash` is DeepSeek-V4.1-Flash (currently served as V4.1-Flash-1017). `deepseek-v4-pro` is
the larger sibling. That's the whole configuration — Cline talks to `https://api.deepseek.com`
itself.

## If `deepseek-flash` is missing from the Model dropdown

Cline's built-in DeepSeek list can lag the provider. Two ways around it, both without a proxy:

**A. Same provider, newer model list** — update Cline, then reopen the Model dropdown.

**B. OpenAI Compatible** — use Cline's generic provider, which lets you type any model id:

| Field | Value |
| --- | --- |
| API Provider | `OpenAI Compatible` |
| Base URL | `https://api.deepseek.com` |
| API Key | your DeepSeek key |
| Model | `deepseek-flash` |

Under **Model Configuration** set Context Window `1000000`, Max Output Tokens `8192`-`65536`, and
enable **Computer Use** for tool calling.

## About the original error

`There's an issue with the selected model (deepseek/deepseek-v4.1-flash)` happened because the
provider and the model id belonged to different systems:

| Selection | Where the id is valid | In Cline with provider = Cline |
| --- | --- | --- |
| `cline-cloud/deepseek-v4.1-flash` | Cline's own cloud catalogue | gated, `403` outside product surfaces |
| `deepseek/deepseek-v4.1-flash` | Cline's API gateway / OpenRouter | `404 model not found` if the client build predates it |
| `deepseek-flash` | **DeepSeek itself** | correct — but only with provider = `DeepSeek` |

So if you want DeepSeek in Cline, pick the **DeepSeek** provider and the id
`deepseek-flash`. Do not pair a `vendor/model` shorthand with the DeepSeek provider — DeepSeek's API
answers `400 invalid model format` for those, and the Cline provider answers `404` for ids it has not
published to your account.

## Notes

- Billing is DeepSeek's own; nothing routes through Cline's balance.
- Keep Max Output Tokens generous. V4.1 Flash reasons before answering, and a small budget returns
  an empty `content` field.
- Legacy ids `deepseek-v4-flash` and `deepseek-v4-flash-vision-exp` are still accepted by DeepSeek and
  route to V4.1 Flash, but prefer `deepseek-flash`.
