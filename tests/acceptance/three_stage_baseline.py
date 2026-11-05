"""Independent CDX-11A/B1 acceptance through a fresh, real MCP connection.

No product read/serialization algorithm is copied here. Literal text fixtures,
separate read_file results, standard JSON decoding and fixture HTTP bytes are
the oracles. Raw requests, responses and every failed measurement are retained.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time


MAX_FRAME = 8 * 1024 * 1024
MAX_CALLS = 600
MAX_TEXT = 256 * 1024
MAX_BATCH = 4 * 1024 * 1024
SOURCE_FILES = (
    "src/cdx-protocol/operations.c", "src/cdx-protocol/http.c",
    "src/cdx-protocol/tools.c", "src/cdx-protocol/json.c",
    "src/cdx-cli/main.c", "src/cdx-cli/main_posix.c",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, value):
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def frame(value):
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


class Mcp:
    def __init__(self, binary, cwd, archive, deadline, receipt):
        self.deadline = deadline
        self.archive = archive
        self.archive.mkdir()
        self.sequence = 0
        self.responses = queue.Queue(maxsize=16)
        self.stderr = bytearray()
        self.process = subprocess.Popen(
            [str(binary), "--mcp"], cwd=cwd, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.audit = {"pid": self.process.pid, "parent_pid": os.getpid(),
                      "program": str(binary), "args": ["--mcp"], "cwd": str(cwd),
                      "started_ns": time.time_ns(), "cleanup_complete": False,
                      "forced_termination": False}
        receipt["processes"].append(self.audit)
        self.readers = [threading.Thread(target=self._stdout, daemon=True),
                        threading.Thread(target=self._stderr, daemon=True)]
        for reader in self.readers:
            reader.start()

    def initialize(self):
        result = self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                           "clientInfo": {"name": "cdx-independent-three-stage", "version": "2"}})
        self.send_notification("notifications/initialized", {})
        return result

    def _stdout(self):
        try:
            for sequence in range(MAX_CALLS + 32):
                raw = self.process.stdout.readline(MAX_FRAME + 1)
                if not raw:
                    return
                name = f"response-{sequence:04d}.json"
                (self.archive / name).write_bytes(raw)
                if len(raw) > MAX_FRAME or not raw.endswith(b"\n"):
                    raise ValueError("MCP frame exceeds the existing 8 MiB JSON bound")
                value = json.loads(raw.decode("utf-8", errors="strict"))
                self.responses.put((raw, value, name), timeout=2)
            raise ValueError("MCP response-count bound exceeded")
        except BaseException as error:
            try:
                self.responses.put(error, timeout=1)
            except queue.Full:
                pass

    def _stderr(self):
        for _ in range(64):
            data = self.process.stderr.read(4096)
            if not data:
                return
            self.stderr.extend(data)

    def send_notification(self, method, params):
        raw = frame({"jsonrpc": "2.0", "method": method, "params": params})
        (self.archive / f"notification-{self.sequence:04d}-{method.rsplit('/', 1)[-1]}.json").write_bytes(raw)
        self.process.stdin.write(raw)
        self.process.stdin.flush()

    def rpc(self, method, params, cancel=False):
        if self.sequence >= MAX_CALLS or time.monotonic() >= self.deadline:
            raise TimeoutError("acceptance call-count/deadline reached")
        self.sequence += 1
        request = frame({"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params})
        request_path = self.archive / f"request-{self.sequence:04d}.json"
        request_path.write_bytes(request)
        notification = b""
        if cancel:
            notification = frame({"jsonrpc": "2.0", "method": "notifications/cancelled",
                                  "params": {"requestId": self.sequence, "reason": "bounded acceptance"}})
            (self.archive / f"cancel-{self.sequence:04d}.json").write_bytes(notification)
        started = time.perf_counter()
        self.process.stdin.write(request + notification)
        self.process.stdin.flush()
        for _ in range(32):
            remaining = min(30, self.deadline - time.monotonic())
            if remaining <= 0:
                raise TimeoutError("acceptance response deadline reached")
            item = self.responses.get(timeout=remaining)
            if isinstance(item, BaseException):
                raise item
            raw, response, name = item
            if response.get("id") != self.sequence:
                if "id" in response:
                    raise ValueError("unexpected response ID")
                continue
            result = response.get("result")
            record = {"request_id": self.sequence, "method": method,
                      "request_file": str(request_path), "response_file": str(self.archive / name),
                      "request_utf8_bytes": len(request), "mcp_response_utf8_bytes": len(raw),
                      "mcp_response_sha256": digest(raw), "elapsed_seconds": time.perf_counter() - started,
                      "rpc_error": response.get("error"), "retry_count": 0}
            return result, record
        raise ValueError("notification-count bound reached")

    def tool(self, name, args, cancel=False):
        result, record = self.rpc("tools/call", {"name": name, "arguments": args}, cancel)
        record["tool"] = name
        record["is_error"] = bool(result and result.get("isError"))
        if record["rpc_error"] or not result:
            record["internal_json_bytes"] = 0
            record["tool_status"] = "RPC_ERROR"
            return {}, record
        text = result["content"][0]["text"]
        internal = text.encode("utf-8")
        path = self.archive / f"internal-{record['request_id']:04d}.json"
        path.write_bytes(internal)
        value = json.loads(text)
        record.update(internal_json_bytes=len(internal), internal_json_file=str(path),
                      tool_status=value.get("status"))
        return value, record

    def close(self):
        try:
            if not self.process.stdin.closed:
                self.process.stdin.close()
            self.process.wait(timeout=5)
        except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
            self.audit["forced_termination"] = True
            self.process.kill()
            self.process.wait(timeout=5)
        finally:
            for reader in self.readers:
                reader.join(timeout=3)
            self.audit["exit_code"] = self.process.poll()
            self.audit["cleanup_complete"] = self.process.poll() is not None and all(not reader.is_alive() for reader in self.readers)
            (self.archive / "stderr.bin").write_bytes(self.stderr)
            for pipe in (self.process.stdout, self.process.stderr):
                pipe.close()


class HttpFixture:
    def __init__(self, payloads, deadline):
        self.requests = []
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def date_time_string(self, timestamp=None):
                return "Thu, 05 Nov 2026 00:00:00 GMT"

            def do_GET(self):
                self.connection.settimeout(5)
                if time.monotonic() >= deadline or len(owner.requests) >= 64:
                    self.close_connection = True
                    return
                status, data = payloads[self.path]
                owner.requests.append({"path": self.path, "status_code": status,
                                       "body_bytes": len(data), "body_sha256": digest(data)})
                self.send_response(status)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data)

            def log_message(self, *_args):
                return

        self.server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        self.thread.start()

    def url(self, path):
        return f"http://127.0.0.1:{self.server.server_port}{path}"

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)
        return not self.thread.is_alive()


def make_corpus(directory):
    directory.mkdir()
    entries = {}

    def add(name, source_bytes, encoding, expected, **options):
        path = directory / name
        path.write_bytes(source_bytes)
        entries[name] = {"path": str(path), "raw_bytes": len(source_bytes), "raw_sha256": digest(source_bytes),
                         "encoding": encoding, "expected_text": expected,
                         "expected_text_utf8_bytes": len(expected.encode("utf-8")) if expected is not None else None,
                         "expected_text_sha256": digest(expected.encode("utf-8")) if expected is not None else None,
                         "options": options}
        return {"path": str(path), "encoding": encoding, "max_bytes": MAX_TEXT, **options}

    ordinary = [
        add("普通 utf8.txt", "甲\r\n乙\n丙\r末尾 $(literal)".encode(), "utf-8", "甲\r\n乙\n丙\r末尾 $(literal)", raw=True),
        add("legacy gbk.txt", "中文旧文件\r\n保留".encode("gbk"), "cp936", "中文旧文件\r\n保留", raw=True),
        add("bom.txt", b"\xef\xbb\xbf" + "BOM 中文\r\n".encode(), "utf-8-bom", "BOM 中文\r\n", raw=True),
        add("mixed lines.txt", b"first\r\nsecond\nthird\rlast", "utf-8", "second\nthird\r", start_line=2, line_count=2),
        add("utf16 tail.txt", b"\xff\xfe" + "first\r\n中间\n最后".encode("utf-16-le"), "utf-16-le", "中间\n最后", tail_lines=2),
        add("empty.txt", b"", "utf-8", "", raw=True),
    ]
    invalid = add("invalid utf8.txt", b"\xff\xfe\x81", "utf-8", None, raw=True)
    oversized = add("over raw budget.txt", b"a" * (MAX_TEXT + 1), "utf-8", None, raw=True)
    missing = {"path": str(directory / "missing.txt"), "encoding": "utf-8", "raw": True, "max_bytes": MAX_TEXT}
    add("code-shaped $(literal).txt", b"literal data", "utf-8", "literal data", raw=True)
    failure = [ordinary[0], missing, invalid, oversized, ordinary[1]]
    pattern = b'"\\\n'
    maximum = (pattern * (MAX_TEXT // len(pattern) + 1))[:MAX_TEXT]
    maximum_files = [add(f"maximum {index:02d}.txt", maximum, "utf-8", maximum.decode(), raw=True) for index in range(32)]
    # Eight MiB source decoding makes an early real cancellation/deadline
    # observable without sleeps, artificial hooks or product timing changes.
    large = b"a\n" * (4 * 1024 * 1024)
    slow = add("deadline source 8MiB.txt", large, "utf-8", "a\n", start_line=1, line_count=1)
    path_cases = []
    if os.name != "nt":
        add("path semantics.txt", b"lexical neighbor", "utf-8", "lexical neighbor", raw=True)
        (directory / "symlink target" / "nested").mkdir(parents=True)
        add("symlink target/path semantics.txt", b"symlink parent", "utf-8", "symlink parent", raw=True)
        (directory / "directory link").symlink_to(Path("symlink target") / "nested", target_is_directory=True)
        path_cases = [
            ("missing-parent", str(directory / "missing directory" / ".." / "path semantics.txt"), None),
            ("symlink-parent", str(directory / "directory link" / ".." / "path semantics.txt"), "symlink parent"),
        ]
    return entries, ordinary, failure, maximum_files, [slow] * 32, path_cases


def add_check(case, name, passed, detail=None):
    entry = {"name": name, "status": "PASS" if passed else "FAIL"}
    if detail is not None:
        entry["detail"] = detail
    case["checks"].append(entry)


def measured_text(value):
    text = value.get("text")
    data = text.encode("utf-8") if isinstance(text, str) else None
    return {"status": value.get("status"), "body_bytes": len(data) if data is not None else 0,
            "text_sha256": digest(data) if data is not None else None}


def finish_case(case):
    statuses = [check["status"] for check in case["checks"]]
    case["status"] = "PASS" if statuses and all(status == "PASS" for status in statuses) else "FAIL"
    return case


def measurement(records, successful, attempts, bodies):
    return {"calls": len(records), "body_bytes": sum(body["body_bytes"] for body in bodies),
            "body_text_sha256": [body["text_sha256"] for body in bodies],
            "internal_json_bytes": sum(record["internal_json_bytes"] for record in records),
            "mcp_response_utf8_bytes": sum(record["mcp_response_utf8_bytes"] for record in records),
            "elapsed_seconds": sum(record["elapsed_seconds"] for record in records),
            "attempted_items": attempts, "successful_items": successful,
            "success_rate": successful / attempts if attempts else None,
            "failed_items": attempts - successful, "retry_count": 0, "records": records}


def check_batch_structure(case, batch, requests):
    items = batch.get("results", [])
    add_check(case, "all-ordered-item-states", len(items) == len(requests) and all(
        item.get("index") == index and item.get("path") == requests[index]["path"] and
        item.get("source") == "disk" for index, item in enumerate(items)))
    statuses = [item.get("status") for item in items]
    add_check(case, "batch-counts-match-item-states", batch.get("count") == len(requests) and
              batch.get("completed") == sum(status != "not_run" for status in statuses) and
              batch.get("not_run") == statuses.count("not_run") and
              batch.get("failed") == sum(status not in ("ok", "not_run") for status in statuses))
    add_check(case, "nested-statuses-match", all(
        item.get("status") == item.get("result", {}).get("status") for item in items))
    retained = [item.get("result", {}).get("text") for item in items]
    add_check(case, "successful-items-have-text", all(
        isinstance(item.get("result", {}).get("text"), str) for item in items if item.get("status") == "ok"))
    add_check(case, "text-byte-count-matches-retained-text", batch.get("text_bytes") == sum(
        len(text.encode("utf-8")) for text in retained if isinstance(text, str)))
    return items


def read_comparison(client, label, round_number, requests, corpus):
    case = {"name": label, "round": round_number, "checks": []}
    arguments = {"files": requests, "max_total_bytes": MAX_BATCH, "max_output_bytes": MAX_BATCH, "timeout_ms": 10000}
    case["execution_order"] = ["batch", "separate"] if round_number % 2 == 0 else ["separate", "batch"]
    if round_number % 2 == 0:
        batch, batch_record = client.tool("read_files", arguments)
    single_values, single_records = [], []
    for index, file_arguments in enumerate(requests):
        value, record = client.tool("read_file", file_arguments)
        single_values.append(value)
        single_records.append(record)
        fixture = corpus.get(Path(file_arguments["path"]).name)
        if fixture and fixture["expected_text"] is not None:
            add_check(case, f"single-{index}-literal-fixture", value.get("status") == "ok" and value.get("text") == fixture["expected_text"])
    if round_number % 2 != 0:
        batch, batch_record = client.tool("read_files", arguments)
    results = check_batch_structure(case, batch, requests)
    returned = []
    for index, item in enumerate(results[:len(requests)]):
        result = item.get("result", {})
        returned.append(measured_text(result))
        if item.get("status") == "ok":
            add_check(case, f"batch-{index}-matches-separate-read", result == single_values[index])
        elif label != "maximum":
            add_check(case, f"batch-{index}-failure-matches-separate", result.get("status") == single_values[index].get("status") == item.get("status"))
    if label == "ordinary":
        add_check(case, "whole-batch-ok", batch.get("status") == "ok" and all(item.get("status") == "ok" for item in results))
    elif label == "failure":
        add_check(case, "partial-failures-kept", batch.get("status") == "partial" and batch.get("failed") == 3 and batch.get("not_run") == 0)
        add_check(case, "later-valid-item-still-read", len(results) == 5 and results[-1].get("status") == "ok")
    else:
        statuses = [item.get("status") for item in results]
        add_check(case, "output-limit-retains-all-32-states", batch.get("status") == "output_limit" and len(results) == 32 and "output_limit" in statuses and "not_run" in statuses)
        add_check(case, "earlier-successes-preserved", "ok" in statuses)
        if "output_limit" in statuses:
            add_check(case, "all-following-items-not-run", all(status == "not_run" for status in statuses[statuses.index("output_limit") + 1:]))
    add_check(case, "valid-frame-below-global-bound", batch_record["mcp_response_utf8_bytes"] < MAX_FRAME)
    # Standard JSON serialization independently bounds the text string. The
    # raw frame remains the source of truth for complete MCP wire bytes.
    inner = Path(batch_record["internal_json_file"]).read_bytes().decode("utf-8")
    quoted_size = len(json.dumps(inner, ensure_ascii=False).encode("utf-8"))
    case["quoted_internal_json_utf8_bytes"] = quoted_size
    add_check(case, "quoted-result-within-selected-budget", quoted_size <= MAX_BATCH)
    bodies = [measured_text(value) for value in single_values]
    case["separate"] = measurement(single_records, sum(value.get("status") == "ok" for value in single_values), len(requests), bodies)
    case["batch"] = measurement([batch_record], sum(item.get("status") == "ok" for item in results), len(requests), returned)
    case["item_statuses"] = [item.get("status") for item in results]
    return finish_case(case)


def interrupted_read(client, label, requests):
    case = {"name": label, "checks": []}
    args = {"files": requests, "max_total_bytes": MAX_BATCH, "max_output_bytes": MAX_BATCH,
            "timeout_ms": 1 if label == "deadline" else 10000}
    value, record = client.tool("read_files", args, cancel=label == "cancellation")
    expected = "timed_out" if label == "deadline" else "cancelled"
    items = check_batch_structure(case, value, requests)
    add_check(case, "actual-stop-reason", value.get("status") == expected, value.get("status"))
    add_check(case, "32-states-with-unrun-suffix", len(items) == 32 and any(item.get("status") == "not_run" for item in items))
    statuses = [item.get("status") for item in items]
    add_check(case, "valid-fixtures-only-success-stop-or-not-run", all(
        status in ("ok", expected, "not_run") for status in statuses) and statuses.count(expected) <= 1)
    if expected in statuses:
        stop = statuses.index(expected)
        add_check(case, "interrupted-item-separates-success-and-not-run", all(
            status == "ok" for status in statuses[:stop]) and all(status == "not_run" for status in statuses[stop + 1:]))
    add_check(case, "retained-successes-match-literal-fixture", all(
        item.get("result", {}).get("text") == "a\n" for item in items if item.get("status") == "ok"))
    if "not_run" in statuses:
        add_check(case, "unrun-items-form-suffix", all(status == "not_run" for status in statuses[statuses.index("not_run"):]))
    post, post_record = client.tool("tool_info", {})
    add_check(case, "subsequent-session-usable", post.get("status") == "ok")
    case.update(record=record, post_record=post_record, item_statuses=[item.get("status") for item in items])
    return finish_case(case)


def low_text_budget(client, requests, corpus):
    case = {"name": "low-total-text-budget", "checks": []}
    selected = requests[:3]
    expected_text = corpus[Path(selected[0]["path"]).name]["expected_text"]
    budget = len(expected_text.encode("utf-8")) + 1
    value, record = client.tool("read_files", {"files": selected, "max_total_bytes": budget,
                                "max_output_bytes": MAX_BATCH, "timeout_ms": 10000})
    items = check_batch_structure(case, value, selected)
    add_check(case, "text-budget-stops-whole-item", value.get("status") == "output_limit" and
              [item.get("status") for item in items] == ["ok", "output_limit", "not_run"])
    add_check(case, "first-item-matches-literal-fixture", bool(items) and
              items[0].get("result", {}).get("text") == expected_text)
    add_check(case, "text-budget-is-independent-of-output-budget", record["mcp_response_utf8_bytes"] < MAX_BATCH // 2)
    add_check(case, "requested-budgets-reported", value.get("max_total_bytes") == budget and
              value.get("max_output_bytes") == MAX_BATCH)
    case.update(record=record, text_budget=budget, item_statuses=[item.get("status") for item in items])
    return finish_case(case)


def path_preflight(client, valid_request, corpus):
    case = {"name": "code-shaped-path-preflight", "checks": []}
    before, before_record = client.tool("tool_info", {})
    forbidden = {"path": corpus["code-shaped $(literal).txt"]["path"], "encoding": "utf-8", "raw": True}
    value, record = client.tool("read_files", {"files": [valid_request, forbidden]})
    after, after_record = client.tool("tool_info", {})
    add_check(case, "whole-input-preflight-rejects-code-shaped-path", value.get("status") == "validation_error")
    add_check(case, "no-files-read-before-rejection", before.get("document_loads") is not None and before.get("document_loads") == after.get("document_loads"))
    add_check(case, "subsequent-session-usable", after.get("status") == "ok")
    case.update(record=record, before_record=before_record, after_record=after_record)
    return finish_case(case)


def path_semantics(client, label, path, expected_text):
    case = {"name": "posix-path-" + label, "checks": []}
    request = {"path": path, "encoding": "utf-8", "raw": True, "max_bytes": MAX_TEXT}
    separate, separate_record = client.tool("read_file", request)
    batch, batch_record = client.tool("read_files", {"files": [request]})
    items = check_batch_structure(case, batch, [request])
    add_check(case, "batch-preserves-native-path-resolution", len(items) == 1 and items[0].get("result") == separate)
    if expected_text is None:
        add_check(case, "missing-component-remains-failure", separate.get("status") != "ok" and batch.get("status") == "partial")
    else:
        add_check(case, "symlink-parent-matches-literal-fixture", separate.get("status") == "ok" and
                  separate.get("text") == expected_text and batch.get("status") == "ok")
    case.update(separate_record=separate_record, batch_record=batch_record)
    return finish_case(case)


def http_comparison(client, server, payloads, label, round_number):
    case = {"name": "http-" + label, "round": round_number, "checks": [], "representations": {}}
    path = "/" + label
    expected_status, expected_bytes = payloads[path]
    values = []
    for representation in (None, "both", "text", "base64"):
        args = {"url": server.url(path), "method": "GET", "proxy": "direct", "timeout_ms": 5000,
                "response_limit_bytes": MAX_TEXT}
        if representation is not None:
            args["representation"] = representation
        before = len(server.requests)
        value, record = client.tool("http", args)
        values.append(value)
        name = representation or "default"
        add_check(case, name + "-one-request-no-replay", len(server.requests) == before + 1)
        add_check(case, name + "-http-code", value.get("status_code") == expected_status)
        add_check(case, name + "-transport-outcome", value.get("status") == "completed" and value.get("remote_outcome") == "known" and value.get("response_complete") is True and value.get("cleanup_complete") is True)
        needs_text = representation != "base64"
        needs_base64 = representation != "text" or label == "invalid"
        add_check(case, name + "-exact-representation-fields", ("body" in value) == needs_text and
                  ("body_base64" in value) == needs_base64)
        text_data = value["body"].encode("utf-8") if isinstance(value.get("body"), str) else None
        base64_data = base64.b64decode(value["body_base64"], validate=True) if isinstance(value.get("body_base64"), str) else None
        if needs_text:
            add_check(case, name + "-text-matches-fixture", "body" in value and
                      (value["body"] is None if label == "invalid" else text_data == expected_bytes))
        if needs_base64:
            add_check(case, name + "-base64-matches-fixture", base64_data == expected_bytes)
        data = base64_data if needs_base64 else text_data
        expected_result = "completed"
        add_check(case, name + "-status", value.get("status") == expected_result)
        add_check(case, name + "-decode-status", value.get("body_encoding_status") == ("encoding_error" if label == "invalid" else "ok"))
        add_check(case, name + "-http-success", value.get("http_success") == (200 <= expected_status < 300))
        case["representations"][name] = {"calls": 1, "body_bytes": len(data) if data is not None else None,
            "body_sha256": digest(data) if data is not None else None, "record": record,
            "text_body_sha256": digest(text_data) if text_data is not None else None,
            "base64_body_sha256": digest(base64_data) if base64_data is not None else None,
            "body_recovery_success_rate": int(data == expected_bytes and value.get("status") == expected_result),
            "http_success": value.get("http_success"), "body_encoding_status": value.get("body_encoding_status"),
            "retry_count": 0}
    semantic = {key: value for key, value in values[0].items() if key not in ("elapsed_ms", "body", "body_base64")}
    for index, value in enumerate(values[1:], 1):
        add_check(case, f"representation-{index}-same-outcomes", {key: item for key, item in value.items() if key not in ("elapsed_ms", "body", "body_base64")} == semantic)
    return finish_case(case)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, choices=(1, 2, 3), default=3)
    parser.add_argument("--observation-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = args.executable.resolve(strict=True)
    output = args.output.resolve()
    if not output.is_relative_to((root / "build").resolve()) or output.exists():
        parser.error("choose a new receipt under this checkout's ignored build directory")
    archive = output.with_suffix("")
    if archive.exists():
        parser.error("receipt artifact directory already exists")
    archive.mkdir(parents=True)
    fixture_root = archive / "fixtures"
    receipt = {"schema": 2, "status": "FAIL", "python": sys.version, "platform": sys.platform,
        "harness_sha256": digest(Path(__file__).read_bytes()),
        "executable": str(binary), "executable_sha256": digest(binary.read_bytes()),
        "source_files_sha256": {path: digest((root / path).read_bytes()) for path in SOURCE_FILES},
        "rounds": args.rounds, "processes": [], "cases": [], "budget_seconds": 240,
        "model_tokens": "NOT_MEASURED", "provider_usage": "NOT_MEASURED", "cost": "NOT_MEASURED",
        "peak_memory": "NOT_MEASURED", "physical_io": "NOT_MEASURED", "retry_count": 0,
        "notes": ["Internal JSON is content[0].text UTF-8, full MCP bytes include the wire newline.",
                  "Raw fixture hashes and decoded selected-text hashes are distinct, especially for GBK/BOM.",
                  "No cache flush, tokenizer/provider sampling, IO speedup or statistical significance is claimed.",
                  "Maximum batch intentionally returns partial content; compare retained hashes, not full-task success."]}
    client = None
    server = None
    fixture_owner = None
    started = time.perf_counter()
    deadline = time.monotonic() + 240
    try:
        if os.name != "nt":
            fixture_owner = tempfile.TemporaryDirectory(prefix="cdx11c-", dir="/tmp")
            fixture_root = Path(fixture_owner.name) / "fixtures"
            with Path("/proc/self/mountinfo").open("rb") as mounts:
                mount_data = mounts.read(MAX_TEXT + 1)
            if len(mount_data) > MAX_TEXT:
                raise AssertionError("mount metadata exceeds acceptance bound")
            candidates = []
            for line in mount_data.decode("utf-8").splitlines():
                fields, filesystem = line.split(" - ", 1)
                mount_path = fields.split()[4]
                if fixture_root.is_relative_to(mount_path):
                    candidates.append((len(mount_path), mount_path, filesystem.split()[0]))
            _, mount_path, filesystem = max(candidates)
            receipt["fixture_mount"] = {"path": mount_path, "filesystem": filesystem}
            if filesystem != "ext4":
                raise AssertionError("POSIX path acceptance requires a real ext4 /tmp fixture")
        receipt["fixture_root"] = str(fixture_root)
        corpus, ordinary, failures, maximum, interrupt_files, path_cases = make_corpus(fixture_root)
        receipt["corpus"] = {name: {key: value for key, value in item.items() if key != "expected_text"} for name, item in corpus.items()}
        save_json(archive / "corpus.json", receipt["corpus"])
        client = Mcp(binary, root, archive / "mcp", deadline, receipt)
        initialized, init_record = client.initialize()
        listed, list_record = client.rpc("tools/list", {})
        info, info_record = client.tool("tool_info", {})
        receipt.update(initialize=initialized, initialize_record=init_record, tools_list_record=list_record,
                       tool_info=info, tool_info_record=info_record)
        tools = {tool["name"]: tool for tool in listed["tools"]}
        receipt["tools"] = list(tools)
        required = {"read_file", "read_files", "tool_info"}
        if os.name == "nt":
            required.add("http")
        missing = sorted(required.difference(tools))
        receipt["required_capabilities"] = {name: "NOT_RUN" if name in missing else "PRESENT" for name in sorted(required)}
        if args.observation_only:
            receipt["status"] = "OBSERVATION"
            receipt["required_capabilities"] = {name: "NOT_RUN" for name in required}
            receipt["missing_tools"] = missing
        else:
            if missing:
                raise AssertionError("required capabilities NOT_RUN: " + ", ".join(missing))
            if "files" not in tools["read_files"]["inputSchema"]["properties"]:
                raise AssertionError("read_files/files contract missing")
            for round_number in range(1, args.rounds + 1):
                for label, requests in (("ordinary", ordinary), ("failure", failures), ("maximum", maximum)):
                    case = read_comparison(client, label, round_number, requests, corpus)
                    receipt["cases"].append(case)
                    save_json(output, receipt)
                    print(json.dumps({"round": round_number, "case": label, "status": case["status"]}), flush=True)
            for label in ("cancellation", "deadline"):
                receipt["cases"].append(interrupted_read(client, label, interrupt_files))
            receipt["cases"].append(low_text_budget(client, ordinary, corpus))
            receipt["cases"].append(path_preflight(client, ordinary[0], corpus))
            for label, path, expected_text in path_cases:
                receipt["cases"].append(path_semantics(client, label, path, expected_text))
            if os.name == "nt":
                expected_enum = ["both", "text", "base64"]
                if tools["http"]["inputSchema"]["properties"]["representation"]["enum"] != expected_enum:
                    raise AssertionError("http representation contract differs")
                payloads = {"/ordinary": (200, ("HTTP 中文 \"quote\" \\path\r\n" * 2000).encode()),
                            "/failure": (404, b"fixture missing\n"),
                            "/maximum": (200, (b'"\\\n' * (MAX_TEXT // 3 + 1))[:MAX_TEXT]),
                            "/invalid": (200, b"raw\xff\x81\x00bytes")}
                receipt["http_payloads"] = {name: {"status_code": code, "bytes": len(data), "sha256": digest(data)} for name, (code, data) in payloads.items()}
                server = HttpFixture(payloads, deadline)
                for round_number in range(1, args.rounds + 1):
                    for label in ("ordinary", "failure", "maximum", "invalid"):
                        case = http_comparison(client, server, payloads, label, round_number)
                        receipt["cases"].append(case)
                        save_json(output, receipt)
                        print(json.dumps({"round": round_number, "case": "http-" + label, "status": case["status"]}), flush=True)
                receipt["http_requests"] = server.requests
            else:
                receipt["http_status"] = "NOT_RUN: Windows backend only"
            receipt["status"] = "PASS" if all(case["status"] == "PASS" for case in receipt["cases"]) else "FAIL"
    except BaseException as error:
        receipt["status"] = "FAIL"
        receipt["error"] = f"{type(error).__name__}: {error}"
    finally:
        if server:
            receipt["http_cleanup_complete"] = server.close()
        if client:
            client.close()
        receipt["executable_sha256_after"] = digest(binary.read_bytes())
        receipt["source_files_sha256_after"] = {path: digest((root / path).read_bytes()) for path in SOURCE_FILES}
        receipt["binary_unchanged"] = receipt["executable_sha256_after"] == receipt["executable_sha256"]
        receipt["source_unchanged"] = receipt["source_files_sha256_after"] == receipt["source_files_sha256"]
        receipt["fixtures_unchanged"] = all(digest(Path(item["path"]).read_bytes()) == item["raw_sha256"] for item in receipt.get("corpus", {}).values())
        if fixture_owner:
            fixture_owner.cleanup()
        elif fixture_root.is_dir() and fixture_root.parent == archive and fixture_root.name == "fixtures":
            shutil.rmtree(fixture_root)
        receipt["fixture_cleaned"] = not fixture_root.exists() and (fixture_owner is None or not Path(fixture_owner.name).exists())
        receipt["elapsed_seconds"] = time.perf_counter() - started
        receipt["case_success_rate"] = (sum(case["status"] == "PASS" for case in receipt["cases"]) / len(receipt["cases"])) if receipt["cases"] else None
        receipt["failed_cases"] = [case["name"] for case in receipt["cases"] if case["status"] != "PASS"]
        receipt["normal_process_exit"] = bool(receipt["processes"]) and all(
            proc["cleanup_complete"] and proc.get("exit_code") == 0 and not proc.get("forced_termination")
            for proc in receipt["processes"])
        receipt["cleanup_passed"] = receipt["normal_process_exit"] and (server is None or receipt.get("http_cleanup_complete") is True)
        if not receipt["binary_unchanged"] or not receipt["source_unchanged"] or not receipt["fixtures_unchanged"] or not receipt["fixture_cleaned"] or not receipt["cleanup_passed"]:
            receipt["status"] = "FAIL"
        save_json(output, receipt)
    print(json.dumps({"status": receipt["status"], "receipt": str(output), "cases": len(receipt["cases"]), "failed_cases": receipt["failed_cases"]}), flush=True)
    return 0 if receipt["status"] in ("PASS", "OBSERVATION") else 1


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_args: (_ for _ in ()).throw(KeyboardInterrupt()))
    raise SystemExit(main())
