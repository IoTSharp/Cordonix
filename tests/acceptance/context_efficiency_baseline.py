"""Bounded CDX-14C context-efficiency and task-success baseline.

The harness talks to a fresh real Cordonix stdio process. It records complete
MCP frames and hashes, then reports byte-level and outcome metrics. Tokenizer
and provider usage are intentionally NOT_MEASURED unless an explicitly named
local tokenizer command is supplied; bytes/4 is never treated as tokens.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import signal
import subprocess
import sys
import tempfile
import threading
import time

sys.dont_write_bytecode = True

MAX_FRAME = 8 * 1024 * 1024
MAX_RESPONSES = 256
MAX_CALLS = 64
TOKENIZER_BATCH_SIZE = 60
MAX_TOKENIZER_SESSIONS = 2
DEFAULT_DEADLINE_SECONDS = 120


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def frame(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize(records: list[dict]) -> dict:
    sizes = [float(record["mcp_response_utf8_bytes"]) for record in records if record.get("mcp_response_utf8_bytes") is not None]
    elapsed = [float(record["elapsed_ms"]) for record in records if record.get("elapsed_ms") is not None]
    return {
        "calls": len(records),
        "retries": sum(int(record.get("retry_count", 0)) for record in records),
        "success": sum(record.get("outcome") == "success" for record in records),
        "unknown": sum(record.get("outcome") == "unknown" for record in records),
        "failed": sum(record.get("outcome") == "failed" for record in records),
        "median_mcp_response_bytes": percentile(sizes, 0.5),
        "p95_mcp_response_bytes": percentile(sizes, 0.95),
        "median_elapsed_ms": percentile(elapsed, 0.5),
        "p95_elapsed_ms": percentile(elapsed, 0.95),
        "records": records,
    }


class StdioClient:
    def __init__(self, executable: Path, cwd: Path, archive: Path, deadline: float, receipt: dict, profile: str | None = None):
        self.deadline = deadline
        self.archive = archive
        self.archive.mkdir(parents=True, exist_ok=False)
        argv = [str(executable), "--mcp"] + ([] if profile is None else ["--tool-profile", profile])
        self.process = subprocess.Popen(
            argv, cwd=str(cwd), stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            start_new_session=os.name != "nt",
        )
        self.accounting = None
        self.sequence = 0
        self.transport_failed = False
        self.responses: queue.Queue[object] = queue.Queue(maxsize=16)
        self.stderr = bytearray()
        self.audit = {"pid": self.process.pid, "parent_pid": os.getpid(),
                      "program": str(executable), "args": argv[1:], "cwd": str(cwd),
                      "started_ns": time.time_ns(), "cleanup_complete": False,
                      "forced_termination": False, "rpc_calls": 0}
        receipt.setdefault("processes", []).append(self.audit)
        if os.name == "nt":
            from benchmark_workspace import ProcessAccounting
            self.accounting = ProcessAccounting(self.process)
            self.audit["creation_filetime"] = self.accounting.snapshot()["creation_filetime"]
        else:
            self.audit["owned_process_group"] = os.getpgid(self.process.pid)
        self.readers = [threading.Thread(target=self._read_stdout, daemon=True),
                        threading.Thread(target=self._read_stderr, daemon=True)]
        for reader in self.readers:
            reader.start()

    def _read_stdout(self) -> None:
        try:
            for index in range(MAX_RESPONSES):
                raw = self.process.stdout.readline(MAX_FRAME + 1)
                if not raw:
                    return
                if len(raw) > MAX_FRAME or not raw.endswith(b"\n"):
                    raise ValueError("MCP frame exceeds bounded 8 MiB limit")
                name = f"response-{index:04d}.json"
                (self.archive / name).write_bytes(raw)
                self.responses.put((raw, json.loads(raw.decode("utf-8")), name), timeout=2)
            raise ValueError("MCP response-count bound exceeded")
        except BaseException as error:
            try:
                self.responses.put(error, timeout=1)
            except queue.Full:
                pass

    def _read_stderr(self) -> None:
        for _ in range(64):
            if time.monotonic() >= self.deadline:
                return
            chunk = self.process.stderr.read(4096)
            if not chunk:
                return
            self.stderr.extend(chunk)

    def rpc(self, method: str, params: dict, *, cancel: bool = False) -> tuple[dict, dict]:
        if self.transport_failed:
            raise RuntimeError("MCP transport previously failed; this session cannot dispatch another request")
        if self.sequence >= MAX_CALLS or time.monotonic() >= self.deadline:
            raise TimeoutError("acceptance call-count/deadline reached")
        self.sequence += 1
        self.audit["rpc_calls"] += 1
        request = frame({"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params})
        request_path = self.archive / f"request-{self.sequence:04d}.json"
        request_path.write_bytes(request)
        notification = b""
        if cancel:
            notification = frame({"jsonrpc": "2.0", "method": "notifications/cancelled",
                                  "params": {"requestId": self.sequence, "reason": "bounded acceptance"}})
            (self.archive / f"cancel-{self.sequence:04d}.json").write_bytes(notification)
        started = time.perf_counter()
        record = {"request_id": self.sequence, "method": method,
                  "request_utf8_bytes": len(request), "request_file": str(request_path),
                  "mcp_response_utf8_bytes": None, "mcp_response_sha256": None,
                  "response_file": None, "rpc_error": None, "retry_count": 0}
        try:
            self.process.stdin.write(request)
            self.process.stdin.flush()
            if notification:
                # Separate frames allow the bounded worker to start before cancel.
                time.sleep(min(0.05, max(0.0, self.deadline - time.monotonic())))
                self.process.stdin.write(notification)
                self.process.stdin.flush()
            for _ in range(32):
                remaining = min(20.0, self.deadline - time.monotonic())
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
                record.update(mcp_response_utf8_bytes=len(raw), mcp_response_sha256=sha256(raw),
                              response_file=str(self.archive / name), rpc_error=response.get("error"))
                return response, record
            raise ValueError("notification-count bound reached")
        except (OSError, queue.Empty, TimeoutError, ValueError) as error:
            self.transport_failed = True
            record.update(transport_error=f"{type(error).__name__}: {error}", outcome="unknown",
                          acceptance_success=None)
            self.audit.setdefault("transport_failures", []).append(record)
            return {}, record
        finally:
            record["elapsed_ms"] = (time.perf_counter() - started) * 1000.0

    def initialize(self) -> None:
        response, record = self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                  "clientInfo": {"name": "cdx-context-efficiency-baseline", "version": "1"}})
        if record.get("transport_error") or response.get("error"):
            raise RuntimeError("MCP initialization failed; no task request was dispatched")
        raw = frame({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})
        self.archive.joinpath("notification-initialized.json").write_bytes(raw)
        self.process.stdin.write(raw)
        self.process.stdin.flush()

    def tools_list(self, profile: str | None) -> tuple[dict, dict]:
        response, record = self.rpc("tools/list", {})
        record["profile"] = "default" if profile is None else profile
        result = response.get("result")
        if isinstance(result, dict):
            tools = result.get("tools")
            if isinstance(tools, list):
                record["tool_count"] = len(tools)
                record["tool_names"] = [item.get("name") for item in tools if isinstance(item, dict)]
                tools_bytes = json.dumps(tools, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
                record["tools_array_utf8_bytes"] = len(tools_bytes)
                record["tool_schema_sha256"] = sha256(tools_bytes)
        record["acceptance_success"] = not response.get("error") and "tool_count" in record
        record["outcome"] = "unknown" if record.get("transport_error") else ("success" if record["acceptance_success"] else "failed")
        return response, record

    def execute(self, arguments: dict, mode: str, cancel: bool = False, expected_status: str = "completed") -> tuple[dict, dict]:
        response, record = self.rpc("tools/call", {"name": "execute_program", "arguments": arguments}, cancel=cancel)
        result = response.get("result") or {}
        text = None
        if isinstance(result, dict) and result.get("content"):
            text = result["content"][0].get("text")
        value = {}
        if isinstance(text, str):
            data = text.encode("utf-8")
            record["tool_result_utf8_bytes"] = len(data)
            record["tool_result_sha256"] = sha256(data)
            record["tool_result"] = json.loads(text)
            value = record["tool_result"]
        else:
            record["tool_result_utf8_bytes"] = None
            record["tool_result_sha256"] = None
        status = value.get("status") if isinstance(value, dict) else None
        process_status = value.get("process_status") if isinstance(value, dict) else None
        record["mode"] = mode
        record["status"] = status
        record["process_status"] = process_status
        record["expected_status"] = expected_status
        record["observed_outcome"] = "unknown" if record.get("transport_error") or status == "unknown" else "returned"
        record["acceptance_success"] = status == expected_status and not response.get("error") and not record.get("transport_error")
        record["outcome"] = "unknown" if record["observed_outcome"] == "unknown" else ("success" if record["acceptance_success"] else "failed")
        return response, record

    def close(self) -> None:
        try:
            if self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.audit["forced_termination"] = True
                if self.accounting:
                    self.accounting.kernel.TerminateJobObject(self.accounting.job, 1)
                elif self.audit.get("owned_process_group") == self.process.pid:
                    os.killpg(self.process.pid, signal.SIGKILL)
                else:
                    self.process.kill()
                self.process.wait(timeout=5)
        finally:
            if self.accounting:
                self.accounting.close()
            for reader in self.readers:
                reader.join(timeout=3)
            self.audit["exit_code"] = self.process.poll()
            self.audit["cleanup_complete"] = self.process.poll() is not None and all(not r.is_alive() for r in self.readers)
            self.archive.joinpath("stderr.bin").write_bytes(self.stderr)
            for pipe in (self.process.stdout, self.process.stderr):
                pipe.close()


def load_cases(path: Path) -> list[dict]:
    value = json.loads(path.read_text(encoding="utf-8"))
    cases = value.get("execute_cases")
    if not isinstance(cases, list) or not cases or len(cases) > 16:
        raise ValueError("fixed corpus must contain 1..16 cases")
    return cases


def add_check(receipt: dict, name: str, passed: bool, detail: object = None) -> None:
    receipt["checks"].append({"name": name, "status": "PASS" if passed else "FAIL", "detail": detail})


def apply_tokenizer(receipt: dict, config_path: Path | None, deadline: float, executable: Path, root: Path, archive: Path) -> None:
    if config_path is None:
        return
    config = json.loads(config_path.read_text(encoding="utf-8"))
    argv = config.get("argv")
    if not config.get("name") or not config.get("version") or not isinstance(argv, list) or not argv or not all(isinstance(v, str) for v in argv):
        raise ValueError("tokenizer config requires explicit name, version and native argv array")
    if not Path(argv[0]).is_absolute():
        raise ValueError("tokenizer argv[0] must be an explicit absolute executable")
    samples = receipt["tools_list"] + receipt["execute"]["full"] + receipt["execute"]["summary"]
    if len(samples) > TOKENIZER_BATCH_SIZE * MAX_TOKENIZER_SESSIONS:
        raise ValueError("tokenizer sample bound exceeded")
    receipt["tokenizer"] = {"status": "NOT_MEASURED", "name": config["name"], "version": config["version"], "argv": argv,
                            "sample_count": len(samples), "measured_samples": 0, "tokens": None,
                            "input_contract": "exact UTF-8 MCP response bytes on stdin; JSON {tokens: nonnegative integer} on stdout"}
    # The native execution boundary owns tokenizer descendants and caps output.
    # This auxiliary session is outside the measured task-call totals.
    for batch in range(MAX_TOKENIZER_SESSIONS):
        selected = samples[batch * TOKENIZER_BATCH_SIZE:(batch + 1) * TOKENIZER_BATCH_SIZE]
        if not selected:
            break
        client = StdioClient(executable, root, archive / f"tokenizer-{batch + 1:02d}", deadline, receipt)
        try:
            client.initialize()
            for sample in selected:
                remaining = min(10.0, deadline - time.monotonic())
                if remaining <= 0:
                    raise TimeoutError("tokenizer deadline reached")
                raw = Path(sample["response_file"]).read_bytes()
                _, record = client.execute({"program": argv[0], "args": argv[1:], "cwd": str(root),
                                            "stdin": raw.decode("utf-8"), "timeout_ms": max(1, int(remaining * 1000)),
                                            "output_limit_bytes": 4096}, "tokenizer")
                result = record.get("tool_result", {})
                if result.get("status") != "completed" or not result.get("cleanup_complete"):
                    raise ValueError("explicit tokenizer execution did not complete cleanly")
                value = json.loads(result.get("stdout", ""))
                tokens = value.get("tokens")
                if isinstance(tokens, bool) or not isinstance(tokens, int) or tokens < 0:
                    raise ValueError("tokenizer must return exact nonnegative integer tokens")
                sample["tokenizer_tokens"] = tokens
                receipt["tokenizer"]["measured_samples"] += 1
        finally:
            client.close()
        if not client.audit["cleanup_complete"]:
            raise RuntimeError("tokenizer MCP cleanup incomplete")
    receipt["tokenizer"]["status"] = "MEASURED"
    receipt["tokenizer"]["tokens"] = sum(sample["tokenizer_tokens"] for sample in samples)


def aggregate_receipt(receipt: dict) -> None:
    for mode in ("full", "summary"):
        receipt["execute"][mode + "_summary"] = summarize(receipt["execute"][mode])
    receipt["tools_list_summary"] = {profile: summarize([record for record in receipt["tools_list"] if record["profile"] == profile]) for profile in ("default", "full", "local-code")}
    receipt["transport_unknown_rpc_calls"] = sum(len(process.get("transport_failures", [])) for process in receipt.get("processes", []))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--process-fixture", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--deadline-seconds", type=float, default=DEFAULT_DEADLINE_SECONDS)
    parser.add_argument("--tokenizer-config", type=Path, help="Explicit local name/version/native argv JSON; otherwise NOT_MEASURED")
    parser.add_argument("--pilot", action="store_true", help="Run one repetition and one execute case")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    build_root = (root / "build").resolve()
    if not output.is_relative_to(build_root) or output.exists():
        parser.error("--output must be a new path under this checkout's build directory")
    executable = args.executable.resolve(strict=True)
    default_fixture_name = "cdx-process-fixture.exe" if os.name == "nt" else "cdx-process-fixture"
    default_fixture = root / "build" / "process" / default_fixture_name
    if not default_fixture.exists():
        default_fixture = root / "build" / "linux" / "tests" / default_fixture_name
    fixture = (args.process_fixture or default_fixture).resolve(strict=True)
    if args.repetitions < 1 or args.repetitions > 5 or not 0 < args.deadline_seconds <= 600:
        parser.error("repetitions must be 1..5 and deadline-seconds must be >0..600")
    corpus_path = Path(__file__).with_name("context_efficiency_corpus.json")
    cases = load_cases(corpus_path)
    if args.pilot:
        args.repetitions = 1
        cases = cases[:1]
    output.parent.mkdir(parents=True, exist_ok=True)
    archive = Path(tempfile.mkdtemp(prefix="context-efficiency-", dir=str(output.parent))).resolve()
    receipt = {"status": "FAIL", "schema": 1, "platform": sys.platform, "python": sys.version,
               "executable": str(executable), "executable_sha256": sha256(executable.read_bytes()),
               "corpus": {"path": str(corpus_path), "sha256": sha256(corpus_path.read_bytes()), "cases": cases},
               "archive": str(archive), "repetitions": args.repetitions, "deadline_seconds": args.deadline_seconds,
               "tools_list": [], "execute": {"full": [], "summary": []}, "processes": [], "checks": [],
               "profile_direct_calls": [],
               "tokenizer": {"status": "NOT_MEASURED", "reason": "no explicit tokenizer command supplied"},
               "provider_usage": "NOT_MEASURED", "notes": [
                   "MCP response bytes are UTF-8 wire frames; tool_result bytes are the inner text JSON.",
                   "Hashes cover the exact captured bytes saved in the archive.",
                   "No bytes-to-token conversion or model cost claim is made.",
                   "Each fresh session is closed before the next repetition; no command is replayed after an unknown result."]}
    started = time.monotonic()
    deadline = started + args.deadline_seconds
    try:
        for repeat in range(args.repetitions):
            if time.monotonic() >= deadline:
                raise TimeoutError("baseline deadline reached")
            lists = {}
            for profile in (None, "full", "local-code"):
                client = StdioClient(executable, root, archive / f"repeat-{repeat + 1:02d}-{profile or 'default'}", deadline, receipt, profile)
                try:
                    client.initialize()
                    _, record = client.tools_list(profile)
                    receipt["tools_list"].append(record)
                    lists[profile] = record
                    if record.get("transport_error"):
                        raise RuntimeError(record["transport_error"])
                    if profile == "local-code":
                        hidden_response, hidden_record = client.rpc(
                            "tools/call", {"name": "ssh", "arguments": {"target": {"host": "127.0.0.1", "port": 0}}})
                        hidden_record["profile"] = profile
                        hidden_record["outcome"] = "unknown" if hidden_record.get("transport_error") else ("success" if not hidden_response.get("error") else "failed")
                        receipt["profile_direct_calls"].append(hidden_record)
                        hidden_result = hidden_response.get("result") or {}
                        hidden_content = hidden_result.get("content") if isinstance(hidden_result, dict) else None
                        hidden_text = hidden_content[0].get("text") if isinstance(hidden_content, list) and hidden_content else None
                        hidden_value = json.loads(hidden_text) if isinstance(hidden_text, str) else {}
                        add_check(receipt, f"repeat-{repeat + 1}-local-code-direct-hidden-call",
                                  not hidden_response.get("error") and hidden_value.get("status") == "validation_error")
                finally:
                    client.close()
            add_check(receipt, f"repeat-{repeat + 1}-default-full-schema", lists[None].get("tool_schema_sha256") is not None and lists[None].get("tool_schema_sha256") == lists["full"].get("tool_schema_sha256"))
            add_check(receipt, f"repeat-{repeat + 1}-local-code-proper-subset", set(lists["local-code"].get("tool_names", [])) < set(lists["full"].get("tool_names", [])))
            client = StdioClient(executable, root, archive / f"repeat-{repeat + 1:02d}-execute", deadline, receipt)
            try:
                client.initialize()
                for mode in ("full", "summary"):
                    for case in cases:
                        arguments = {"program": str(fixture), "args": case["args"], "cwd": str(root),
                                     "timeout_ms": case.get("timeout_ms", 5000),
                                     "output_limit_bytes": case.get("output_limit_bytes", 32768),
                                     "output_encoding": case.get("output_encoding", "utf-8")}
                        arguments["result_mode"] = mode
                        log_path = archive / f"capture-{repeat + 1:02d}-{mode}-{case['name']}.bin"
                        if mode == "summary":
                            arguments["capture_log_path"] = str(log_path)
                            arguments["summary_tail_bytes"] = 128
                        _, record = client.execute(arguments, mode, cancel=bool(case.get("cancel")), expected_status=case.get("expected_status"))
                        record["case"] = case["name"]
                        record["expected_status"] = case.get("expected_status")
                        receipt["execute"][mode].append(record)
                        if record.get("transport_error"):
                            raise RuntimeError(record["transport_error"])
                        value = record.get("tool_result", {})
                        add_check(receipt, f"repeat-{repeat + 1}-{mode}-{case['name']}-status", value.get("status") == case.get("expected_status"))
                        if mode == "summary":
                            summary_published = value.get("result_mode") == "summary" and value.get("capture_log_status") == "ok"
                            full_fallback = value.get("result_mode") == "full" and value.get("capture_log_status") not in (None, "not_requested", "ok")
                            add_check(receipt, f"repeat-{repeat + 1}-{case['name']}-summary-or-log-fallback", summary_published or full_fallback)
                            if summary_published and log_path.exists():
                                captured = log_path.read_bytes()
                                record["capture_log_bytes"] = len(captured)
                                record["capture_log_sha256"] = sha256(captured)
                                header, payload = captured.split(b"\n\n", 1) if b"\n\n" in captured else (b"", b"")
                                fields = {}
                                for line in header.splitlines()[1:]:
                                    key, separator, field_value = line.partition(b"=")
                                    if separator:
                                        fields[key.decode("ascii", "strict")] = int(field_value.decode("ascii", "strict"))
                                expected_payload = value.get("stdout_bytes", -1) + value.get("stderr_bytes", -1)
                                add_check(receipt, f"repeat-{repeat + 1}-{case['name']}-capture-hash", value.get("captured_sha256") == sha256(payload))
                                add_check(receipt, f"repeat-{repeat + 1}-{case['name']}-capture-length", expected_payload == len(payload))
                                add_check(receipt, f"repeat-{repeat + 1}-{case['name']}-capture-offsets",
                                          fields.get("stdout_offset") == len(header) + 2 and
                                          fields.get("stderr_offset") == len(header) + 2 + value.get("stdout_bytes", -1) and
                                          fields.get("stderr_bytes") == value.get("stderr_bytes", -1))
                            elif summary_published:
                                add_check(receipt, f"repeat-{repeat + 1}-{case['name']}-capture-exists", False)
            finally:
                client.close()
        add_check(receipt, "all-owned-stdio-processes-cleaned", all(item.get("cleanup_complete") for item in receipt["processes"]))
        add_check(receipt, "binary-unchanged", sha256(executable.read_bytes()) == receipt["executable_sha256"])
        apply_tokenizer(receipt, args.tokenizer_config, deadline, executable, root, archive)
        receipt["status"] = "PASS" if all(check["status"] == "PASS" for check in receipt["checks"]) else "FAIL"
    except BaseException as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        receipt["status"] = "NOT_RUN" if not receipt["tools_list"] else "FAIL"
    finally:
        aggregate_receipt(receipt)
        receipt["total_rpc_calls_including_setup_and_tokenizer"] = sum(process.get("rpc_calls", 0) for process in receipt["processes"])
        receipt["retries"] = 0
        receipt["measurement_scope"] = "tools_list and execute summaries count only measured task calls; total_rpc_calls includes initialize and optional tokenizer calls"
        receipt["elapsed_seconds"] = time.monotonic() - started
        output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "receipt": str(output), "archive": str(archive)}, ensure_ascii=False), flush=True)
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
