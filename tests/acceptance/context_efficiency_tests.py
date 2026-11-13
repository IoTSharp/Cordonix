"""Bounded standard-library regression checks for the CDX-14C harness."""

import json
import io
from pathlib import Path
import queue
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import sys

sys.dont_write_bytecode = True
import context_efficiency_baseline as baseline


class MeasurementChecks(unittest.TestCase):
    def test_transport_timeout_is_retained_without_replay(self):
        with tempfile.TemporaryDirectory(prefix="context-efficiency-test-") as temporary:
            client = baseline.StdioClient.__new__(baseline.StdioClient)
            client.deadline = time.monotonic() + 2
            client.archive = Path(temporary)
            client.sequence = 0
            client.transport_failed = False
            client.audit = {"rpc_calls": 0}
            client.process = SimpleNamespace(stdin=io.BytesIO())
            client.responses = Mock()
            client.responses.get.side_effect = queue.Empty
            _, record = client.execute({"program": "fixture"}, "full")
            self.assertEqual(record["outcome"], "unknown")
            self.assertIsNone(record["mcp_response_utf8_bytes"])
            self.assertIsNone(record["mcp_response_sha256"])
            self.assertEqual(client.audit["rpc_calls"], 1)
            self.assertEqual(len(client.audit["transport_failures"]), 1)
            wire_before = client.process.stdin.getvalue()
            with self.assertRaises(RuntimeError):
                client.execute({"program": "fixture"}, "full")
            self.assertEqual(client.process.stdin.getvalue(), wire_before)
            receipt = {"execute": {"full": [record], "summary": []}, "tools_list": []}
            baseline.aggregate_receipt(receipt)
            self.assertEqual(receipt["execute"]["full_summary"]["unknown"], 1)
            self.assertIsNone(receipt["execute"]["full_summary"]["median_mcp_response_bytes"])

    def test_broken_pipe_is_unknown_with_one_write_attempt(self):
        with tempfile.TemporaryDirectory(prefix="context-efficiency-test-") as temporary:
            client = baseline.StdioClient.__new__(baseline.StdioClient)
            client.deadline = time.monotonic() + 2
            client.archive = Path(temporary)
            client.sequence = 0
            client.transport_failed = False
            client.audit = {"rpc_calls": 0}
            stream = Mock()
            stream.write.side_effect = BrokenPipeError("fixture closed")
            client.process = SimpleNamespace(stdin=stream)
            _, record = client.execute({}, "full")
            self.assertEqual(record["outcome"], "unknown")
            self.assertIn("BrokenPipeError", record["transport_error"])
            stream.write.assert_called_once()

    def test_observed_completion_does_not_pass_expected_timeout(self):
        client = baseline.StdioClient.__new__(baseline.StdioClient)
        client.rpc = Mock(return_value=({"result": {"content": [{"text": '{"status":"completed","process_status":"completed"}'}]}}, {}))
        _, record = client.execute({}, "full", expected_status="timed_out")
        self.assertEqual(record["status"], "completed")
        self.assertEqual(record["observed_outcome"], "returned")
        self.assertFalse(record["acceptance_success"])
        self.assertEqual(record["outcome"], "failed")

    def test_tools_array_bytes_are_distinct_from_wire_bytes(self):
        client = baseline.StdioClient.__new__(baseline.StdioClient)
        client.rpc = Mock(return_value=({"result": {"tools": [{"name": "中文"}]}}, {"mcp_response_utf8_bytes": 200}))
        _, record = client.tools_list("full")
        self.assertEqual(record["tools_array_utf8_bytes"], len('[{"name":"中文"}]'.encode("utf-8")))
        self.assertEqual(record["mcp_response_utf8_bytes"], 200)

    def test_tokenizer_batches_more_than_single_session_limit(self):
        with tempfile.TemporaryDirectory(prefix="context-efficiency-test-") as temporary:
            root = Path(temporary)
            raw = root / "response.json"
            raw.write_bytes(b'{"result":{}}\n')
            config = root / "tokenizer.json"
            config.write_text(json.dumps({"name": "fixture-only", "version": "test", "argv": [str(raw.resolve())]}), encoding="utf-8")
            receipt = {"tools_list": [{"response_file": str(raw)} for _ in range(65)], "execute": {"full": [], "summary": []}}
            sessions = []

            def make_client(*_args):
                client = Mock()
                client.audit = {"cleanup_complete": True}
                client.execute.return_value = ({}, {"tool_result": {"status": "completed", "cleanup_complete": True, "stdout": '{"tokens":2}'}})
                sessions.append(client)
                return client

            with patch.object(baseline, "StdioClient", side_effect=make_client):
                baseline.apply_tokenizer(receipt, config, time.monotonic() + 2, raw, root, root)
            self.assertEqual([client.execute.call_count for client in sessions], [60, 5])
            self.assertTrue(all(client.close.call_count == 1 for client in sessions))
            self.assertEqual(receipt["tokenizer"]["status"], "MEASURED")
            self.assertEqual(receipt["tokenizer"]["tokens"], 130)

    def test_failed_run_still_writes_unknown_summary(self):
        root = Path(baseline.__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="context-efficiency-test-", dir=root / "build") as temporary:
            output = Path(temporary) / "failed.json"
            client = Mock()
            client.tools_list.return_value = ({}, {"profile": "default", "outcome": "unknown", "transport_error": "fixture timeout", "elapsed_ms": 1, "mcp_response_utf8_bytes": None})
            argv = ["baseline", "--executable", __file__, "--process-fixture", __file__, "--output", str(output), "--pilot"]
            with patch.object(sys, "argv", argv), patch.object(baseline, "StdioClient", return_value=client), patch("sys.stdout", io.StringIO()):
                self.assertEqual(baseline.main(), 1)
            receipt = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(receipt["tools_list_summary"]["default"]["unknown"], 1)
            self.assertIsNone(receipt["tools_list_summary"]["default"]["median_mcp_response_bytes"])
            self.assertEqual(receipt["status"], "FAIL")
            client.close.assert_called_once()

    def test_partial_tokenizer_failure_does_not_publish_aggregate(self):
        with tempfile.TemporaryDirectory(prefix="context-efficiency-test-") as temporary:
            root = Path(temporary)
            raw = root / "response.json"
            raw.write_bytes(b"{}\n")
            config = root / "tokenizer.json"
            config.write_text(json.dumps({"name": "fixture-only", "version": "test", "argv": [str(raw.resolve())]}), encoding="utf-8")
            receipt = {"tools_list": [{"response_file": str(raw)}, {"response_file": str(raw)}], "execute": {"full": [], "summary": []}}
            client = Mock()
            client.audit = {"cleanup_complete": True}
            client.execute.side_effect = [({}, {"tool_result": {"status": "completed", "cleanup_complete": True, "stdout": '{"tokens":2}'}}), ({}, {"tool_result": {"status": "timed_out"}})]
            with patch.object(baseline, "StdioClient", return_value=client), self.assertRaises(ValueError):
                baseline.apply_tokenizer(receipt, config, time.monotonic() + 2, raw, root, root)
            self.assertEqual(receipt["tokenizer"]["status"], "NOT_MEASURED")
            self.assertEqual(receipt["tokenizer"]["measured_samples"], 1)
            self.assertIsNone(receipt["tokenizer"]["tokens"])
            client.close.assert_called_once()

    def test_repetition_and_deadline_bounds_reject_before_launch(self):
        for option, value in (("--repetitions", "6"), ("--repetitions", "0"), ("--deadline-seconds", "nan"), ("--deadline-seconds", "601")):
            with self.subTest(option=option, value=value), tempfile.TemporaryDirectory(prefix="context-efficiency-test-") as temporary:
                argv = ["baseline", "--executable", __file__, "--process-fixture", __file__, "--output", str(Path(baseline.__file__).resolve().parents[2] / "build" / ("invalid-" + Path(temporary).name + ".json")), option, value]
                with patch.object(sys, "argv", argv), patch("sys.stderr", io.StringIO()), patch.object(baseline, "StdioClient") as client:
                    with self.assertRaises(SystemExit):
                        baseline.main()
                    client.assert_not_called()
    def test_empty_measurement_is_not_fabricated(self):
        result = baseline.summarize([])
        self.assertEqual(result["calls"], 0)
        self.assertIsNone(result["median_mcp_response_bytes"])
        self.assertIsNone(result["p95_elapsed_ms"])

    def test_summary_keeps_failed_and_unknown_samples(self):
        records = [{"mcp_response_utf8_bytes": 10, "elapsed_ms": 2, "outcome": "success"},
                   {"mcp_response_utf8_bytes": 30, "elapsed_ms": 6, "outcome": "failed"},
                   {"mcp_response_utf8_bytes": 20, "elapsed_ms": 4, "outcome": "unknown", "retry_count": 1}]
        result = baseline.summarize(records)
        self.assertEqual((result["calls"], result["success"], result["failed"], result["unknown"], result["retries"]), (3, 1, 1, 1, 1))
        self.assertEqual(result["median_mcp_response_bytes"], 20)
        self.assertEqual(result["p95_mcp_response_bytes"], 29)
        self.assertIs(result["records"], records)

    def test_fixed_corpus_contains_failure_boundaries(self):
        cases = baseline.load_cases(Path(__file__).with_name("context_efficiency_corpus.json"))
        self.assertEqual({case["expected_status"] for case in cases}, {"completed", "output_limit", "encoding_error", "timed_out", "cancelled"})

    def test_oversized_corpus_rejected_before_execution(self):
        with tempfile.TemporaryDirectory(prefix="context-efficiency-test-") as temporary:
            path = Path(temporary) / "corpus.json"
            path.write_text(json.dumps({"execute_cases": [{}] * 17}), encoding="utf-8")
            with self.assertRaises(ValueError):
                baseline.load_cases(path)


if __name__ == "__main__":
    unittest.main()
