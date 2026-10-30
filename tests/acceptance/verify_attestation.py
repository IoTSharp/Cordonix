"""Verify public release archives with gh's Sigstore verifier and an exact policy."""

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time


REPO = "IoTSharp/Cordonix"
WORKFLOW = REPO + "/.github/workflows/release.yml"
ISSUER = "https://token.actions.githubusercontent.com"
PREDICATE = "https://slsa.dev/provenance/v1"
MAX_ASSET_BYTES = 256 * 1024 * 1024
MAX_LOG_BYTES = 8 * 1024 * 1024
NETWORK_ERRORS = ("dial tcp", "i/o timeout", "tls handshake timeout", "connection refused",
                  "no such host", "proxyconnect", "network is unreachable", "connection reset")


class BasicJobLimits(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_longlong), ("job_time", ctypes.c_longlong),
                ("flags", wintypes.DWORD), ("min_working_set", ctypes.c_size_t),
                ("max_working_set", ctypes.c_size_t), ("active_process_limit", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                ("scheduling_class", wintypes.DWORD)]


class ExtendedJobLimits(ctypes.Structure):
    _fields_ = [("basic", BasicJobLimits), ("io", ctypes.c_ulonglong * 6),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process_memory", ctypes.c_size_t), ("peak_job_memory", ctypes.c_size_t)]


class JobAccounting(ctypes.Structure):
    _fields_ = [("user_time", ctypes.c_longlong), ("kernel_time", ctypes.c_longlong),
                ("period_user_time", ctypes.c_longlong), ("period_kernel_time", ctypes.c_longlong),
                ("page_faults", wintypes.DWORD), ("total_processes", wintypes.DWORD),
                ("active_processes", wintypes.DWORD), ("terminated_processes", wintypes.DWORD)]


class OwnedJob:
    def __init__(self, process):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.kernel.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
        self.kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedJobLimits()
        limits.basic.flags = 0x2000
        if not self.kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)
        times = [wintypes.FILETIME() for _ in range(4)]
        if not self.kernel.GetProcessTimes(int(process._handle), *(ctypes.byref(value) for value in times)):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)
        self.creation_filetime = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime

    def stop(self):
        self.kernel.TerminateJobObject(self.handle, 1)
        deadline = time.monotonic() + 2
        info = JobAccounting()
        for _ in range(40):
            if not self.kernel.QueryInformationJobObject(self.handle, 1, ctypes.byref(info), ctypes.sizeof(info), None):
                return {"observed": False, "active": None}
            if info.active_processes == 0 or time.monotonic() >= deadline:
                break
            time.sleep(0.05)
        return {"observed": True, "total": info.total_processes, "active": info.active_processes}

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def require_budget(deadline):
    if time.monotonic() >= deadline:
        raise TimeoutError("Attestation acceptance exceeded its wall-clock budget")


def digest(path, deadline, maximum=MAX_ASSET_BYTES):
    result = hashlib.sha256()
    total = 0
    with path.open("rb") as stream:
        for _ in range(maximum // 65536 + 2):
            require_budget(deadline)
            chunk = stream.read(65536)
            if not chunk:
                return result.hexdigest()
            total += len(chunk)
            if total > maximum:
                raise ValueError(f"Asset exceeds byte bound: {path.name}")
            result.update(chunk)
    raise ValueError("Asset read did not reach EOF within its bound")


def invoke(gh, arguments, log_root, label, proxy, deadline, audits):
    require_budget(deadline)
    environment = os.environ.copy()
    environment["GH_PROMPT_DISABLED"] = "1"
    if proxy:
        environment["HTTP_PROXY"] = proxy
        environment["HTTPS_PROXY"] = proxy
    else:
        for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
            environment.pop(key, None)
    stdout_path = log_root / (label + ".stdout.json")
    stderr_path = log_root / (label + ".stderr.log")
    process = None
    job = None
    audit = {"program": str(gh), "args": arguments, "parent_pid": os.getpid(),
             "requested_utc": datetime.now(timezone.utc).isoformat(), "proxy": proxy,
             "stdout": str(stdout_path), "stderr": str(stderr_path), "cleanup_complete": False}
    audits.append(audit)
    with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
        try:
            process = subprocess.Popen([str(gh), *arguments], stdin=subprocess.DEVNULL,
                                       stdout=stdout, stderr=stderr, env=environment,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            audit["pid"] = process.pid
            job = OwnedJob(process)
            audit["creation_filetime"] = job.creation_filetime
            audit["exit_code"] = process.wait(timeout=min(45, max(1, deadline - time.monotonic())))
        except subprocess.TimeoutExpired:
            audit["timed_out"] = True
        finally:
            if job:
                audit["job_cleanup"] = job.stop()
                job.close()
            elif process and process.poll() is None:
                process.kill()
            if process:
                try:
                    process.wait(timeout=3)
                finally:
                    job_clean = audit.get("job_cleanup", {}).get("active") == 0 if job else True
                    audit["cleanup_complete"] = process.poll() is not None and job_clean
    if not audit["cleanup_complete"]:
        raise RuntimeError("Owned verifier process cleanup incomplete")
    if stdout_path.stat().st_size > MAX_LOG_BYTES or stderr_path.stat().st_size > MAX_LOG_BYTES:
        raise ValueError("Verifier output exceeded 8 MiB")
    return {"exit_code": audit.get("exit_code"), "timed_out": audit.get("timed_out", False),
            "stdout": stdout_path.read_text(encoding="utf-8"),
            "stderr": stderr_path.read_text(encoding="utf-8")}


def verification_arguments(artifact, bundle, sha, tag):
    return ["attestation", "verify", str(artifact), "--repo", REPO,
            "--signer-workflow", WORKFLOW, "--source-digest", sha,
            "--source-ref", "refs/tags/" + tag, "--deny-self-hosted-runners",
            "--bundle", str(bundle), "--format", "json",
            "--cert-oidc-issuer", ISSUER, "--predicate-type", PREDICATE]


def verify_command(gh, arguments, log_root, label, proxy, deadline, audits):
    result = None
    for attempt in range(2):
        result = invoke(gh, arguments, log_root, label + f"-{attempt + 1}",
                        None if attempt == 0 else proxy, deadline, audits)
        network_failure = result["timed_out"] or any(token in result["stderr"].lower() for token in NETWORK_ERRORS)
        if not network_failure or not proxy or attempt == 1:
            return result
        require_budget(deadline)
        time.sleep(min(1, max(0, deadline - time.monotonic())))
    return result


def inspect_verified_output(output, artifact_name, sha256, source_sha, tag):
    results = json.loads(output)
    if not isinstance(results, list) or not 1 <= len(results) <= 32:
        raise ValueError("Expected a bounded nonempty gh verification result array")
    identities = []
    expected_san = "https://github.com/" + WORKFLOW + "@refs/tags/" + tag
    expected_certificate = {
        "issuer": ISSUER, "subjectAlternativeName": expected_san,
        "sourceRepositoryURI": "https://github.com/" + REPO,
        "sourceRepositoryOwnerURI": "https://github.com/IoTSharp",
        "sourceRepositoryDigest": source_sha, "sourceRepositoryRef": "refs/tags/" + tag,
        "runnerEnvironment": "github-hosted",
    }
    for result in results:
        verified = result["verificationResult"]
        certificate = verified["signature"]["certificate"]
        for field, expected in expected_certificate.items():
            if certificate.get(field) != expected:
                raise ValueError(f"Verified certificate {field} does not match policy")
        statement = verified["statement"]
        if statement.get("_type") != "https://in-toto.io/Statement/v1" or statement.get("predicateType") != PREDICATE:
            raise ValueError("Verified statement type differs from policy")
        subjects = statement.get("subject")
        if not isinstance(subjects, list) or len(subjects) != 1:
            raise ValueError("Each platform bundle must name exactly one archive subject")
        if subjects[0].get("name") != artifact_name or subjects[0].get("digest", {}).get("sha256") != sha256:
            raise ValueError("Archive name or SHA-256 does not match the verified subject")
        timestamps = verified.get("verifiedTimestamps")
        if not isinstance(timestamps, list) or not timestamps:
            raise ValueError("No verified witness timestamp was returned")
        identities.append({"certificate": certificate, "subject": subjects[0],
                           "verified_timestamps": timestamps, "predicate_type": statement["predicateType"]})
    return identities


def verify_case(gh, assets, host, suffix, source_sha, tag, logs, temporary, proxy, deadline, audits):
    name = f"cordonix-{tag[1:]}-{suffix}"
    artifact = (assets / name).resolve(strict=True)
    bundle = (assets / (name + ".sigstore.json")).resolve(strict=True)
    if artifact.parent != assets or bundle.parent != assets:
        raise ValueError("Release assets must be ordinary files immediately within the supplied readback directory")
    archive_sha = digest(artifact, deadline)
    bundle_sha = digest(bundle, deadline, 4 * 1024 * 1024)
    label = host + "-" + suffix.replace(".", "-")
    result = verify_command(gh, verification_arguments(artifact, bundle, source_sha, tag), logs,
                            label + "-original", proxy, deadline, audits)
    if result["exit_code"] != 0 or result["timed_out"]:
        raise ValueError(f"Original archive did not verify: {label}; see its verifier stderr")
    identities = inspect_verified_output(result["stdout"], name, archive_sha, source_sha, tag)
    modified = temporary / (label + "-tampered")
    shutil.copyfile(artifact, modified)
    with modified.open("r+b") as stream:
        original = stream.read(1)
        if not original:
            raise ValueError("Cannot tamper an empty archive fixture")
        stream.seek(0)
        stream.write(bytes([original[0] ^ 1]))
    tampered_sha = digest(modified, deadline)
    if tampered_sha == archive_sha:
        raise AssertionError("Tampered copy unexpectedly has the original SHA")
    negative = verify_command(gh, verification_arguments(modified, bundle, source_sha, tag), logs,
                              label + "-tampered", proxy, deadline, audits)
    diagnostic = negative["stderr"].lower()
    mismatch = re.search(r"(digest.*(match|mismatch)|(match|mismatch).*digest|no matching subject|no subject found)", diagnostic)
    if negative["exit_code"] in (None, 0) or negative["timed_out"] or not mismatch or any(token in diagnostic for token in NETWORK_ERRORS):
        raise ValueError("Tamper fixture lacks a conclusive artifact-digest rejection")
    if digest(artifact, deadline) != archive_sha or digest(bundle, deadline, 4 * 1024 * 1024) != bundle_sha:
        raise AssertionError("Public readback asset changed during verification")
    return {"status": "PASS", "host": host, "artifact": str(artifact), "bundle": str(bundle),
            "archive_sha256": archive_sha, "bundle_sha256": bundle_sha,
            "verified_identities": identities, "signature_verification": "PASS",
            "tamper_rejection": "PASS", "tampered_sha256": tampered_sha,
            "tamper_exit_code": negative["exit_code"], "original_assets_unchanged": True}


def cancel(*_):
    raise KeyboardInterrupt()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gh", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--github-assets", type=Path)
    parser.add_argument("--gitee-assets", type=Path)
    parser.add_argument("--source-sha")
    parser.add_argument("--tag", default="v0.0.4")
    parser.add_argument("--proxy", default="http://127.0.0.1:7890")
    parser.add_argument("--check-tools", action="store_true")
    parser.add_argument("--pilot", action="store_true", help="Verify only the GitHub Windows archive and its tampered copy")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This acceptance runner uses Windows owned Job cleanup")
    gh = args.gh.resolve(strict=True)
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to((root / "build").resolve()) or output.exists():
        parser.error("Use a new receipt path below the checkout build directory")
    if not args.check_tools and (not args.github_assets or (not args.pilot and not args.gitee_assets) or
                                not args.source_sha or not re.fullmatch(r"[0-9a-f]{40}", args.source_sha) or
                                not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", args.tag)):
        parser.error("Verification requires public readback directories, exact lowercase 40-hex source SHA and stable tag")
    output.parent.mkdir(parents=True, exist_ok=True)
    logs = output.parent / (output.stem + "-logs")
    logs.mkdir()
    temporary = Path(tempfile.mkdtemp(prefix=output.stem + "-tampered-", dir=output.parent)).resolve()
    deadline = time.monotonic() + (60 if args.check_tools else 600)
    signal.signal(signal.SIGTERM, cancel)
    receipt = {"status": "FAIL", "schema": 1, "tag": args.tag, "source_sha": args.source_sha,
               "scope": "tool_preflight" if args.check_tools else "one_archive_pilot" if args.pilot else "both_hosts_two_platforms",
               "policy": {"repo": REPO, "signer_workflow": WORKFLOW, "source_ref": "refs/tags/" + args.tag,
                          "issuer": ISSUER, "predicate_type": PREDICATE, "deny_self_hosted_runners": True},
               "authenticode": "NOT_RUN", "cases": [], "processes": [], "temporary_cleaned": False}
    started = time.monotonic()
    try:
        version = invoke(gh, ["--version"], logs, "gh-version", None, deadline, receipt["processes"])
        receipt["gh_version"] = version["stdout"].strip()
        if version["exit_code"] != 0:
            raise ValueError("gh version check failed")
        if args.check_tools:
            help_text = invoke(gh, ["attestation", "verify", "--help"], logs, "gh-verify-help", None, deadline, receipt["processes"])
            required = ("--bundle", "--repo", "--signer-workflow", "--source-digest", "--source-ref",
                        "--deny-self-hosted-runners", "--format", "--cert-oidc-issuer", "--predicate-type")
            if help_text["exit_code"] != 0 or not all(flag in help_text["stdout"] for flag in required):
                raise ValueError("Installed gh is missing an exact policy option")
            receipt["signature_verification"] = "NOT_RUN"
        else:
            sources = [("github", args.github_assets.resolve(strict=True))]
            if not args.pilot:
                sources.append(("gitee", args.gitee_assets.resolve(strict=True)))
            for host, assets in sources:
                suffixes = ("win-x64.zip",) if args.pilot else ("win-x64.zip", "linux-x64.tar.gz")
                for suffix in suffixes:
                    case = verify_case(gh, assets, host, suffix, args.source_sha, args.tag, logs,
                                       temporary, args.proxy, deadline, receipt["processes"])
                    receipt["cases"].append(case)
                    print(json.dumps({"status": case["status"], "host": host, "platform": suffix,
                                      "archive_sha256": case["archive_sha256"]}), flush=True)
            if not args.pilot:
                for index in range(2):
                    first, second = receipt["cases"][index], receipt["cases"][index + 2]
                    if first["archive_sha256"] != second["archive_sha256"] or first["bundle_sha256"] != second["bundle_sha256"]:
                        raise AssertionError("GitHub and Gitee public archive/bundle bytes differ")
                receipt["mirrored_assets_identical"] = True
        receipt["status"] = "PASS"
    except BaseException as error:
        receipt["error"] = f"{type(error).__name__}: {error}"
    finally:
        try:
            if temporary.parent == output.parent and temporary.name.startswith(output.stem + "-tampered-"):
                shutil.rmtree(temporary)
                receipt["temporary_cleaned"] = not temporary.exists()
        except OSError as error:
            receipt["status"] = "FAIL"
            receipt["cleanup_error"] = f"{type(error).__name__}: {error}"
        receipt["elapsed_seconds"] = time.monotonic() - started
        output.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": receipt["status"], "scope": receipt["scope"], "receipt": str(output)}), flush=True)
    return 0 if receipt["status"] == "PASS" and receipt["temporary_cleaned"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
