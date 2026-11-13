"""Real stdio regression for execute_program full/summary projections.

Uses only Python's standard library and the existing acceptance stdio client.
Every child is owned by Cordonix; its client process has the existing bounded
Job/process-group cleanup. Raw RPC frames and a JSON receipt stay under build/.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import signal
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "acceptance"))
from context_efficiency_baseline import StdioClient


FIXTURE = '''import json, os, subprocess, sys, time
from pathlib import Path
action, counter = sys.argv[1:3]
if action == "descendant":
    time.sleep(8)
    raise SystemExit(0)
with open(counter, "ab") as stream:
    stream.write(b"once\\n")
if action == "streams":
    os.write(1, "中文🙂\\r\\nstdout\\n".encode("utf-8"))
    os.write(2, "错误 stderr\\n".encode("utf-8"))
elif action == "cp936":
    os.write(1, "中文\\r\\n".encode("cp936"))
    os.write(2, "错误\\n".encode("cp936"))
elif action == "invalid":
    os.write(1, b"valid\\xff\\n")
    os.write(2, b"stderr\\x00\\n")
elif action == "output":
    os.write(1, b"x" * 65536)
elif action == "sleep":
    os.write(1, b"started\\n")
    time.sleep(8)
elif action == "exit23":
    os.write(1, b"nonzero\\n")
    raise SystemExit(23)
elif action == "tree":
    child = subprocess.Popen([sys.executable, __file__, "descendant", counter])
    os.write(1, json.dumps({"child_pid":child.pid}).encode("ascii"))
else:
    raise SystemExit(2)
'''


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def full_stream(value: dict, name: str) -> bytes:
    if name + "_base64" in value:
        return base64.b64decode(value[name + "_base64"], validate=True)
    return value[name].encode(value["output_encoding"])


def log_streams(value: dict) -> tuple[bytes, bytes]:
    metadata = value["capture_log"]
    raw = Path(metadata["path"]).read_bytes()
    assert len(raw) == metadata["bytes"]
    end = raw.index(b"\n\n") + 2
    assert end == metadata["header_bytes"] == metadata["stdout_offset"]
    header = raw[:end].decode("ascii").splitlines()
    assert header[0] == "CDX-CAPTURE-1"
    fields = dict(line.split("=", 1) for line in header[1:] if line)
    for name in ("stdout_offset", "stderr_offset"):
        assert int(fields[name]) == metadata[name]
    for name in ("stdout_bytes", "stderr_bytes"):
        assert int(fields[name]) == value[name]
    offset = metadata["stdout_offset"]
    split = metadata["stderr_offset"]
    assert split == offset + value["stdout_bytes"]
    stdout, stderr = raw[offset:split], raw[split:]
    assert len(stdout) == value["stdout_bytes"] and len(stderr) == value["stderr_bytes"]
    return stdout, stderr


def check_capture(value: dict, expected: tuple[bytes, bytes] | None = None, tail: int = 4) -> None:
    if value["capture_log_status"] == "ok":
        assert value["capture_log"]["written_to_disk"] is True
        assert value["capture_log"]["cleanup_complete"] is True
        streams = log_streams(value)
    else:
        assert value["result_mode"] == "full"
        streams = full_stream(value, "stdout"), full_stream(value, "stderr")
    if expected is not None:
        assert streams == expected
    stdout, stderr = streams
    assert value["stdout_bytes"] == len(stdout) and value["stderr_bytes"] == len(stderr)
    assert value["stdout_sha256"] == digest(stdout)
    assert value["stderr_sha256"] == digest(stderr)
    assert value["captured_sha256"] == digest(stdout + stderr)
    assert value["digest_scope"] == "captured_stdout_then_stderr"
    if value["result_mode"] == "summary":
        assert "stdout" not in value and "stderr" not in value
        assert base64.b64decode(value["stdout_tail_base64"], validate=True) == stdout[-tail:]
        assert base64.b64decode(value["stderr_tail_base64"], validate=True) == stderr[-tail:]
        assert value["summary_truncated"] == (len(stdout) > tail or len(stderr) > tail)


def child_stopped(pid: int) -> bool:
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x00100000, False, pid)
        if not handle:
            return ctypes.get_last_error() == 87
        try:
            return kernel.WaitForSingleObject(handle, 0) == 0
        finally:
            kernel.CloseHandle(handle)
    path = Path(f"/proc/{pid}/stat")
    if not path.exists():
        return True
    return path.read_text().rsplit(")", 1)[1].strip().split()[0] == "Z"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New task-owned directory under build/")
    parser.add_argument("--deadline-seconds", type=float, default=120)
    parser.add_argument("--pilot", action="store_true")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = options.output.resolve()
    if output.exists() or not output.is_relative_to((root / "build").resolve()):
        parser.error("output must be a new directory under this checkout's build/")
    if not 0 < options.deadline_seconds <= 120:
        parser.error("deadline-seconds must be >0..120")
    executable = options.executable.resolve(strict=True)
    output.mkdir(parents=True)
    fixture = output / "fixture.py"
    fixture.write_text(FIXTURE, encoding="utf-8", newline="\n")
    receipt = {"status": "FAIL", "executable": str(executable),
               "executable_sha256": digest(executable.read_bytes()),
               "python": sys.executable, "python_version": sys.version,
               "deadline_seconds": options.deadline_seconds, "max_calls": 48,
               "checks": [], "calls": [], "processes": [], "retries": 0}
    started = time.monotonic()
    deadline = started + options.deadline_seconds
    client = None
    interrupted = False

    def stop(signum, _frame):
        nonlocal interrupted
        interrupted = True
        raise KeyboardInterrupt(f"cancelled by signal {signum}")

    signal.signal(signal.SIGINT, stop)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, stop)

    def bounded() -> None:
        if interrupted or len(receipt["calls"]) >= 48 or time.monotonic() >= deadline:
            raise TimeoutError("execution projection call-count/deadline reached")

    def execute(name: str, action: str, mode: str | None = None, *, cancel: bool = False,
                mutate=None) -> tuple[dict, Path]:
        bounded()
        artifact = f"{len(receipt['calls']):02d}-{name}"
        counter = output / (artifact + ".count")
        arguments = {"program": sys.executable, "args": [str(fixture), action, str(counter)],
                     "cwd": str(output), "timeout_ms": 2000, "output_limit_bytes": 8192,
                     "summary_tail_bytes": 4}
        if mode is not None:
            arguments["result_mode"] = mode
            arguments["capture_log_path"] = str(output / (artifact + ".capture"))
        if mutate:
            mutate(arguments)
        response, record = client.execute(arguments, mode or "default", cancel=cancel)
        receipt["calls"].append(dict(record, name=name))
        assert "error" not in response, response
        result = record["tool_result"]
        return result, counter

    def check(name: str, function) -> None:
        bounded()
        try:
            function()
            receipt["checks"].append({"name": name, "status": "PASS"})
            print(name + ": PASS", flush=True)
        except Exception as error:
            receipt["checks"].append({"name": name, "status": "FAIL", "error": repr(error)})
            print(name + ": FAIL " + repr(error), flush=True)

    def invalid_cases() -> None:
        cases = [("invalid-mode", lambda a: a.update(result_mode="compact")),
                 ("summary-no-log", lambda a: a.pop("capture_log_path")),
                 ("summary-relative-log", lambda a: a.update(capture_log_path="relative.capture")),
                 ("summary-newline-log", lambda a: a.update(capture_log_path=str(output / "bad\npath"))),
                 ("summary-code-log", lambda a: a.update(capture_log_path=str(output / "$(payload).capture"))),
                 ("summary-zero-tail", lambda a: a.update(summary_tail_bytes=0))]
        for name, mutation in cases:
            bounded()
            value, counter = execute(name, "streams", "summary", mutate=mutation)
            assert value["status"] == "validation_error", (name, value)
            assert not counter.exists(), name + " must not execute"

    def fallbacks() -> None:
        collision = output / "existing.capture"
        original = b"do not replace\x00\xff"
        collision.write_bytes(original)
        targets = [("collision", collision, "already_exists"),
                   ("missing-parent", output / "absent" / "child.capture", "not_found")]
        for name, path, status in targets:
            bounded()
            value, counter = execute(name, "streams", "summary", mutate=lambda a: a.update(capture_log_path=str(path)))
            assert value["status"] == value["process_status"] == "completed"
            assert value["result_mode"] == "full" and value["capture_log_status"] == status
            assert value["capture_log"]["written_to_disk"] is False
            assert counter.read_bytes() == b"once\n"
            check_capture(value, ("中文🙂\r\nstdout\n".encode(), "错误 stderr\n".encode()))
        assert collision.read_bytes() == original

    def compare(action: str, expected_status: str, expected: tuple[bytes, bytes] | None, mutation=None) -> None:
        values = []
        for mode in ("full", "summary"):
            bounded()
            value, counter = execute(action + "-" + mode, action, mode, mutate=mutation)
            assert counter.read_bytes() == b"once\n"
            assert value["status"] == expected_status
            assert value["cleanup_complete"] is True and value["active_processes_after_cleanup"] == 0
            assert value["result_mode"] == mode
            check_capture(value, expected)
            values.append(value)
        for field in ("status", "process_status", "exit_code", "cleanup_complete", "output_encoding", "output_truncated"):
            assert values[0][field] == values[1][field], field

    def output_limit() -> None:
        compare("output", "output_limit", None, lambda a: a.update(output_limit_bytes=4096))
        for call in receipt["calls"][-2:]:
            value = call["tool_result"]
            assert value["output_truncated"] is True
            assert value["stdout_bytes"] + value["stderr_bytes"] == 4096
            assert log_streams(value) == (b"x" * 4096, b"")

    def cancel_cases() -> None:
        for mode in ("full", "summary"):
            bounded()
            value, counter = execute("cancel-" + mode, "sleep", mode, cancel=True)
            assert value["status"] == value["process_status"] == "cancelled", value
            assert value["cleanup_complete"] is True and value["active_processes_after_cleanup"] == 0
            assert value["result_mode"] == "full"
            assert value["capture_log_status"] == "cancelled"
            assert not value["capture_log"]["written_to_disk"]
            assert not counter.exists() or counter.read_bytes() == b"once\n"
            check_capture(value)

    def descendants() -> None:
        for mode in ("full", "summary"):
            bounded()
            value, counter = execute("tree-" + mode, "tree", mode)
            assert value["status"] == value["process_status"] == "completed"
            assert counter.read_bytes() == b"once\n"
            assert value["cleanup_complete"] and value["active_processes_after_cleanup"] == 0
            check_capture(value)
            stdout, _ = log_streams(value)
            assert child_stopped(json.loads(stdout)["child_pid"]), value

    def default_full() -> None:
        value, counter = execute("default-full", "streams")
        assert value["result_mode"] == "full" and value["capture_log_status"] == "not_requested"
        assert value["status"] == "completed" and counter.read_bytes() == b"once\n"
        check_capture(value, ("中文🙂\r\nstdout\n".encode(), "错误 stderr\n".encode()))

    try:
        client = StdioClient(executable, root, output / "stdio", deadline, receipt)
        client.initialize()
        check("full-summary Unicode bytes/hashes/offsets/tails", lambda: compare(
            "streams", "completed", ("中文🙂\r\nstdout\n".encode(), "错误 stderr\n".encode())))
        if not options.pilot:
            check("full remains the default without a log target", default_full)
            check("preflight rejects without child execution", invalid_cases)
            check("log failures preserve full result and execute side effect once", fallbacks)
            check("expected nonzero has identical successful semantics", lambda: compare(
                "exit23", "completed", (b"nonzero\n", b""), lambda a: a.update(expected_exit_codes=[23])))
            check("unexpected nonzero remains failed", lambda: compare("exit23", "failed", (b"nonzero\n", b"")))
            check("strict CP936 preserves raw bytes", lambda: compare("cp936", "completed",
                ("中文\r\n".encode("cp936"), "错误\n".encode("cp936")), lambda a: a.update(output_encoding="cp936")))
            check("encoding error keeps process result and raw capture", lambda: compare(
                "invalid", "encoding_error", (b"valid\xff\n", b"stderr\x00\n")))
            check("capture output limit differs from summary omission", output_limit)
            check("timeout keeps child cleanup and captured bytes", lambda: compare(
                "sleep", "timed_out", (b"started\n", b""), lambda a: a.update(timeout_ms=400)))
            check("cancel keeps full fallback without replay", cancel_cases)
            check("successful root cleans owned descendants", descendants)
    except BaseException as error:
        receipt["error"] = repr(error)
    finally:
        if client is not None:
            client.close()
        fixture.unlink(missing_ok=True)
        receipt["fixture_removed"] = not fixture.exists()
        receipt["elapsed_seconds"] = time.monotonic() - started
        receipt["cleanup_complete"] = all(p["cleanup_complete"] for p in receipt["processes"])
        receipt["status"] = "PASS" if (receipt["checks"] and all(c["status"] == "PASS" for c in receipt["checks"])
            and not receipt.get("error") and receipt["cleanup_complete"] and receipt["fixture_removed"]) else "FAIL"
        (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "checks": len(receipt["checks"]),
                      "calls": len(receipt["calls"]), "cleanup_complete": receipt["cleanup_complete"],
                      "receipt": str(output / "receipt.json")}, ensure_ascii=False), flush=True)
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
