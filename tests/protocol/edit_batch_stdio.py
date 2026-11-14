"""Bounded real-stdio acceptance for transactional single-document edit batches."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import signal
import sys
import time

sys.dont_write_bytecode = True
sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "acceptance"))
from context_efficiency_baseline import StdioClient


MAX_CALLS = 128
MAX_CHECKS = 100


def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def schema_descriptions(schema: dict) -> list[str]:
    """Check the public nested property descriptions with explicit work bounds."""
    pending = [(schema, "$", 0)]
    missing = []
    deadline = time.monotonic() + 1
    for _ in range(256):
        if time.monotonic() >= deadline:
            raise TimeoutError("schema inspection deadline reached")
        if not pending:
            return missing
        node, path, depth = pending.pop()
        if depth > 12:
            raise ValueError("schema nesting exceeds twelve levels")
        properties = node.get("properties", {})
        if len(properties) > 64:
            raise ValueError("schema property bound exceeded")
        for name, value in properties.items():
            child = path + "." + name
            if not isinstance(value.get("description"), str) or not value["description"].strip():
                missing.append(child)
            pending.append((value, child, depth + 1))
        item = node.get("items")
        if isinstance(item, dict):
            pending.append((item, path + "[]", depth + 1))
    raise ValueError("schema node bound exceeded")


def check_diff(result: dict, before: str, after: str, *, truncated: bool = False) -> None:
    diff = result["diff"]
    old, new = before.encode("utf-8"), after.encode("utf-8")
    prefix, removed, added = (diff[name] for name in ("prefix_bytes", "removed_bytes", "added_bytes"))
    old_preview, new_preview = diff["before"].encode("utf-8"), diff["after"].encode("utf-8")
    assert isinstance(prefix, int) and isinstance(removed, int) and isinstance(added, int)
    assert prefix >= 0 and removed >= 0 and added >= 0
    assert prefix + removed <= len(old) and prefix + added <= len(new)
    assert old[:prefix] == new[:prefix]
    assert old[prefix + removed:] == new[prefix + added:]
    assert len(old_preview) <= 8192 and len(new_preview) <= 8192
    assert old[prefix:prefix + removed].startswith(old_preview)
    assert new[prefix:prefix + added].startswith(new_preview)
    assert diff["truncated"] is truncated
    if not truncated:
        assert old_preview == old[prefix:prefix + removed]
        assert new_preview == new[prefix:prefix + added]
        assert old[:prefix] + new_preview + old[prefix + removed:] == new


def measure_batches(options: argparse.Namespace, root: Path, executable: Path, output: Path) -> int:
    """Compare exact edit RPC traffic for the same fixed six-replacement task."""
    output.mkdir(parents=True)
    started = time.monotonic()
    deadline = started + options.deadline_seconds - 20
    receipt = {"status": "FAIL", "platform": sys.platform, "mode": "fixed-six-edit-measurement",
               "executable": str(executable), "executable_sha256": digest(executable.read_bytes()),
               "max_rpc": 20, "max_edits": 6, "deadline_seconds": options.deadline_seconds,
               "processes": [], "calls": [], "checks": [], "retries": 0,
               "tokens": "NOT_MEASURED", "provider_usage": "NOT_MEASURED"}
    client = None
    fixtures = []

    def stop(signum: int, _frame: object) -> None:
        raise InterruptedError(f"cancelled by signal {signum}")

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    def call(name: str, arguments: dict, mode: str) -> dict:
        if time.monotonic() >= deadline or sum(item["rpc_calls"] for item in receipt["processes"]) >= 20:
            raise TimeoutError("measurement RPC/deadline bound reached")
        response, record = client.rpc("tools/call", {"name": name, "arguments": arguments})
        receipt["calls"].append(dict(record, tool=name, mode=mode))
        assert not record.get("transport_error") and not response.get("error"), record
        value = json.loads(response["result"]["content"][0]["text"])
        assert value["status"] == "ok", value
        return value

    try:
        client = StdioClient(executable, root, output / "rpc", deadline, receipt)
        client.initialize()
        original = b"feature=name0\nmode=steady\n"
        final = b"feature=name6\nmode=steady\n"
        edits = [{"old": f"name{index}", "new": f"name{index + 1}"} for index in range(6)]
        for mode in ("legacy", "batch"):
            path = output / (mode + ".txt")
            path.write_bytes(original)
            fixtures.append(path)
            doc = call("workspace_open", {"path": str(path), "encoding": "utf-8"}, mode)
            if mode == "legacy":
                for edit in edits:
                    result = call("workspace_edit", {"document_id": doc["document_id"],
                                  "expected_sha256": doc["memory_sha256"], "dry_run": False, **edit}, mode)
                    doc["memory_sha256"] = result["memory_sha256"]
            else:
                call("workspace_edit", {"document_id": doc["document_id"],
                     "expected_sha256": doc["memory_sha256"], "dry_run": False, "edits": edits}, mode)
            result = call("workspace_read", {"document_id": doc["document_id"], "raw": True}, mode)
            assert result["text"].encode() == final and result["memory_sha256"] == digest(final), result
            assert path.read_bytes() == original
            receipt.setdefault("finals", {})[mode] = {"text_sha256": digest(result["text"].encode()),
                                                       "memory_sha256": result["memory_sha256"],
                                                       "disk_unchanged": True}
            call("workspace_close", {"document_id": doc["document_id"], "discard": True}, mode)
        for mode, expected_calls in (("legacy", 6), ("batch", 1)):
            records = [item for item in receipt["calls"] if item["tool"] == "workspace_edit" and item["mode"] == mode]
            assert len(records) == expected_calls
            requests = sum(item["request_utf8_bytes"] for item in records)
            responses = sum(item["mcp_response_utf8_bytes"] for item in records)
            receipt.setdefault("edit_rpc", {})[mode] = {"calls": len(records), "request_utf8_bytes": requests,
                                                       "response_utf8_bytes": responses,
                                                       "request_response_utf8_bytes": requests + responses}
        assert receipt["finals"]["legacy"] == receipt["finals"]["batch"]
        client.close()
        client = None
        assert all(item["cleanup_complete"] for item in receipt["processes"])
        assert time.monotonic() - started <= options.deadline_seconds
        receipt["status"] = "PASS"
    except BaseException as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        print(receipt["error"], file=sys.stderr, flush=True)
    finally:
        if client is not None:
            client.close()
        for path in fixtures:
            if path.parent.resolve() != output:
                raise RuntimeError("measurement cleanup escaped its owned directory")
            path.unlink(missing_ok=True)
        receipt["total_rpc"] = sum(item["rpc_calls"] for item in receipt["processes"])
        receipt["fixtures_removed"] = all(not path.exists() for path in fixtures)
        receipt["elapsed_seconds"] = time.monotonic() - started
        (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "edit_rpc": receipt.get("edit_rpc"),
                      "receipt": str(output / "receipt.json")}), flush=True)
    return 0 if receipt["status"] == "PASS" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="New receipt directory under build/")
    parser.add_argument("--deadline-seconds", type=float, default=120)
    parser.add_argument("--pilot", action="store_true", help="Run only schema and legacy compatibility")
    parser.add_argument("--measure-only", action="store_true", help="Measure six equivalent legacy edits versus one batch")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    executable = options.executable.resolve(strict=True)
    output = options.output.resolve()
    if output.exists() or not output.is_relative_to((root / "build").resolve()):
        parser.error("output must be a new directory under this checkout's build/")
    if not 30 <= options.deadline_seconds <= 120:
        parser.error("deadline-seconds must be 30..120, including a twenty-second cleanup reserve")
    if options.measure_only:
        if options.pilot:
            parser.error("--measure-only and --pilot are separate modes")
        return measure_batches(options, root, executable, output)
    output.mkdir(parents=True)
    started = time.monotonic()
    deadline = started + options.deadline_seconds - 20
    receipt = {"status": "FAIL", "platform": sys.platform, "pilot": options.pilot,
               "executable": str(executable), "executable_sha256": digest(executable.read_bytes()),
               "deadline_seconds": options.deadline_seconds, "max_calls": MAX_CALLS,
               "max_checks": MAX_CHECKS, "checks": [], "calls": [], "processes": [], "retries": 0}
    fixtures: list[Path] = []
    client = None

    def stop(signum: int, _frame: object) -> None:
        raise InterruptedError(f"cancelled by signal {signum}")

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    def bounded() -> None:
        total_rpc = sum(item["rpc_calls"] for item in receipt["processes"])
        if time.monotonic() >= deadline or total_rpc >= MAX_CALLS:
            raise TimeoutError("batch acceptance call-count/deadline reached")

    def check(name: str, passed: bool, detail: object = None) -> None:
        if len(receipt["checks"]) >= MAX_CHECKS:
            raise ValueError("batch acceptance check-count bound exceeded")
        receipt["checks"].append({"name": name, "status": "PASS" if passed else "FAIL", "detail": detail})
        print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)
        if not passed:
            raise AssertionError(name)

    def call(name: str, arguments: dict, status: str | tuple[str, ...] = "ok", *, cancel: bool = False) -> dict:
        bounded()
        response, record = client.rpc("tools/call", {"name": name, "arguments": arguments}, cancel=cancel)
        receipt["calls"].append(dict(record, tool=name))
        assert not record.get("transport_error"), record
        assert not response.get("error"), response
        envelope = response["result"]
        value = json.loads(envelope["content"][0]["text"])
        expected = (status,) if isinstance(status, str) else status
        assert value.get("status") in expected, value
        if value["status"] not in ("ok", "preview"):
            assert envelope.get("isError") is True, envelope
        return value

    def fixture(name: str, raw: bytes, encoding: str) -> tuple[Path, dict]:
        if len(fixtures) >= 12:
            raise ValueError("fixture bound exceeded")
        path = output / name
        path.write_bytes(raw)
        fixtures.append(path)
        opened = call("workspace_open", {"path": str(path), "encoding": encoding})
        assert opened["memory_sha256"] == digest(raw), opened
        return path, opened

    def read(document: dict) -> dict:
        return call("workspace_read", {"document_id": document["document_id"], "raw": True, "max_bytes": 262144})

    def edit(document: dict, edits: list, *, status: str | tuple[str, ...] = "ok", cancel: bool = False, **extra) -> dict:
        return call("workspace_edit", {"document_id": document["document_id"],
                    "expected_sha256": document["memory_sha256"], "edits": edits, **extra}, status, cancel=cancel)

    def close(document: dict) -> None:
        call("workspace_close", {"document_id": document["document_id"], "discard": True})

    try:
        client = StdioClient(executable, root, output / "core", deadline, receipt)
        bounded()
        client.initialize()
        bounded()
        response, record = client.tools_list(None)
        receipt["calls"].append(record)
        tools = response["result"]["tools"]
        schema = next(tool["inputSchema"] for tool in tools if tool["name"] == "workspace_edit")
        props = schema["properties"]
        check("35-tool surface retained and batch properties described", len(tools) == 35 and
              not schema_descriptions(schema) and "edits" in props and "timeout_ms" in props,
              schema_descriptions(schema))
        check("batch cardinality and safe preview default advertised", props["edits"]["minItems"] == 1 and
              props["edits"]["maxItems"] == 32 and props["dry_run"]["default"] is True and
              set(props["edits"]["items"]["properties"]) >= {"old", "new", "expected_count"})
        original = b"alpha=1\r\nbeta=2\r\n"
        path, doc = fixture("legacy 中文.txt", original, "utf-8")
        legacy = call("workspace_edit", {"document_id": doc["document_id"], "expected_sha256": doc["memory_sha256"],
                      "old": "beta", "new": "legacy", "dry_run": False})
        current = read(doc)
        check("legacy single-edit behavior retained without writing disk", legacy["matches"] == 1 and
              current["text"] == "alpha=1\r\nlegacy=2\r\n" and path.read_bytes() == original)
        doc.update(current)
        if not options.pilot:
            sequence = [{"old": "alpha", "new": "gamma"}, {"old": "gamma", "new": "delta"}]
            expected = "delta=1\r\nlegacy=2\r\n"
            preview = edit(doc, sequence, status="preview")
            check_diff(preview, current["text"], expected)
            after_preview = read(doc)
            check("default batch preview preserves memory SHA dirty state and disk", preview["written_to_disk"] is False and
                  preview["memory_sha256"] == doc["memory_sha256"] and preview["dirty_in_memory"] is True and
                  preview["preview_sha256"] == digest(expected.encode()) and
                  after_preview["text"] == current["text"] and after_preview["memory_sha256"] == doc["memory_sha256"] and
                  path.read_bytes() == original)
            applied = edit(doc, sequence, dry_run=False)
            check_diff(applied, current["text"], expected)
            current = read(doc)
            check("sequential batch observes earlier replacements and reports per-edit counts", current["text"] == expected and
                  applied["edit_count"] == applied["completed_edits"] == 2 and applied["failed_edit_index"] is None and
                  [item["actual_count"] for item in applied["edits"]] == [1, 1] and
                  [item["expected_count"] for item in applied["edits"]] == [1, 1] and
                  applied["memory_sha256"] == applied["preview_sha256"] == digest(expected.encode()) and path.read_bytes() == original)
            doc.update(current)
            stale = edit(dict(doc, memory_sha256="0" * 64), sequence, status="file_changed", dry_run=False)
            check("stale initial SHA rejects the entire batch", read(doc)["memory_sha256"] == doc["memory_sha256"] and
                  path.read_bytes() == original, stale.get("status"))
            failed = edit(doc, [{"old": "delta", "new": "first"}, {"old": "missing", "new": "never"},
                               {"old": "legacy", "new": "not-run"}],
                          dry_run=False, status="anchor_mismatch")
            unchanged = read(doc)
            check("second-edit anchor failure rolls back an already-dirty document", failed["failed_edit_index"] == 1 and
                  failed["completed_edits"] == 1 and failed["edits"][2]["status"] == "not_run" and
                  unchanged["text"] == expected and unchanged["memory_sha256"] == doc["memory_sha256"] and
                  unchanged["dirty_in_memory"] is True and path.read_bytes() == original)
            failed = edit(doc, [{"old": "delta", "new": "first"}, {"old": "legacy", "new": "second"},
                               {"old": "=", "new": ":"}], dry_run=False, status="ambiguous_match")
            check("last-edit ambiguous count rolls back all preceding edits", failed["failed_edit_index"] == 2 and
                  read(doc)["text"] == expected and path.read_bytes() == original)
            failed = edit(doc, [{"old": "delta", "new": "first"}, {"old": "=", "new": ":", "expected_count": 3}],
                          dry_run=False, status="anchor_mismatch")
            check("explicit occurrence undercount rolls back batch", failed["failed_edit_index"] == 1 and
                  read(doc)["memory_sha256"] == doc["memory_sha256"])
            no_op = edit(doc, [{"old": "delta", "new": "delta"}], dry_run=False)
            check_diff(no_op, expected, expected)
            check("no-op batch preserves the current memory SHA", no_op["memory_sha256"] == doc["memory_sha256"])
            committed = call("workspace_commit", {"document_id": doc["document_id"]})
            check("explicit commit preserves exact CRLF bytes", committed["written_to_disk"] is True and
                  committed["cleanup_complete"] is True and path.read_bytes() == expected.encode() and
                  committed["dirty_in_memory"] is False)
            clean = read(doc)
            doc.update(clean)
            edit(doc, [{"old": "delta", "new": "pending"}], dry_run=False)
            external = b"external writer\r\n"
            path.write_bytes(external)
            conflict = call("workspace_commit", {"document_id": doc["document_id"]}, "file_changed")
            check("commit conflict preserves external disk and dirty batch memory", conflict["written_to_disk"] is False and
                  conflict["dirty_in_memory"] is True and path.read_bytes() == external and
                  read(doc)["text"] == "pending=1\r\nlegacy=2\r\n")
        close(doc)
        client.close()
        client = None

        if not options.pilot:
            bounded()
            client = StdioClient(executable, root, output / "validation", deadline, receipt, "local-code")
            bounded()
            client.initialize()
            bounded()
            response, record = client.tools_list("local-code")
            receipt["calls"].append(record)
            check("local-code remains 27 tools with identical workspace_edit schema", len(response["result"]["tools"]) == 27 and
                  next(tool["inputSchema"] for tool in response["result"]["tools"] if tool["name"] == "workspace_edit") == schema)
            path, doc = fixture("validation.txt", b"old old\n", "utf-8")
            base = {"document_id": doc["document_id"], "expected_sha256": doc["memory_sha256"], "dry_run": False}
            invalid = [
                ("empty edits", {"edits": []}),
                ("33 edits", {"edits": [{"old": "old", "new": "old"}] * 33}),
                ("edits wrong type", {"edits": "old"}),
                ("null edits", {"edits": None}),
                ("edit wrong type", {"edits": [1]}),
                ("invalid later edit", {"edits": [{"old": "old", "new": "new", "expected_count": 2}, None]}),
                ("empty anchor", {"edits": [{"old": "", "new": "new"}]}),
                ("missing replacement", {"edits": [{"old": "old"}]}),
                ("nonstring replacement", {"edits": [{"old": "old", "new": 3}]}),
                ("zero count", {"edits": [{"old": "old", "new": "new", "expected_count": 0}]}),
                ("fractional count", {"edits": [{"old": "old", "new": "new", "expected_count": 1.5}]}),
                ("boolean count", {"edits": [{"old": "old", "new": "new", "expected_count": True}]}),
                ("mixed legacy fields", {"edits": [{"old": "old", "new": "new"}], "old": "old", "new": "new"}),
                ("mixed top-level count", {"edits": [{"old": "old", "new": "new"}], "expected_count": 2}),
                ("timeout zero", {"edits": [{"old": "old", "new": "new"}], "timeout_ms": 0}),
                ("timeout too high", {"edits": [{"old": "old", "new": "new"}], "timeout_ms": 60001}),
            ]
            for label, arguments in invalid:
                bounded()
                call("workspace_edit", dict(base, **arguments), "validation_error")
                check("rejects " + label, path.read_bytes() == b"old old\n")
            unchanged = read(doc)
            check("validation failures preserve clean memory and SHA", unchanged["text"] == "old old\n" and
                  unchanged["memory_sha256"] == doc["memory_sha256"] and unchanged["dirty_in_memory"] is False)
            maximum = edit(doc, [{"old": "old", "new": "old", "expected_count": 2}] * 32, dry_run=False)
            check("32-edit upper bound is accepted without dirtying a no-op document", maximum["edit_count"] == 32 and
                  maximum["completed_edits"] == 32 and len(maximum["edits"]) == 32 and maximum["dirty_in_memory"] is False and
                  maximum["memory_sha256"] == doc["memory_sha256"])
            edits = [{"old": "old", "new": "new", "expected_count": 2}, {"old": "new", "new": "", "expected_count": 2}]
            applied = edit(doc, edits, dry_run=False, timeout_ms=60000)
            check("explicit counts and empty replacement are accepted", read(doc)["text"] == " \n" and
                  [item["actual_count"] for item in applied["edits"]] == [2, 2])
            close(doc)
            client.close()
            client = None

            bounded()
            client = StdioClient(executable, root, output / "encoding", deadline, receipt)
            bounded()
            client.initialize()
            bom_raw = b"\xef\xbb\xbf" + "汉一\r\nsecond\r\n".encode()
            path, doc = fixture("utf8-bom.txt", bom_raw, "utf-8-bom")
            applied = edit(doc, [{"old": "一", "new": "二"}, {"old": "second", "new": "other"}], dry_run=False)
            expected = "汉二\r\nother\r\n"
            check_diff(applied, "汉一\r\nsecond\r\n", expected)
            check("Unicode diff byte boundaries and BOM accounting are exact", applied["diff"]["prefix_bytes"] == 3 and
                  applied["bytes_before"] == len(bom_raw) and applied["bytes_after"] == len(b"\xef\xbb\xbf" + expected.encode()))
            call("workspace_commit", {"document_id": doc["document_id"]})
            check("batch commit preserves UTF-8 BOM and CRLF", path.read_bytes() == b"\xef\xbb\xbf" + expected.encode())
            close(doc)

            cp_raw = "中文=一\r\n中文=二\r\n".encode("cp936")
            path, doc = fixture("cp936.txt", cp_raw, "cp936")
            failed = edit(doc, [{"old": "一", "new": "三"}, {"old": "二", "new": "🙂"}],
                          dry_run=False, status="encoding_error")
            check("unrepresentable CP936 second edit rolls back without loss", failed["failed_edit_index"] == 1 and
                  read(doc)["text"] == "中文=一\r\n中文=二\r\n" and path.read_bytes() == cp_raw)
            applied = edit(doc, [{"old": "一", "new": "三"}, {"old": "二", "new": "四"}], dry_run=False)
            call("workspace_commit", {"document_id": doc["document_id"]})
            check("CP936 batch commit has exact intended bytes", path.read_bytes() == "中文=三\r\n中文=四\r\n".encode("cp936") and
                  applied["written_to_disk"] is False and
                  applied["preview_sha256"] == digest("中文=三\r\n中文=四\r\n".encode("cp936")))
            close(doc)

            for label, raw in (("mixed", b"one\r\ntwo\n"), ("cr", b"one\rtwo\r")):
                bounded()
                path, doc = fixture(label + ".txt", raw, "utf-8")
                edit(doc, [{"old": "one", "new": "changed"}], dry_run=False, status="mixed_eol")
                check(label + " EOL rejection preserves original bytes and memory", path.read_bytes() == raw and
                      read(doc)["memory_sha256"] == doc["memory_sha256"])
                close(doc)

            path, doc = fixture("eol-change.txt", b"one\r\ntwo\r\n", "utf-8")
            edit(doc, [{"old": "one", "new": "changed"}, {"old": "two\r\n", "new": "two\n"}],
                 dry_run=False, status="mixed_eol")
            check("later mixed-EOL edit rolls back earlier valid replacement", read(doc)["text"] == "one\r\ntwo\r\n" and
                  path.read_bytes() == b"one\r\ntwo\r\n")
            close(doc)

            path, doc = fixture("cr-output.txt", b"one\n", "utf-8")
            edit(doc, [{"old": "one\n", "new": "one\r"}], dry_run=False, status="mixed_eol")
            check("CR-only candidate is rejected without changing clean text or SHA", read(doc)["text"] == "one\n" and
                  read(doc)["memory_sha256"] == doc["memory_sha256"] and path.read_bytes() == b"one\n")
            close(doc)

            large_before, large_after = "前" + "甲" * 4000 + "后", "前" + "乙" * 4000 + "后"
            path, doc = fixture("large-preview.txt", large_before.encode(), "utf-8")
            preview = edit(doc, [{"old": "甲" * 4000, "new": "乙" * 4000}], status="preview")
            check_diff(preview, large_before, large_after, truncated=True)
            repeated = edit(doc, [{"old": "甲" * 4000, "new": "乙" * 4000}], status="preview")
            check("large Unicode preview is deterministic bounded and explicitly truncated", preview["diff"] == repeated["diff"] and
                  preview["diff"]["removed_bytes"] == preview["diff"]["added_bytes"] == 12000 and
                  path.read_bytes() == large_before.encode() and read(doc)["memory_sha256"] == doc["memory_sha256"])
            close(doc)

            slow_raw = (b"marker00 " + b"x" * 249999 + b"\n") * 24
            path, doc = fixture("interruption.txt", slow_raw, "utf-8")
            slow_edits = [{"old": f"marker{index:02d}", "new": f"marker{index + 1:02d}", "expected_count": 24}
                          for index in range(32)]
            timed = edit(doc, slow_edits, dry_run=False, timeout_ms=1, status="timed_out")
            state = call("workspace_status", {"document_id": doc["document_id"]})
            check("real batch deadline returns timed_out without applying candidates", timed["written_to_disk"] is False and
                  state["memory_sha256"] == doc["memory_sha256"] and state["dirty_in_memory"] is False and
                  path.read_bytes() == slow_raw)
            cancelled = edit(doc, slow_edits, dry_run=False, timeout_ms=60000, cancel=True, status="cancelled")
            state = call("workspace_status", {"document_id": doc["document_id"]})
            check("real MCP cancellation preserves memory SHA and disk", cancelled["written_to_disk"] is False and
                  state["memory_sha256"] == doc["memory_sha256"] and state["dirty_in_memory"] is False and
                  path.read_bytes() == slow_raw)
            close(doc)
            client.close()
            client = None

        check("all owned MCP processes and readers are closed", all(item.get("cleanup_complete") for item in receipt["processes"]))
        check("acceptance stayed within wall-clock limit without retries", time.monotonic() - started <= options.deadline_seconds and receipt["retries"] == 0)
        receipt["status"] = "PASS"
    except BaseException as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
        print(receipt["error"], file=sys.stderr, flush=True)
    finally:
        if client is not None:
            client.close()
        for path in fixtures:
            if path.parent.resolve() != output:
                raise RuntimeError("fixture cleanup escaped its task-owned directory")
            path.unlink(missing_ok=True)
        receipt["fixtures_removed"] = all(not path.exists() for path in fixtures)
        receipt["total_rpc"] = sum(item["rpc_calls"] for item in receipt["processes"])
        receipt["elapsed_seconds"] = time.monotonic() - started
        (output / "receipt.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "checks": len(receipt["checks"]),
                      "calls": receipt["total_rpc"], "receipt": str(output / "receipt.json")}), flush=True)
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
