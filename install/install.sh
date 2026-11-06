#!/bin/sh
# Cordonix updater generation: 3
set -eu

command -v python3 >/dev/null 2>&1 || { echo 'Python 3 is required by the installer; the cdx runtime itself does not require Python.' >&2; exit 2; }
command -v timeout >/dev/null 2>&1 || { echo 'GNU timeout is required by the installer.' >&2; exit 2; }
script_path=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
exec timeout --kill-after=5s 180s python3 - "$script_path" "$@" <<'PY'
import argparse
import datetime
import fcntl
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
UPDATE_LOCK = None
ATTEMPT_PATH = None
SOURCE_DEADLINE = DEADLINE
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
    with urllib.request.urlopen(request, timeout=min(20, max(0.1, SOURCE_DEADLINE - time.monotonic()))) as response:
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
    return info


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
    def quote(text):
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%").replace("$", "$$") + '"'
    marker = "# Cordonix installer: " + str(root) + "\n"
    service = marker + "[Unit]\nDescription=Check and update Cordonix for this user\n[Service]\nType=oneshot\nTimeoutStartSec=190\nKillMode=control-group\nExecStart=/bin/sh " + quote(str(updater)) + " --root " + quote(str(root)) + " --automatic\n"
    timer = marker + "[Unit]\nDescription=Check Cordonix releases daily\n[Timer]\nOnBootSec=5min\nOnUnitActiveSec=1d\nPersistent=true\n[Install]\nWantedBy=timers.target\n"
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
    global DEADLINE, SOURCE_DEADLINE, UPDATE_LOCK, ATTEMPT_PATH
    parser = argparse.ArgumentParser(description="Install/update Cordonix Linux x64 using Python 3 standard library and GNU timeout")
    parser.add_argument("--root", default=str(Path.home() / ".local/opt/cordonix"))
    parser.add_argument("--source", choices=("auto", "github", "gitee"), default="auto")
    parser.add_argument("--check", action="store_true")
    schedule = parser.add_mutually_exclusive_group()
    schedule.add_argument("--enable-auto-update", action="store_true")
    schedule.add_argument("--disable-auto-update", action="store_true")
    parser.add_argument("--schedule-dry-run", action="store_true", help="Write isolated unit definitions under the install root without registering a user timer")
    parser.add_argument("--automatic", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--startup-check", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--time-budget", type=int, default=170, help=argparse.SUPPRESS)
    parser.add_argument("--github-api", default=GITHUB, help=argparse.SUPPRESS)
    parser.add_argument("--gitee-api", default=GITEE, help=argparse.SUPPRESS)
    parser.add_argument("--allow-test-http", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(sys.argv[2:])
    if not 1 <= args.time_budget <= 170:
        raise ValueError("Time budget must be between 1 and 170 seconds")
    DEADLINE = START + args.time_budget
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
    settings_path = root / "update-settings.json"
    attempt_path = root / "last-update-check.json"
    settings = json.loads(settings_path.read_text(encoding="utf-8")) if settings_path.exists() else {"enabled": True, "source": args.source, "github_api": args.github_api, "gitee_api": args.gitee_api, "allow_test_http": args.allow_test_http, "proxy_env": {key: os.environ[key] for key in ("HTTPS_PROXY", "HTTP_PROXY", "NO_PROXY", "https_proxy", "http_proxy", "no_proxy") if key in os.environ}}
    if args.automatic:
        if not settings["enabled"]:
            return {"status": "PASS", "operation": "auto_update_disabled"}, 0
        args.source = settings["source"]
        args.github_api = settings["github_api"]
        args.gitee_api = settings["gitee_api"]
        args.allow_test_http = settings["allow_test_http"]
        os.environ.update(settings["proxy_env"])
    if not args.check:
        UPDATE_LOCK = (root / ".update.lock").open("a")
        try:
            fcntl.flock(UPDATE_LOCK, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            if args.automatic:
                return {"status": "PASS", "operation": "update_in_progress"}, 0
            raise RuntimeError("Another updater owns this installation")
        if args.automatic and args.startup_check and attempt_path.exists():
            last = json.loads(attempt_path.read_text(encoding="utf-8"))
            if time.localtime(last["checked_unix"])[:3] == time.localtime()[:3]:
                return {"status": "PASS", "operation": "check_throttled"}, 0
    if args.disable_auto_update:
        settings["enabled"] = False
        atomic_json(settings_path, settings)
        return {"status": "PASS", "operation": "disable_auto_update", "auto_update": systemd(root, args.source, "disable", args.schedule_dry_run)}, 0
    if args.enable_auto_update:
        settings["enabled"] = True
    if not args.automatic and not args.check:
        settings.update(source=args.source, github_api=args.github_api, gitee_api=args.gitee_api, allow_test_http=args.allow_test_http)
    if not args.check:
        ATTEMPT_PATH = attempt_path
        atomic_json(attempt_path, {"status": "checking", "checked_unix": time.time()})
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
    receipt = {"status": "PASS", "operation": "install", "source": "none", "previous_version": previous, "config_updated": False, "auto_update": "disabled" if not settings["enabled"] else "daily_startup", "command": str(root / "launch.sh")}
    with tempfile.TemporaryDirectory(prefix=".install-", dir=root) as temporary:
        work = Path(temporary)
        failures = []
        for source in sources:
            try:
                SOURCE_DEADLINE = time.monotonic() + (DEADLINE - time.monotonic()) / 2 if args.automatic and args.source == "auto" and source == "github" else DEADLINE
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
                info = verify_runtime(payload, version, work)
                settings['supports_checkupgrade'] = info.get('update_command') == '--checkupgrade'
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
        if not args.check:
            updater = root / "updater/install.sh"
            updater.parent.mkdir(parents=True, exist_ok=True)
            updater_text = SCRIPT.read_text(encoding="utf-8")
            for packaged in (current / "install/install.sh", updater):
                if packaged.is_file():
                    packaged_text = packaged.read_text(encoding="utf-8")
                    pattern = r"(?m)^# Cordonix updater generation: ([0-9]+)$"
                    generation = re.search(pattern, updater_text)
                    packaged_generation = re.search(pattern, packaged_text)
                    if packaged_generation and int(packaged_generation[1]) > int(generation[1]):
                        updater_text = packaged_text
            temporary_updater = updater.with_suffix(".new")
            temporary_updater.write_text(updater_text, encoding="utf-8")
            os.replace(temporary_updater, updater)
            python_updater = updater.with_suffix('.py')
            python_source = updater_text.split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
            temporary_python = python_updater.with_suffix('.new')
            temporary_python.write_text(python_source, encoding='utf-8')
            os.replace(temporary_python, python_updater)
            launcher = root / "launch.sh"
            temporary_launcher = launcher.with_suffix(".new")
            temporary_launcher.write_text('''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 - "$root" <<'LAUNCH' || :
import fcntl, json, os, subprocess, sys, time
from pathlib import Path
root = Path(sys.argv[1])
try:
    settings = json.loads((root / "update-settings.json").read_text(encoding="utf-8"))
    if settings["enabled"]:
        with (root / ".update-launch.lock").open("a") as lease:
            fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
            due = True
            for name, key in (("last-update-check.json", "checked_unix"), ("last-update-launch.json", "started_unix")):
                path = root / name
                if path.exists() and time.localtime(json.loads(path.read_text(encoding="utf-8"))[key])[:3] == time.localtime()[:3]:
                    due = False
            if due:
                arguments = [str(root / "current/cdx"), "--checkupgrade"] if settings.get("supports_checkupgrade") else ["/bin/sh", str(root / "updater/install.sh"), "--root", str(root), "--automatic", "--startup-check"]
                with (root / "upgrade-worker.stdout.json").open("wb") as output, (root / "upgrade-worker.stderr.log").open("wb") as errors:
                    worker = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=output, stderr=errors, start_new_session=True)
                audit = {"pid": worker.pid, "parent_pid": os.getpid(), "started_unix": time.time(), "arguments": arguments}
                stat = Path("/proc") / str(worker.pid) / "stat"
                try:
                    audit["start_ticks"] = stat.read_text().rsplit(") ", 1)[1].split()[19]
                except (FileNotFoundError, ProcessLookupError):
                    audit["start_ticks"] = None
                temporary = root / "last-update-launch.new"
                temporary.write_text(json.dumps(audit), encoding="utf-8")
                os.replace(temporary, root / "last-update-launch.json")
except (OSError, ValueError, KeyError) as error:
    print("Cordonix background update check could not start: " + str(error), file=sys.stderr)
LAUNCH
exec "$root/current/cdx" "$@"
''', encoding="utf-8")
            temporary_launcher.chmod(0o755)
            os.replace(temporary_launcher, launcher)
            atomic_json(settings_path, settings)
            atomic_json(root / 'update-request.json', {"operation": "execute_program", "arguments": {"program": sys.executable, "args": [str(python_updater), str(updater), "--root", str(root), "--automatic", "--startup-check"], "cwd": str(root), "timeout_ms": 180000, "output_limit_bytes": 262144, "expected_exit_codes": [0]}})
        if args.enable_auto_update and settings["enabled"] and not args.automatic and not args.check:
            try:
                if args.allow_test_http and not args.schedule_dry_run:
                    raise RuntimeError("Test HTTP endpoints cannot be registered for automatic updates")
                receipt["auto_update"] = systemd(root, args.source, "enable", args.schedule_dry_run)
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                receipt.update(auto_update="startup_fallback", schedule_registration="FAIL", auto_update_error=str(error))
        if not args.check:
            receipt.update(auto_update_enabled=settings["enabled"], checked_unix=time.time())
            atomic_json(attempt_path, receipt)
            saved = work / "receipt.json"
            saved.write_text(json.dumps(receipt, ensure_ascii=False) + "\n", encoding="utf-8")
            os.replace(saved, root / "install-receipt.json")
    return receipt, 0 if receipt["status"] == "PASS" else 1


def atomic_json(path, value):
    temporary = path.with_suffix(".new")
    temporary.write_text(json.dumps(value, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


try:
    result, code = main()
except InterruptedError as error:
    result, code = {"status": "UNKNOWN", "operation": "cancelled", "error": str(error), "config_updated": False}, 130
except (OSError, ValueError, RuntimeError, EOFError, subprocess.SubprocessError, tarfile.TarError) as error:
    result, code = {"status": "FAIL", "error": str(error), "config_updated": False}, 1
    if ATTEMPT_PATH is not None:
        atomic_json(ATTEMPT_PATH, dict(result, checked_unix=time.time()))
finally:
    if UPDATE_LOCK is not None:
        UPDATE_LOCK.close()
print(json.dumps(result, ensure_ascii=False))
sys.exit(code)
PY
