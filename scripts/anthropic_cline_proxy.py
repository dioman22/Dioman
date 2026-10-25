#!/usr/bin/env python3
"""Anthropic Messages API  ->  OpenAI Chat Completions  bridge for the Cline API.

Claude Code speaks the Anthropic Messages protocol (POST /v1/messages). The Cline API
(https://api.cline.bot/api/v1) is OpenAI-compatible only and answers 404 on every Anthropic
path. This process sits in between: it accepts Anthropic requests from Claude Code, translates
them to OpenAI chat-completions calls against Cline, and translates the replies (including
streaming and tool calls) back into Anthropic format.

Standard library only - no pip install required.

    export CLINE_API_KEY=...                      # from app.cline.bot > Settings > API Keys
    python3 scripts/anthropic_cline_proxy.py      # listens on 127.0.0.1:8787

Then point Claude Code at it:
    ANTHROPIC_BASE_URL=http://127.0.0.1:8787
    ANTHROPIC_AUTH_TOKEN=local-bridge            # any non-empty value
    ANTHROPIC_MODEL=deepseek-flash
"""

import argparse
import json
import os
import sys
import time
import uuid
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UPSTREAM_BASE = os.environ.get("CLINE_API_BASE", "https://api.cline.bot/api/v1").rstrip("/")
UPSTREAM_KEY = os.environ.get("CLINE_API_KEY", "")
DEFAULT_MODEL = os.environ.get("CLINE_MODEL", "deepseek/deepseek-v4.1-flash")
REASONING_EFFORT = os.environ.get("CLINE_REASONING_EFFORT", "")
TIMEOUT = float(os.environ.get("CLINE_TIMEOUT", "600"))
# Forward the model's reasoning as Anthropic "thinking" blocks. Off by default: unsigned
# thinking blocks are not universally accepted by clients, and the text is redundant.
FORWARD_REASONING = os.environ.get("PROXY_FORWARD_REASONING", "") == "1"

# Friendly names -> Cline's vendor/model ids. Anything containing a "/" is passed through,
# everything else falls back to DEFAULT_MODEL.
ALIASES = {
    "deepseek-flash": "deepseek/deepseek-v4.1-flash",
    "deepseek": "deepseek/deepseek-v4.1-flash",
    "deepseek-v4-pro": "deepseek/deepseek-v4-pro",
}

STOP_REASONS = {
    "stop": "end_turn",
    "length": "max_tokens",
    "content_filter": "end_turn",
    "tool_calls": "tool_use",
    "function_call": "tool_use",
}


def upstream_model(name):
    if not name:
        return DEFAULT_MODEL
    if name in ALIASES:
        return ALIASES[name]
    return name if "/" in name else DEFAULT_MODEL


def blocks_to_text(content):
    """Flatten an Anthropic content value (string or block list) to plain text."""
    if content is None:
        return ""
    if isinstance(content, str):
        return content
    parts = []
    for block in content:
        if not isinstance(block, dict):
            parts.append(str(block))
        elif block.get("type") == "text":
            parts.append(block.get("text", ""))
        elif block.get("type") == "image":
            parts.append("[image omitted]")
        elif block.get("type") == "thinking":
            continue
    return "\n".join(p for p in parts if p)


def image_parts(content):
    """Return OpenAI image_url parts for any base64/url images in an Anthropic block list."""
    parts = []
    for block in content or []:
        if isinstance(block, dict) and block.get("type") == "image":
            src = block.get("source") or {}
            if src.get("type") == "base64" and src.get("data"):
                url = "data:%s;base64,%s" % (src.get("media_type", "image/png"), src["data"])
                parts.append({"type": "image_url", "image_url": {"url": url}})
            elif src.get("type") == "url" and src.get("url"):
                parts.append({"type": "image_url", "image_url": {"url": src["url"]}})
    return parts


def to_openai(body):
    """Translate an Anthropic /v1/messages body into an OpenAI chat-completions body."""
    messages = []

    system = body.get("system")
    if system:
        messages.append({"role": "system", "content": blocks_to_text(system)})

    for message in body.get("messages") or []:
        role = message.get("role", "user")
        content = message.get("content")

        if isinstance(content, str):
            messages.append({"role": role, "content": content})
            continue

        texts, tool_calls, tool_results = [], [], []
        for block in content or []:
            if not isinstance(block, dict):
                continue
            kind = block.get("type")
            if kind == "text":
                texts.append(block.get("text", ""))
            elif kind == "tool_use":
                tool_calls.append({
                    "id": block.get("id") or "call_" + uuid.uuid4().hex[:24],
                    "type": "function",
                    "function": {
                        "name": block.get("name", ""),
                        "arguments": json.dumps(block.get("input") or {}, ensure_ascii=False),
                    },
                })
            elif kind == "tool_result":
                tool_results.append({
                    "role": "tool",
                    "tool_call_id": block.get("tool_use_id", ""),
                    "content": blocks_to_text(block.get("content")),
                })

        # tool results must precede the assistant turn that follows them
        messages.extend(tool_results)

        images = image_parts(content)
        if images:
            parts = [{"type": "text", "text": t} for t in texts if t]
            messages.append({"role": role, "content": parts + images})
        elif texts or tool_calls:
            entry = {"role": role}
            joined = "\n".join(t for t in texts if t)
            if tool_calls:
                entry["content"] = joined or None
                entry["tool_calls"] = tool_calls
            else:
                entry["content"] = joined
            messages.append(entry)

    out = {
        "model": upstream_model(body.get("model")),
        "messages": messages,
        "max_tokens": body.get("max_tokens") or 4096,
    }
    for key in ("temperature", "top_p"):
        if body.get(key) is not None:
            out[key] = body[key]
    if body.get("stop_sequences"):
        out["stop"] = body["stop_sequences"]
    if body.get("stream"):
        out["stream"] = True
        out["stream_options"] = {"include_usage": True}
    if REASONING_EFFORT:
        out["reasoning_effort"] = REASONING_EFFORT

    tools = [t for t in (body.get("tools") or []) if t.get("name")]
    if tools:
        out["tools"] = [{
            "type": "function",
            "function": {
                "name": t["name"],
                "description": t.get("description", ""),
                "parameters": t.get("input_schema") or {"type": "object", "properties": {}},
            },
        } for t in tools]

        choice = body.get("tool_choice")
        if isinstance(choice, dict):
            if choice.get("type") == "tool" and choice.get("name"):
                out["tool_choice"] = {"type": "function", "function": {"name": choice["name"]}}
            elif choice.get("type") == "any":
                out["tool_choice"] = "required"
            elif choice.get("type") == "none":
                out["tool_choice"] = "none"
            else:
                out["tool_choice"] = "auto"
    return out



def _tool_use_block(call_id, name, arguments):
    try:
        parsed = json.loads(arguments or "{}")
    except Exception:
        parsed = {"_raw_arguments": arguments}
    return {
        "type": "tool_use",
        "id": call_id or "toolu_" + uuid.uuid4().hex[:24],
        "name": name or "",
        "input": parsed,
    }


def from_openai(payload, model):
    """Translate a non-streaming OpenAI chat-completions reply into an Anthropic message."""
    choice = (payload.get("choices") or [{}])[0]
    message = choice.get("message") or {}
    content = []

    reasoning = message.get("reasoning") or message.get("reasoning_content")
    if reasoning and FORWARD_REASONING:
        content.append({"type": "thinking", "thinking": reasoning, "signature": ""})
    if message.get("content"):
        content.append({"type": "text", "text": message["content"]})
    for call in message.get("tool_calls") or []:
        fn = call.get("function") or {}
        content.append(_tool_use_block(call.get("id"), fn.get("name"), fn.get("arguments")))

    if not content:
        content.append({"type": "text", "text": ""})

    usage = payload.get("usage") or {}
    return {
        "id": "msg_" + uuid.uuid4().hex[:24],
        "type": "message",
        "role": "assistant",
        "model": model,
        "content": content,
        "stop_reason": STOP_REASONS.get(choice.get("finish_reason"), "end_turn"),
        "stop_sequence": None,
        "usage": {
            "input_tokens": usage.get("prompt_tokens", 0),
            "output_tokens": usage.get("completion_tokens", 0),
        },
    }


class StreamTranslator:
    """Convert a stream of OpenAI chat-completion chunks into Anthropic SSE events."""

    def __init__(self, model):
        self.model = model
        self.message_id = "msg_" + uuid.uuid4().hex[:24]
        self.index = -1
        self.open_block = None          # None | "text" | "thinking" | tool index
        self.tool_blocks = {}           # upstream tool index -> anthropic content index
        self.stop_reason = None
        self.usage = {"input_tokens": 0, "output_tokens": 0}
        self.started = False

    def _start(self):
        self.started = True
        return ("message_start", {
            "type": "message_start",
            "message": {
                "id": self.message_id,
                "type": "message",
                "role": "assistant",
                "model": self.model,
                "content": [],
                "stop_reason": None,
                "stop_sequence": None,
                "usage": {"input_tokens": 0, "output_tokens": 0},
            },
        })

    def _close(self):
        if self.open_block is None:
            return []
        events = [("content_block_stop", {"type": "content_block_stop", "index": self.index})]
        self.open_block = None
        return events

    def _open(self, kind, block):
        events = self._close()
        self.index += 1
        self.open_block = kind
        events.append(("content_block_start", {
            "type": "content_block_start",
            "index": self.index,
            "content_block": block,
        }))
        return events

    def feed(self, chunk):
        events = []
        if not self.started:
            events.append(self._start())

        usage = chunk.get("usage") or {}
        if usage:
            self.usage = {
                "input_tokens": usage.get("prompt_tokens", self.usage["input_tokens"]),
                "output_tokens": usage.get("completion_tokens", self.usage["output_tokens"]),
            }

        for choice in chunk.get("choices") or []:
            delta = choice.get("delta") or {}

            reasoning = delta.get("reasoning") or delta.get("reasoning_content")
            if reasoning and FORWARD_REASONING:
                if self.open_block != "thinking":
                    events += self._open("thinking", {"type": "thinking", "thinking": ""})
                events.append(("content_block_delta", {
                    "type": "content_block_delta",
                    "index": self.index,
                    "delta": {"type": "thinking_delta", "thinking": reasoning},
                }))

            text = delta.get("content")
            if text:
                if self.open_block != "text":
                    events += self._open("text", {"type": "text", "text": ""})
                events.append(("content_block_delta", {
                    "type": "content_block_delta",
                    "index": self.index,
                    "delta": {"type": "text_delta", "text": text},
                }))

            for call in delta.get("tool_calls") or []:
                upstream_index = call.get("index", 0)
                fn = call.get("function") or {}
                if upstream_index not in self.tool_blocks:
                    events += self._open(upstream_index, {
                        "type": "tool_use",
                        "id": call.get("id") or "toolu_" + uuid.uuid4().hex[:24],
                        "name": fn.get("name") or "",
                        "input": {},
                    })
                    self.tool_blocks[upstream_index] = self.index
                partial = fn.get("arguments")
                if partial:
                    events.append(("content_block_delta", {
                        "type": "content_block_delta",
                        "index": self.tool_blocks[upstream_index],
                        "delta": {"type": "input_json_delta", "partial_json": partial},
                    }))

            if choice.get("finish_reason"):
                self.stop_reason = STOP_REASONS.get(choice["finish_reason"], "end_turn")
        return events

    def finish(self):
        events = self._close()
        if not self.started:
            events.insert(0, self._start())
        events.append(("message_delta", {
            "type": "message_delta",
            "delta": {"stop_reason": self.stop_reason or "end_turn", "stop_sequence": None},
            "usage": self.usage,
        }))
        events.append(("message_stop", {"type": "message_stop"}))
        return events



def anthropic_error(kind, message):
    return {"type": "error", "error": {"type": kind, "message": message}}


def call_upstream(body):
    data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(UPSTREAM_BASE + "/chat/completions", data=data, method="POST")
    request.add_header("Content-Type", "application/json")
    request.add_header("Accept", "text/event-stream" if body.get("stream") else "application/json")
    if UPSTREAM_KEY:
        request.add_header("Authorization", "Bearer " + UPSTREAM_KEY)
    return urllib.request.urlopen(request, timeout=TIMEOUT)


def iter_upstream_chunks(response):
    """Yield decoded OpenAI SSE chunks from the upstream streaming response."""
    for raw in response:
        line = raw.decode("utf-8", "replace").strip()
        if not line or line.startswith(":"):
            continue
        if not line.startswith("data:"):
            continue
        data = line[5:].strip()
        if data == "[DONE]":
            return
        try:
            yield json.loads(data)
        except Exception:
            continue


def assemble(events):
    """Fold a sequence of Anthropic stream events into one Anthropic message object."""
    message = None
    blocks, order = {}, []
    stop_reason = "end_turn"
    usage = {"input_tokens": 0, "output_tokens": 0}

    for name, data in events:
        if name == "message_start":
            message = dict(data["message"])
        elif name == "content_block_start":
            index = data["index"]
            blocks[index] = dict(data["content_block"])
            order.append(index)
        elif name == "content_block_delta":
            block = blocks.get(data["index"])
            delta = data.get("delta") or {}
            if block is None:
                continue
            if delta.get("type") == "text_delta":
                block["text"] = block.get("text", "") + delta["text"]
            elif delta.get("type") == "thinking_delta":
                block["thinking"] = block.get("thinking", "") + delta["thinking"]
            elif delta.get("type") == "input_json_delta":
                block["_partial_json"] = block.get("_partial_json", "") + delta["partial_json"]
        elif name == "message_delta":
            stop_reason = (data.get("delta") or {}).get("stop_reason") or stop_reason
            usage = data.get("usage") or usage

    content = []
    for index in order:
        block = dict(blocks[index])
        partial = block.pop("_partial_json", None)
        if block.get("type") == "tool_use":
            try:
                block["input"] = json.loads(partial) if partial else {}
            except Exception:
                block["input"] = {"_raw_arguments": partial}
        content.append(block)

    result = message or {}
    result["type"] = "message"
    result["role"] = "assistant"
    result["content"] = content or [{"type": "text", "text": ""}]
    result["stop_reason"] = stop_reason
    result["usage"] = usage
    return result


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[proxy] %s\n" % (fmt % args))
        sys.stderr.flush()

    def send_json(self, code, payload):
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        route = self.path.split("?")[0]
        if route == "/health":
            return self.send_json(200, {
                "ok": True,
                "upstream": UPSTREAM_BASE,
                "default_model": DEFAULT_MODEL,
                "authenticated": bool(UPSTREAM_KEY),
            })
        if route.endswith("/models"):
            return self.send_json(200, {"object": "list", "data": [
                {"id": "deepseek-flash", "type": "model", "display_name": "DeepSeek V4.1 Flash (via Cline)"},
                {"id": "deepseek-v4-pro", "type": "model", "display_name": "DeepSeek V4 Pro (via Cline)"},
            ]})
        self.send_json(404, anthropic_error("not_found_error", "no route for %s" % route))

    def do_POST(self):
        route = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw or b"{}")
        except Exception as exc:
            return self.send_json(400, anthropic_error("invalid_request_error", "invalid JSON: %s" % exc))

        if route.endswith("/count_tokens"):
            # Rough estimate; Claude Code only uses this for display/compaction hints.
            approx = max(1, len(json.dumps(body)) // 4)
            return self.send_json(200, {"input_tokens": approx})

        if not route.endswith("/messages"):
            return self.send_json(404, anthropic_error("not_found_error", "no route for %s" % route))

        oa_body = to_openai(body)
        shown_model = body.get("model") or oa_body["model"]
        client_wants_stream = bool(oa_body.get("stream"))

        # Always stream upstream. The gateway's non-streaming path returns
        # 500 {"error":"empty response content"} whenever the model spends its budget on
        # reasoning, while the streaming path returns the same turn correctly. We aggregate
        # locally when the client asked for a single JSON response.
        oa_body["stream"] = True
        oa_body["stream_options"] = {"include_usage": True}

        try:
            response = call_upstream(oa_body)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")
            return self.send_json(exc.code, anthropic_error("api_error", detail))
        except Exception as exc:
            return self.send_json(502, anthropic_error("api_error", str(exc)))

        if client_wants_stream:
            return self.stream_response(response, shown_model)

        events = []
        translator = StreamTranslator(shown_model)
        try:
            for chunk in iter_upstream_chunks(response):
                if chunk.get("error"):
                    return self.send_json(502, anthropic_error("api_error", json.dumps(chunk["error"])))
                events += translator.feed(chunk)
            events += translator.finish()
        except Exception as exc:
            return self.send_json(502, anthropic_error("api_error", str(exc)))
        self.send_json(200, assemble(events))

    def stream_response(self, response, model):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True

        def emit(event):
            name, data = event
            payload = "event: %s\ndata: %s\n\n" % (name, json.dumps(data))
            self.wfile.write(payload.encode("utf-8"))
            self.wfile.flush()

        translator = StreamTranslator(model)
        try:
            for chunk in iter_upstream_chunks(response):
                if chunk.get("error"):
                    emit(("error", anthropic_error("api_error", json.dumps(chunk["error"]))))
                    return
                for event in translator.feed(chunk):
                    emit(event)
            for event in translator.finish():
                emit(event)
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception as exc:
            try:
                emit(("error", anthropic_error("api_error", str(exc))))
            except Exception:
                pass


def main():
    parser = argparse.ArgumentParser(description="Anthropic -> Cline (OpenAI) protocol bridge")
    parser.add_argument("--host", default=os.environ.get("PROXY_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PROXY_PORT", "8787")))
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    sys.stderr.write(
        "[proxy] Anthropic endpoint : http://%s:%d/v1/messages\n"
        "[proxy] upstream           : %s/chat/completions\n"
        "[proxy] default model      : %s\n"
        "[proxy] api key            : %s\n"
        % (args.host, args.port, UPSTREAM_BASE, DEFAULT_MODEL,
           "set" if UPSTREAM_KEY else "NOT set (calls will be anonymous)")
    )
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


if __name__ == "__main__":
    main()

