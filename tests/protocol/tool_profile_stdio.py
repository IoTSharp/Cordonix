"""Bounded real-stdio regression for Cordonix tool advertisement profiles."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import signal
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "acceptance"))
from context_efficiency_baseline import StdioClient, sha256


LOCAL_CODE = frozenset((
    "create_directory", "create_file", "tool_info", "execute_program",
    "query_processes", "search_text", "find_files", "find_program", "git_status",
    "data_query", "hash_file", "copy_file", "move_file", "list_directory",
    "resolve_path", "stat", "verify_encoding", "read_file", "read_files",
    "workspace_open", "workspace_read", "workspace_edit", "workspace_convert_encoding",
    "workspace_commit", "workspace_status", "workspace_search", "workspace_close",
))


def cancelled(signum: int, _frame: object) -> None:
    raise InterruptedError(f"cancelled by signal {signum}")


def check(receipt: dict, name: str, passed: bool, detail: object = None) -> None:
    if len(receipt["checks"]) >= 12:
        raise ValueError("twelve-check bound exceeded")
    receipt["checks"].append({"name": name, "status": "PASS" if passed else "FAIL", "detail": detail})
    print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)


def tool_map(tools: object) -> dict:
    if not isinstance(tools, list) or len(tools) > 64:
        raise ValueError("tools/list must return a bounded list")
    result = {}
    for tool in tools:
        if not isinstance(tool, dict) or not isinstance(tool.get("name"), str) or tool["name"] in result:
            raise ValueError("tools/list contains invalid or duplicate names")
        result[tool["name"]] = tool
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="New receipt directory under this checkout's build/")
    parser.add_argument("--deadline-seconds", type=float, default=60)
    parser.add_argument("--pilot", action="store_true", help="Start only the default profile and check its real tools/list")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    if not output.is_relative_to((root / "build").resolve()) or output.exists():
        parser.error("--output must be a new directory under this checkout's build/")
    if not 25 <= args.deadline_seconds <= 60:
        parser.error("deadline-seconds must be 25..60, including a 20-second cleanup reserve")
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    deadline = started + args.deadline_seconds - 20
    receipt = {"status": "FAIL", "pilot": args.pilot, "platform": sys.platform,
               "executable": str(executable), "executable_sha256": sha256(executable.read_bytes()),
               "deadline_seconds": args.deadline_seconds, "max_checks": 12,
               "processes": [], "checks": [], "calls": [], "retries": 0}
    signal.signal(signal.SIGINT, cancelled)
    signal.signal(signal.SIGTERM, cancelled)
    try:
        lists = {}
        profiles = (None,) if args.pilot else (None, "full", "local-code")
        for profile in profiles:
            if time.monotonic() >= deadline:
                raise TimeoutError("profile acceptance operation deadline reached")
            label = profile or "default"
            client = StdioClient(executable, root, output / label, deadline, receipt, profile)
            try:
                client.initialize()
                response, record = client.tools_list(profile)
                receipt["calls"].append(record)
                tools = response.get("result", {}).get("tools")
                lists[label] = tools
                members = tool_map(tools)
                if profile is None:
                    check(receipt, "default advertises the 35-tool surface", len(members) == 35)
                if profile == "local-code":
                    check(receipt, "local-code has exactly the 27 specified members", set(members) == LOCAL_CODE,
                          {"missing": sorted(LOCAL_CODE - members.keys()), "extra": sorted(members.keys() - LOCAL_CODE)})
                    full = tool_map(lists["full"])
                    check(receipt, "every retained schema object equals full", all(tool == full.get(name) for name, tool in members.items()))
                    properties = members.get("execute_program", {}).get("inputSchema", {}).get("properties", {})
                    check(receipt, "execution projection fields are input properties", all(name in properties for name in (
                        "result_mode", "summary_tail_bytes", "capture_log_path")))
                    response, record = client.rpc("tools/call", {"name": "http", "arguments": {"url": 17}})
                    receipt["calls"].append(record)
                    result = response.get("result", {})
                    content = result.get("content", [])
                    data = json.loads(content[0]["text"]) if content else {}
                    check(receipt, "hidden http direct call retains tool argument validation", not response.get("error") and
                          data.get("status") == "validation_error" and result.get("isError") is True)
                    response, record = client.rpc("tools/call", {"name": "unknown_profile_test_tool", "arguments": {}})
                    receipt["calls"].append(record)
                    error = response.get("error", {})
                    check(receipt, "unknown tool retains JSON-RPC validation", error.get("code") == -32602 and
                          error.get("message") == "Unknown tool")
            finally:
                client.close()
        if not args.pilot:
            check(receipt, "default and explicit full schemas match exactly", lists["default"] == lists["full"])
            if time.monotonic() >= deadline:
                raise TimeoutError("profile acceptance operation deadline reached")
            client = StdioClient(executable, root, output / "unknown", deadline, receipt, "unknown-profile")
            try:
                client.process.wait(timeout=min(5, max(0.001, deadline - time.monotonic())))
            finally:
                client.close()
            check(receipt, "unknown profile exits with code 2", client.audit["exit_code"] == 2)
            check(receipt, "unknown profile has an explicit error and no MCP output",
                  b"Unknown tool profile" in client.stderr and not list((output / "unknown").glob("response-*.json")),
                  client.stderr.decode("utf-8", errors="strict"))
        check(receipt, "all owned stdio processes and readers are closed", all(
            item.get("cleanup_complete") for item in receipt["processes"]))
        check(receipt, "whole acceptance stays within its deadline", time.monotonic() - started <= args.deadline_seconds)
        receipt["status"] = "PASS" if all(item["status"] == "PASS" for item in receipt["checks"]) else "FAIL"
    except BaseException as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        print(receipt["error"], file=sys.stderr, flush=True)
    finally:
        receipt["elapsed_seconds"] = time.monotonic() - started
        (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "checks": len(receipt["checks"]),
                      "receipt": str(output / "receipt.json")}), flush=True)
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
