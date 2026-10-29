#!/bin/sh
set -eu

command -v python3 >/dev/null 2>&1 || { echo 'Python 3 is required by the installer; the cdx runtime itself does not require Python.' >&2; exit 2; }
command -v timeout >/dev/null 2>&1 || { echo 'GNU timeout is required by the installer.' >&2; exit 2; }
script_path=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
exec timeout --kill-after=5s 180s python3 - "$script_path" "$@" <<'PY'
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

SCRIPT = Path(sys.argv[1])
START = time.monotonic()
DEADLINE = START + 170
STABLE = re.compile(r"^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
GITHUB = "https://api.github.com/repos/IoTSharp/Cordonix/releases/latest"
GITEE = "https://gitee.com/api/v5/repos/IoTSharp/Cordonix/releases?page=1&per_page=20"


def cancelled(_number, _frame):
    raise InterruptedError("Installation was cancelled; inspect current before retrying")


signal.signal(signal.SIGINT, cancelled)
signal.signal(signal.SIGTERM, cancelled)


def budget():
    if time.monotonic() >= DEADLINE:
        raise RuntimeError("Installation exceeded its wall-clock deadline")


def version_tuple(tag):
    match = STABLE.fullmatch(tag) if isinstance(tag, str) else None
    return tuple(int(part) for part in match.groups()) if match else None


def check_url(url, test_http):
    parsed = urllib.parse.urlsplit(url)
    if parsed.username or parsed.password or parsed.fragment:
        raise ValueError("Download URL must not contain credentials or a fragment")
    if parsed.scheme != "https" and not (test_http and parsed.scheme == "http" and parsed.hostname in ("localhost", "127.0.0.1", "::1")):
        raise ValueError("Downloads require HTTPS; test HTTP is restricted to loopback")


def download(url, target, limit, test_http):
    budget()
    check_url(url, test_http)
    request = urllib.request.Request(url, headers={"User-Agent": "Cordonix-installer", "Accept": "application/json, application/octet-stream"})
    with urllib.request.urlopen(request, timeout=min(20, max(1, DEADLINE - time.monotonic()))) as response:
        check_url(response.url, test_http)
        if int(response.headers.get("Content-Length", "0")) > limit:
            raise ValueError("Download exceeds its byte limit")
        total = 0
        with target.open("wb") as output:
            for _ in range(limit // 65536 + 2):
                budget()
                block = response.read(min(65536, limit - total + 1))
                if not block:
                    return
                total += len(block)
                if total > limit:
                    raise ValueError("Download exceeds its byte limit")
                output.write(block)
    raise RuntimeError("Download did not terminate within its byte bound")


def run(arguments, seconds=10):
    budget()
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    print(json.dumps({"process_pid": process.pid, "parent_pid": os.getpid(), "started_utc": started, "arguments": arguments}), file=sys.stderr)
    try:
        stdout, stderr = process.communicate(timeout=min(seconds, max(1, DEADLINE - time.monotonic())))
        if len(stdout) + len(stderr) > 1048576:
            raise RuntimeError("Verification output exceeds 1 MiB")
        if process.returncode:
            raise RuntimeError("Verification command failed: " + stderr[:1024].decode("utf-8", errors="replace"))
        return stdout.decode("utf-8", errors="strict").strip()
    finally:
        # Only the session created for this exact subprocess is reclaimed.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=3)


def verify_runtime(directory, version, work):
    binary = directory / "cdx"
    if not binary.is_file() or binary.is_symlink():
        raise ValueError("Package is missing a regular cdx executable")
    binary.chmod(binary.stat().st_mode | 0o100)
    if run([str(binary), "--version"]) != "Cordonix cdx " + version:
        raise ValueError("Downloaded executable version does not match its release tag")
    request = work / "tool-info.json"
    request.write_text('{"operation":"tool_info","arguments":{}}', encoding="utf-8")
    info = json.loads(run([str(binary), "--request", str(request)]))
    if any(info.get(key) != value for key, value in {"status": "ok", "name": "Cordonix", "version": version, "platform": "linux", "runtime": "C11"}.items()):
        raise ValueError("Downloaded executable tool_info contract does not match")


def extract_archive(archive, destination):
    with tarfile.open(archive, "r:gz") as package:
        members = []
        total = 0
        for _ in range(129):
            budget()
            member = package.next()
            if member is None:
                break
            name = PurePosixPath(member.name)
            if name.is_absolute() or ".." in name.parts or "\\" in member.name or not (member.isfile() or member.isdir()):
                raise ValueError("Package contains an unsafe path or a nonregular entry")
            total += member.size
            if len(members) >= 128 or member.size > 33554432 or total > 67108864:
                raise ValueError("Package exceeds the entry or extracted size limit")
            members.append(member)
        else:
            raise ValueError("Package exceeds the entry limit")
        for member in members:
            budget()
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                source = package.extractfile(member)
                if source is None:
                    raise ValueError("Package entry could not be read")
                with source, target.open("xb") as output:
                    shutil.copyfileobj(source, output, 65536)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)


def release_candidate(metadata):
    releases = metadata if isinstance(metadata, list) else [metadata]
    if len(releases) > 20:
        raise ValueError("Release listing exceeds 20 entries")
    stable = [item for item in releases if isinstance(item, dict) and not item.get("draft") and not item.get("prerelease") and version_tuple(item.get("tag_name")) is not None]
    if not stable:
        raise ValueError("No stable release tag is available")
    release = max(stable, key=lambda item: version_tuple(item["tag_name"]))
    version = release["tag_name"][1:]
    asset = "cordonix-" + version + "-linux-x64.tar.gz"
    assets = release.get("assets", [])
    if not isinstance(assets, list) or len(assets) > 64:
        raise ValueError("Release assets must be a bounded array")
    selected = []
    for name in (asset, asset + ".sha256"):
        matches = [item for item in assets if isinstance(item, dict) and item.get("name") == name]
        if len(matches) != 1 or not isinstance(matches[0].get("browser_download_url"), str):
            raise ValueError("Release is missing an exact archive/checksum asset")
        selected.append(matches[0]["browser_download_url"])
    return version, asset, selected


def systemd(root, source, mode, definition_only):
    unit = "cordonix-update-" + hashlib.sha256(os.fsencode(root)).hexdigest()[:12]
    directory = root / "systemd" if definition_only else Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "systemd/user"
    if mode == "disable":
        if not definition_only:
            run(["systemctl", "--user", "disable", "--now", unit + ".timer"], 15)
        for suffix in (".service", ".timer"):
            path = directory / (unit + suffix)
            if path.exists():
                if "# Cordonix installer: " + str(root) not in path.read_text(encoding="utf-8"):
                    raise ValueError("Refusing to remove a unit owned by another installation")
                path.unlink()
        if not definition_only:
            run(["systemctl", "--user", "daemon-reload"], 15)
        return "disabled"
    updater = root / "updater/install.sh"
    updater.parent.mkdir(parents=True, exist_ok=True)
    temporary = updater.with_suffix(".new")
    shutil.copyfile(SCRIPT, temporary)
    os.replace(temporary, updater)
    def quote(text):
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%").replace("$", "$$") + '"'
    marker = "# Cordonix installer: " + str(root) + "\n"
    service = marker + "[Unit]\nDescription=Check and update Cordonix for this user\n[Service]\nType=oneshot\nTimeoutStartSec=190\nKillMode=control-group\nExecStart=/bin/sh " + quote(str(updater)) + " --root " + quote(str(root)) + " --source " + source + "\n"
    timer = marker + "[Unit]\nDescription=Check Cordonix releases hourly\n[Timer]\nOnBootSec=5min\nOnUnitActiveSec=1h\nPersistent=true\n[Install]\nWantedBy=timers.target\n"
    directory.mkdir(parents=True, exist_ok=True)
    for suffix, text in ((".service", service), (".timer", timer)):
        path = directory / (unit + suffix)
        if path.exists() and marker not in path.read_text(encoding="utf-8"):
            raise ValueError("Unit file belongs to another installation")
        temporary = path.with_suffix(path.suffix + ".new")
        temporary.write_text(text, encoding="utf-8")
        os.replace(temporary, path)
    if definition_only:
        return "definition_only_NOT_RUN"
    run(["systemctl", "--user", "daemon-reload"], 15)
    run(["systemctl", "--user", "enable", "--now", unit + ".timer"], 15)
    if run(["systemctl", "--user", "is-enabled", unit + ".timer"]) != "enabled":
        raise RuntimeError("Auto-update timer was not enabled")
    return "enabled"


def main():
    parser = argparse.ArgumentParser(description="Install/update Cordonix Linux x64 using Python 3 standard library and GNU timeout")
    parser.add_argument("--root", default=str(Path.home() / ".local/opt/cordonix"))
    parser.add_argument("--source", choices=("auto", "github", "gitee"), default="auto")
    parser.add_argument("--check", action="store_true")
    schedule = parser.add_mutually_exclusive_group()
    schedule.add_argument("--enable-auto-update", action="store_true")
    schedule.add_argument("--disable-auto-update", action="store_true")
    parser.add_argument("--schedule-dry-run", action="store_true", help="Write isolated unit definitions under the install root without registering a user timer")
    parser.add_argument("--github-api", default=GITHUB, help=argparse.SUPPRESS)
    parser.add_argument("--gitee-api", default=GITEE, help=argparse.SUPPRESS)
    parser.add_argument("--allow-test-http", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(sys.argv[2:])
    if args.check and (args.enable_auto_update or args.disable_auto_update):
        raise ValueError("--check cannot change the auto-update schedule")
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "amd64"):
        return {"status": "NOT_RUN", "operation": "unsupported_platform"}, 2
    if not os.path.isabs(args.root) or any(value in args.root for value in ("\x00", "\n", "\r", "$(", "`")):
        raise ValueError("Install root must be a literal absolute path")
    root = Path(args.root).resolve()
    if root == Path("/"):
        raise ValueError("Install root must be a dedicated directory")
    root.mkdir(parents=True, exist_ok=True)
    if args.disable_auto_update:
        return {"status": "PASS", "operation": "disable_auto_update", "auto_update": systemd(root, args.source, "disable", args.schedule_dry_run)}, 0
    current = root / "current"
    if current.exists() and not current.is_symlink():
        raise ValueError("The current installation entry must be an installer-managed symlink")
    previous = "none"
    if current.exists():
        report = run([str(current / "cdx"), "--version"])
        previous = report[len("Cordonix cdx "):] if report.startswith("Cordonix cdx ") else ""
        if report != "Cordonix cdx " + previous or version_tuple("v" + previous) is None:
            raise ValueError("Current installation has an unknown version")
    sources = ("github", "gitee") if args.source == "auto" else (args.source,)
    receipt = {"status": "PASS", "operation": "install", "source": "none", "previous_version": previous, "config_updated": False, "auto_update": "not_enabled", "command": str(current / "cdx")}
    with tempfile.TemporaryDirectory(prefix=".install-", dir=root) as temporary:
        work = Path(temporary)
        failures = []
        for source in sources:
            try:
                endpoint = args.github_api if source == "github" else args.gitee_api
                download(endpoint, work / "release.json", 1048576, args.allow_test_http)
                version, asset, urls = release_candidate(json.loads((work / "release.json").read_text(encoding="utf-8")))
                for url in urls:
                    check_url(url, args.allow_test_http)
                receipt.update(source=source, tag="v" + version, asset=asset, installed_version=previous)
                comparison = 1 if previous == "none" else (version_tuple("v" + version) > version_tuple("v" + previous)) - (version_tuple("v" + version) < version_tuple("v" + previous))
                if comparison <= 0 or args.check:
                    receipt["operation"] = "up_to_date" if comparison == 0 else "older_release" if comparison < 0 else "update_available"
                    break
                download(urls[0], work / asset, 268435456, args.allow_test_http)
                download(urls[1], work / (asset + ".sha256"), 4096, args.allow_test_http)
                checksum = (work / (asset + ".sha256")).read_text(encoding="ascii")
                match = re.fullmatch(r"([0-9A-Fa-f]{64})[ \t]+\*?" + re.escape(asset) + r"\r?\n?", checksum)
                digest = hashlib.sha256()
                with (work / asset).open("rb") as downloaded:
                    for _ in range(4097):
                        budget()
                        block = downloaded.read(65536)
                        if not block:
                            break
                        digest.update(block)
                actual = digest.hexdigest()
                if not match or match[1].lower() != actual:
                    raise ValueError("SHA-256 checksum or its filename does not match the downloaded asset")
                payload = work / "payload"
                payload.mkdir()
                extract_archive(work / asset, payload)
                verify_runtime(payload, version, work)
                destination = root / "versions" / version
                destination.parent.mkdir(exist_ok=True)
                if destination.exists():
                    verify_runtime(destination, version, work)
                else:
                    payload.rename(destination)
                pointer = work / "current.new"
                pointer.symlink_to(destination)
                os.replace(pointer, current)
                receipt.update(operation="install" if previous == "none" else "update", installed_version=version, install_path=str(destination), sha256=actual)
                break
            except InterruptedError:
                raise
            except (OSError, ValueError, RuntimeError, EOFError, tarfile.TarError, urllib.error.URLError) as error:
                failures.append(source + ": " + str(error))
                print(failures[-1], file=sys.stderr)
                if (work / "payload").exists():
                    shutil.rmtree(work / "payload")
        else:
            raise RuntimeError("No source completed installation: " + "; ".join(failures))
        receipt["fallback_reasons"] = failures
        if args.enable_auto_update:
            if args.allow_test_http and not args.schedule_dry_run:
                raise ValueError("Test HTTP endpoints cannot be registered for automatic updates")
            try:
                receipt["auto_update"] = systemd(root, args.source, "enable", args.schedule_dry_run)
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                receipt.update(status="FAIL", auto_update="UNKNOWN", auto_update_error=str(error))
        if not args.check:
            saved = work / "receipt.json"
            saved.write_text(json.dumps(receipt, ensure_ascii=False) + "\n", encoding="utf-8")
            os.replace(saved, root / "install-receipt.json")
    return receipt, 0 if receipt["status"] == "PASS" else 1


try:
    result, code = main()
except InterruptedError as error:
    result, code = {"status": "UNKNOWN", "operation": "cancelled", "error": str(error), "config_updated": False}, 130
except (OSError, ValueError, RuntimeError, EOFError, subprocess.SubprocessError, tarfile.TarError) as error:
    result, code = {"status": "FAIL", "error": str(error), "config_updated": False}, 1
print(json.dumps(result, ensure_ascii=False))
sys.exit(code)
PY
