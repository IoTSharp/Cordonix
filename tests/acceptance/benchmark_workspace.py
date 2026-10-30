"""Bounded Windows MCP baseline: fresh disk reads versus an open document."""

import argparse
import ctypes
from ctypes import wintypes
import hashlib
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


FILE_BYTES = 131072
MAX_FRAME = 2 * 1024 * 1024
MAX_RESPONSES = 1024


class IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in (
        "read_operations", "write_operations", "other_operations",
        "read_bytes", "write_bytes", "other_bytes")]


class JobLimits(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_longlong), ("job_time", ctypes.c_longlong),
                ("flags", wintypes.DWORD), ("min_working_set", ctypes.c_size_t),
                ("max_working_set", ctypes.c_size_t), ("active_process_limit", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                ("scheduling_class", wintypes.DWORD)]


class ExtendedJobLimits(ctypes.Structure):
    _fields_ = [("basic", JobLimits), ("io", IoCounters),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process_memory", ctypes.c_size_t), ("peak_job_memory", ctypes.c_size_t)]


class ProcessAccounting:
    def __init__(self, process):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.kernel.GetProcessIoCounters.argtypes = [wintypes.HANDLE, ctypes.POINTER(IoCounters)]
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = wintypes.HANDLE(int(process._handle))
        self.job = self.kernel.CreateJobObjectW(None, None)
        if not self.job:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedJobLimits()
        limits.basic.flags = 0x2000  # Kill only this benchmark's job when its handle closes.
        if not self.kernel.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or not self.kernel.AssignProcessToJobObject(self.job, self.handle):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)

    def snapshot(self):
        values = [wintypes.FILETIME() for _ in range(4)]
        if not self.kernel.GetProcessTimes(self.handle, *(ctypes.byref(value) for value in values)):
            raise ctypes.WinError(ctypes.get_last_error())
        numbers = [(value.dwHighDateTime << 32) | value.dwLowDateTime for value in values]
        counters = IoCounters()
        if not self.kernel.GetProcessIoCounters(self.handle, ctypes.byref(counters)):
            raise ctypes.WinError(ctypes.get_last_error())
        return {"creation_filetime": numbers[0], "kernel_cpu_100ns": numbers[2],
                "user_cpu_100ns": numbers[3],
                "os_io": {name: getattr(counters, name) for name, _ in counters._fields_}}

    def close(self):
        if self.job:
            self.kernel.CloseHandle(self.job)
            self.job = None


class Mcp:
    def __init__(self, executable, cwd, deadline, audits):
        self.deadline = deadline
        self.responses = queue.Queue(maxsize=4)
        self.stderr = bytearray()
        self.id = 0
        self.process = subprocess.Popen([str(executable), "--mcp"], cwd=str(cwd),
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        self.accounting = None
        self.readers = []
        self.audit = {"pid": self.process.pid, "parent_pid": os.getpid(),
                      "program": str(executable), "args": ["--mcp"], "cwd": str(cwd),
                      "cleanup_complete": False}
        audits.append(self.audit)
        try:
            self.accounting = ProcessAccounting(self.process)
            self.audit["creation_filetime"] = self.accounting.snapshot()["creation_filetime"]
            self.readers = [threading.Thread(target=self.read_stdout, daemon=True),
                            threading.Thread(target=self.read_stderr, daemon=True)]
            for reader in self.readers:
                reader.start()
            self.rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                    "clientInfo": {"name": "cdx-fixed-workspace-baseline", "version": "1"}})
            self.notify("notifications/initialized", {})
        except BaseException:
            self.close()
            raise

    def read_stdout(self):
        try:
            for _ in range(MAX_RESPONSES):
                if time.monotonic() >= self.deadline:
                    raise TimeoutError("response reader deadline")
                frame = self.process.stdout.readline(MAX_FRAME + 1)
                if not frame:
                    break
                if len(frame) > MAX_FRAME or not frame.endswith(b"\n"):
                    raise ValueError("MCP frame exceeded bound")
                self.responses.put(json.loads(frame), timeout=2)
        except BaseException as error:
            try:
                self.responses.put(error, timeout=1)
            except queue.Full:
                pass

    def read_stderr(self):
        for _ in range(16):
            if time.monotonic() >= self.deadline:
                return
            chunk = self.process.stderr.read(4096)
            if not chunk:
                return
            self.stderr.extend(chunk)

    def notify(self, method, params):
        self.process.stdin.write((json.dumps({"jsonrpc": "2.0", "method": method, "params": params}) + "\n").encode())
        self.process.stdin.flush()

    def rpc(self, method, params):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("benchmark deadline")
        self.id += 1
        message = {"jsonrpc": "2.0", "id": self.id, "method": method, "params": params}
        self.process.stdin.write((json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8"))
        self.process.stdin.flush()
        response = self.responses.get(timeout=min(10, remaining))
        if isinstance(response, BaseException):
            raise response
        if response.get("id") != self.id or "error" in response:
            raise ValueError(f"Unexpected MCP response: {response}")
        return response["result"]

    def tool(self, name, arguments):
        result = self.rpc("tools/call", {"name": name, "arguments": arguments})
        value = json.loads(result["content"][0]["text"])
        if result.get("isError") or value.get("status") != "ok":
            raise ValueError(f"{name}: {value}")
        return value

    def close(self):
        try:
            if self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                if self.accounting:
                    self.accounting.kernel.TerminateJobObject(self.accounting.job, 1)
                else:
                    self.process.kill()
                self.process.wait(timeout=3)
        finally:
            if self.accounting:
                self.accounting.close()
            for reader in self.readers:
                reader.join(timeout=2)
            self.audit["cleanup_complete"] = self.process.poll() is not None and all(not reader.is_alive() for reader in self.readers)
            self.audit["exit_code"] = self.process.poll()
            for pipe in (self.process.stdout, self.process.stderr):
                if pipe:
                    pipe.close()


def delta(before, after):
    return {"kernel_cpu_100ns": after["kernel_cpu_100ns"] - before["kernel_cpu_100ns"],
            "user_cpu_100ns": after["user_cpu_100ns"] - before["user_cpu_100ns"],
            "os_io": {key: after["os_io"][key] - before["os_io"][key] for key in before["os_io"]}}


def measure(executable, fixture, root, mode, calls, text_sha, deadline, audits):
    client = Mcp(executable, root, deadline, audits)
    try:
        baseline = client.tool("tool_info", {})
        document = None
        opening_seconds = None
        if mode == "memory":
            opening_started = time.perf_counter()
            document = client.tool("workspace_open", {"path": str(fixture), "encoding": "utf-8"})["document_id"]
            opening_seconds = time.perf_counter() - opening_started
        ready_info = client.tool("tool_info", {})
        before = client.accounting.snapshot()
        started = time.perf_counter()
        sample_seconds = []
        for _ in range(calls):
            if time.monotonic() >= deadline:
                raise TimeoutError("benchmark operation deadline")
            call_started = time.perf_counter()
            if mode == "memory":
                value = client.tool("workspace_read", {"document_id": document, "raw": True, "max_bytes": 262144})
            else:
                value = client.tool("read_file", {"path": str(fixture), "encoding": "utf-8", "raw": True, "max_bytes": 262144})
            sample_seconds.append(time.perf_counter() - call_started)
            if hashlib.sha256(value["text"].encode("utf-8")).hexdigest() != text_sha:
                raise AssertionError("Fresh/memory text differed from the fixed fixture")
        wall_seconds = time.perf_counter() - started
        after = client.accounting.snapshot()
        final_info = client.tool("tool_info", {})
        if document is not None:
            client.tool("workspace_close", {"document_id": document})
        return {"mode": mode, "calls": calls, "wall_seconds": wall_seconds,
                "open_wall_seconds": opening_seconds, "sample_seconds": sample_seconds,
                "server_counters": delta(before, after),
                "tool_info_before_open": baseline, "tool_info_ready": ready_info,
                "tool_info_after_reads": final_info, "text_sha256": text_sha,
                "process": client.audit}
    finally:
        client.close()


def cancel(*_):
    raise KeyboardInterrupt()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--pilot", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This baseline records Windows process accounting and requires Windows")
    executable = args.executable.resolve(strict=True)
    root = Path(__file__).resolve().parents[2]
    build_root = (root / "build").resolve(strict=True)
    output = args.output.resolve()
    if not output.is_relative_to(build_root) or output.exists():
        parser.error("Use a new receipt path below this checkout's build directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + 180
    signal.signal(signal.SIGTERM, cancel)
    payload_line = "Cordonix 固定基线 literal $(value) abcdefghijklmnopqrstuvwxyz\r\n".encode("utf-8")
    payload = payload_line * (FILE_BYTES // len(payload_line)) + b"x" * (FILE_BYTES % len(payload_line))
    text_sha = hashlib.sha256(payload).hexdigest()
    fixture_root = Path(tempfile.mkdtemp(prefix="fixture-", dir=output.parent)).resolve()
    fixture = fixture_root / "baseline-utf8.txt"
    receipt = {"status": "FAIL", "schema": 1, "platform": sys.platform,
               "python": sys.version, "executable": str(executable),
               "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
               "fixture": {"bytes": FILE_BYTES, "encoding": "utf-8", "text_sha256": text_sha,
                           "generator": "UTF-8 fixed line repeated to 131072 bytes with ASCII x padding"},
               "calls_per_group": 2 if args.pilot else 100, "repetitions": 1 if args.pilot else 3,
               "budget_seconds": 180, "groups": [], "processes": [], "fixture_cleaned": False,
               "physical_disk_io": "NOT_MEASURED", "model_tokens": "NOT_MEASURED",
               "notes": ["OS process I/O includes file and pipe operations; it is not physical disk I/O.",
                         "End-to-end group wall time includes MCP transport, client JSON parsing and per-response SHA checking.",
                         "OS cache is not flushed. Repetition order alternates fresh/memory then memory/fresh.",
                         "Opening the document is measured separately; the memory read loop begins after opening.",
                         "No tokenizer or model sampling is run. No speedup or saving percentage is claimed."]}
    started = time.perf_counter()
    try:
        fixture.write_bytes(payload)
        for repeat in range(receipt["repetitions"]):
            order = ("fresh", "memory") if repeat % 2 == 0 else ("memory", "fresh")
            for mode in order:
                result = measure(executable, fixture, fixture_root, mode, receipt["calls_per_group"], text_sha, deadline, receipt["processes"])
                result["repetition"] = repeat + 1
                receipt["groups"].append(result)
                print(json.dumps({"repetition": repeat + 1, "mode": mode, "calls": result["calls"],
                                  "wall_seconds": result["wall_seconds"], "cleanup": result["process"]["cleanup_complete"]}), flush=True)
                if not result["process"]["cleanup_complete"]:
                    raise RuntimeError("Owned MCP cleanup incomplete")
        receipt["executable_sha256_after"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        if receipt["executable_sha256_after"] != receipt["executable_sha256"]:
            raise AssertionError("Executable changed during baseline")
        if hashlib.sha256(fixture.read_bytes()).hexdigest() != text_sha:
            raise AssertionError("Fixture changed during reads")
        receipt["status"] = "PASS"
    except BaseException as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
    finally:
        receipt["elapsed_seconds"] = time.perf_counter() - started
        try:
            if fixture_root.parent == output.parent and fixture_root.name.startswith("fixture-"):
                shutil.rmtree(fixture_root)
                receipt["fixture_cleaned"] = not fixture_root.exists()
        except OSError as error:
            receipt["status"] = "FAIL"
            receipt["cleanup_error"] = f"{type(error).__name__}: {error}"
        output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "receipt": str(output), "fixture_cleaned": receipt["fixture_cleaned"]}), flush=True)
    return 0 if receipt["status"] == "PASS" and receipt["fixture_cleaned"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
