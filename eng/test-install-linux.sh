#!/bin/sh
set -eu
command -v python3 >/dev/null 2>&1 || { echo 'Python 3 is required for the isolated installer gate.' >&2; exit 2; }
command -v timeout >/dev/null 2>&1 || { echo 'GNU timeout is required for the isolated installer gate.' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec timeout --kill-after=5s 180s python3 - "$root" <<'PY'
import hashlib
import fcntl
import http.server
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time

source = Path(sys.argv[1])
installer = source / "install/install.sh"
deadline = time.monotonic() + 165
checks = 0
requests = []
routes = {}
stop = threading.Event()


def run(arguments, **options):
    remaining = deadline - time.monotonic()
    if remaining < 1:
        raise RuntimeError("Installer test deadline exceeded")
    return subprocess.run(arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=min(30, remaining), **options)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        if self.path == "/slow":
            stop.wait(3)
        status, data = routes.get(self.path, (404, b"missing"))
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except BrokenPipeError:
            pass

    def log_message(self, *arguments):
        pass


def serve(server):
    for _ in range(100):
        if stop.is_set() or time.monotonic() >= deadline:
            return
        server.handle_request()


def check(name, valid):
    global checks
    if not valid:
        raise AssertionError(name)
    checks += 1
    print("PASS " + name, flush=True)


def package(binary, version, traversal=False, truncated=False):
    name = "cordonix-" + version + "-linux-x64.tar.gz"
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as archive:
        member = tarfile.TarInfo("../escaped.txt" if traversal else "cdx")
        body = b"escaped" if traversal else binary.read_bytes()
        member.mode = 0o755
        member.size = len(body)
        archive.addfile(member, io.BytesIO(body))
        if not traversal:
            updater = installer.read_text(encoding="utf-8").replace("# Cordonix updater generation: 3", "# Cordonix updater generation: 4").encode("utf-8")
            member = tarfile.TarInfo("install/install.sh")
            member.mode = 0o644
            member.size = len(updater)
            archive.addfile(member, io.BytesIO(updater))
    payload = data.getvalue()
    if truncated:
        payload = payload[:len(payload) // 2]
    routes["/assets/" + name] = (200, payload)
    routes["/assets/" + name + ".sha256"] = (200, (hashlib.sha256(payload).hexdigest() + "  " + name + "\n").encode("ascii"))
    return {"tag_name": "v" + version, "assets": [{"name": item, "browser_download_url": base + "/assets/" + item} for item in (name, name + ".sha256")]}


with tempfile.TemporaryDirectory(prefix="installer-linux-", dir=source / "build") as temporary:
    task = Path(temporary)
    root = task / "含 空格 的安装目录"
    fixture = task / "version.c"
    fixture.write_text('#include <stdio.h>\n#include <string.h>\n#ifndef VERSION\n#define VERSION "0.0.0"\n#endif\nint main(int argc, char **argv) { if (argc == 2 && !strcmp(argv[1], "--version")) { puts("Cordonix cdx " VERSION); return 0; } if(argc == 3 && !strcmp(argv[1], "--request")) { puts("{\\"status\\":\\"ok\\",\\"name\\":\\"Cordonix\\",\\"version\\":\\"" VERSION "\\",\\"platform\\":\\"linux\\",\\"runtime\\":\\"C11\\"}"); return 0; } return 2; }\n', encoding="utf-8")
    binaries = {}
    for version in ("0.0.1", "0.0.2"):
        binary = task / ("cdx-" + version)
        result = run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror", '-DVERSION="' + version + '"', str(fixture), "-o", str(binary)])
        if result.returncode:
            raise RuntimeError(result.stderr)
        binaries[version] = binary
    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    server.timeout = 0.2
    base = "http://127.0.0.1:" + str(server.server_port)
    thread = threading.Thread(target=serve, args=(server,), daemon=True)
    thread.start()
    try:
        def invoke(*arguments, succeeds=True):
            result = run(["sh", str(installer), "--root", str(root), "--github-api", base + "/github", "--gitee-api", base + "/gitee", "--allow-test-http", "--schedule-dry-run", *arguments])
            if succeeds != (result.returncode == 0):
                raise AssertionError(result.stdout + "\n" + result.stderr)
            return json.loads(result.stdout)

        def clear_attempt(directory=root):
            for name in ("last-update-check.json", "last-update-launch.json"):
                (directory / name).unlink(missing_ok=True)

        def wait_upgrade(directory=root):
            audit_path = directory / "last-update-launch.json"
            if not audit_path.exists():
                return
            audit = json.loads(audit_path.read_text(encoding="utf-8"))
            for _ in range(200):
                if time.monotonic() >= deadline:
                    break
                stat = Path("/proc") / str(audit["pid"]) / "stat"
                try:
                    fields = stat.read_text().rsplit(") ", 1)[1].split()
                except (FileNotFoundError, ProcessLookupError):
                    return
                if fields[0] == "Z" or fields[19] != audit["start_ticks"]:
                    return
                time.sleep(0.05)
            raise TimeoutError("Background upgrade worker did not finish")

        def launch(directory=root, wait=True):
            result = run([str(directory / "launch.sh"), "--version"])
            if wait:
                wait_upgrade(directory)
            return result

        release1 = package(binaries["0.0.1"], "0.0.1")
        routes["/github"] = (503, b"unavailable")
        routes["/gitee"] = (200, json.dumps([release1]).encode("utf-8"))
        receipt = invoke()
        check("GitHub HTTP failure falls back to Gitee and installs a verified runtime", receipt["source"] == "gitee" and receipt["installed_version"] == "0.0.1" and receipt["operation"] == "install")
        check("Unicode spaced install path produces a working stable entry", run([str(root / "current/cdx"), "--version"]).stdout.strip() == "Cordonix cdx 0.0.1")
        check("Default installation enables daily startup updates without requiring a timer", receipt["auto_update_enabled"] and receipt["auto_update"] == "daily_startup" and receipt["command"] == str(root / "launch.sh"))
        check("A verified package refreshes the updater generation", "# Cordonix updater generation: 4" in (root / "updater/install.sh").read_text(encoding="utf-8"))

        before = len(requests)
        receipt = invoke("--source", "gitee")
        check("Same version is a no-op without asset downloads", receipt["operation"] == "up_to_date" and requests[before:] == ["/gitee"])
        check("An older installer cannot downgrade the updater generation", "# Cordonix updater generation: 4" in (root / "updater/install.sh").read_text(encoding="utf-8"))
        release2 = package(binaries["0.0.2"], "0.0.2")
        routes["/gitee"] = (200, json.dumps([release1, {"tag_name": "v9.0.0-rc1"}, release2]).encode("utf-8"))
        settings_path = root / "update-settings.json"
        settings = json.loads(settings_path.read_text(encoding="utf-8"))
        settings["source"] = "auto"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")
        clear_attempt()
        launched = launch()
        receipt = json.loads((root / "last-update-check.json").read_text(encoding="utf-8"))
        check("Startup keeps running while a separate instance updates through Gitee", launched.returncode == 0 and launched.stdout.strip() == "Cordonix cdx 0.0.1" and not launched.stderr and receipt["operation"] == "update" and receipt["source"] == "gitee" and (root / "versions/0.0.1/cdx").exists())
        before = len(requests)
        launch()
        check("Repeated startups on the same day make no network requests", len(requests) == before)
        (root / "last-update-check.json").write_text(json.dumps({"status": "PASS", "checked_unix": time.time() - 86400}))
        (root / "last-update-launch.json").unlink()
        launch()
        today = json.loads((root / "last-update-check.json").read_text())
        check("The first startup on the next calendar day checks again", time.localtime(today["checked_unix"])[:3] == time.localtime()[:3])
        before = len(requests)
        clear_attempt()
        with (root / ".update.lock").open("a") as held:
            fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            launched = launch()
            check("An active updater does not block startup or cause a second mutation", launched.returncode == 0 and len(requests) == before and not (root / "last-update-check.json").exists())
        routes["/gitee"] = (503, b"offline")
        clear_attempt()
        launched = launch()
        failed = json.loads((root / "last-update-check.json").read_text(encoding="utf-8"))
        check("Both sources offline still launch the installed runtime and retain a failure receipt", launched.returncode == 0 and launched.stdout.strip() == "Cordonix cdx 0.0.2" and failed["status"] == "FAIL")
        routes["/gitee"] = (200, json.dumps([release2]).encode("utf-8"))
        scheduled = run(["sh", str(root / "updater/install.sh"), "--root", str(root), "--automatic"])
        check("A recent startup failure cannot suppress the full scheduled check", scheduled.returncode == 0 and json.loads(scheduled.stdout)["source"] == "gitee")
        settings["github_api"] = base + "/slow"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")
        clear_attempt()
        started = time.monotonic()
        launched = launch(wait=False)
        elapsed = time.monotonic() - started
        wait_upgrade()
        slow = json.loads((root / "last-update-check.json").read_text(encoding="utf-8"))
        check("Slow GitHub does not block launch and the background instance falls back to Gitee", launched.returncode == 0 and elapsed < 2.5 and slow["status"] == "PASS" and slow["source"] == "gitee")
        settings["github_api"] = base + "/github"
        settings_path.write_text(json.dumps(settings), encoding="utf-8")
        unregistered = run(["sh", str(installer), "--root", str(root), "--source", "gitee", "--gitee-api", base + "/gitee", "--allow-test-http", "--enable-auto-update"])
        registration_failure = json.loads(unregistered.stdout)
        check("Schedule failure retains the working install and startup fallback", unregistered.returncode == 0 and registration_failure["status"] == "PASS" and registration_failure["auto_update"] == "startup_fallback" and registration_failure["schedule_registration"] == "FAIL")
        previous = (root / "current").readlink()

        routes["/gitee"] = (200, json.dumps([release1]).encode("utf-8"))
        before = len(requests)
        receipt = invoke("--source", "gitee")
        check("Older release never downgrades or downloads", receipt["operation"] == "older_release" and requests[before:] == ["/gitee"] and (root / "current").readlink() == previous)

        release3 = package(binaries["0.0.2"], "0.0.3")
        routes["/gitee"] = (200, json.dumps([release3]).encode("utf-8"))
        routes["/assets/cordonix-0.0.3-linux-x64.tar.gz.sha256"] = (200, ("0" * 64 + "  cordonix-0.0.3-linux-x64.tar.gz\n").encode("ascii"))
        receipt = invoke("--source", "gitee", succeeds=False)
        check("Checksum mismatch preserves the current version", receipt["status"] == "FAIL" and (root / "current").readlink() == previous and not (root / "versions/0.0.3").exists())

        release3 = package(binaries["0.0.1"], "0.0.3")
        routes["/gitee"] = (200, json.dumps([release3]).encode("utf-8"))
        receipt = invoke("--source", "gitee", succeeds=False)
        check("Executable version mismatch fails before switching current", receipt["status"] == "FAIL" and (root / "current").readlink() == previous)

        release3 = package(binaries["0.0.2"], "0.0.3", truncated=True)
        routes["/gitee"] = (200, json.dumps([release3]).encode("utf-8"))
        receipt = invoke("--source", "gitee", succeeds=False)
        check("Truncated archive with a matching sidecar preserves current", receipt["status"] == "FAIL" and (root / "current").readlink() == previous)

        release3 = package(binaries["0.0.2"], "0.0.3", traversal=True)
        routes["/gitee"] = (200, json.dumps([release3]).encode("utf-8"))
        receipt = invoke("--source", "gitee", succeeds=False)
        check("Archive traversal is rejected before extraction", receipt["status"] == "FAIL" and not (root / "escaped.txt").exists() and (root / "current").readlink() == previous)

        routes["/github"] = (200, json.dumps({"tag_name": "v0.0.9", "assets": []}).encode("utf-8"))
        routes["/gitee"] = (200, json.dumps([release2]).encode("utf-8"))
        receipt = invoke()
        check("Missing exact assets trigger the bounded fallback", receipt["source"] == "gitee" and len(receipt["fallback_reasons"]) == 1)

        receipt = invoke("--source", "gitee", "--enable-auto-update", "--schedule-dry-run")
        service = next((root / "systemd").glob("*.service")).read_text(encoding="utf-8")
        timer = next((root / "systemd").glob("*.timer")).read_text(encoding="utf-8")
        check("Daily user timer and bounded service definitions are generated without registration", receipt["auto_update"] == "definition_only_NOT_RUN" and "OnUnitActiveSec=1d" in timer and "TimeoutStartSec=190" in service and "--root" in service and (root / "updater/install.sh").exists())
        if shutil.which("systemd-analyze"):
            units = [str(path) for path in (root / "systemd").glob("cordonix-update-*")]
            result = run(["systemd-analyze", "--user", "verify", *units])
            check("The real systemd parser accepts the generated service and timer", result.returncode == 0)
        receipt = invoke("--source", "gitee", "--disable-auto-update", "--schedule-dry-run")
        check("Disabling the isolated schedule removes only its owned unit definitions", receipt["auto_update"] == "disabled" and not list((root / "systemd").glob("cordonix-update-*")))
        before = len(requests)
        (root / "last-update-check.json").unlink()
        launched = run([str(root / "launch.sh"), "--version"])
        check("Disabling updates prevents startup network access", launched.returncode == 0 and len(requests) == before and not (root / "last-update-check.json").exists())
        receipt = invoke("--source", "gitee")
        check("Reinstalling preserves the explicitly disabled preference", not receipt["auto_update_enabled"] and receipt["auto_update"] == "disabled")
        native_root = task / "native-updater"
        native = source / "build/linux/bin/cdx"
        native_version = run([str(native), "--version"]).stdout.strip().split()[-1]
        native_release = package(native, native_version)
        routes["/gitee"] = (200, json.dumps([native_release]).encode("utf-8"))
        installed = run(["sh", str(installer), "--root", str(native_root), "--github-api", base + "/slow", "--gitee-api", base + "/gitee", "--allow-test-http", "--schedule-dry-run"])
        check("The native Linux runtime installs with its upgrade capability", installed.returncode == 0 and json.loads((native_root / "update-settings.json").read_text())["supports_checkupgrade"])
        clear_attempt(native_root)
        started = time.monotonic()
        launched = launch(native_root, wait=False)
        elapsed = time.monotonic() - started
        wait_upgrade(native_root)
        native_audit = json.loads((native_root / "last-update-launch.json").read_text())
        if not (native_root / "last-update-check.json").exists():
            raise AssertionError((native_root / "upgrade-worker.stdout.json").read_text() + (native_root / "upgrade-worker.stderr.log").read_text())
        native_receipt = json.loads((native_root / "last-update-check.json").read_text())
        check("Real native checkupgrade runs separately without blocking the main runtime", launched.returncode == 0 and elapsed < 2.5 and native_audit["arguments"][1] == "--checkupgrade" and native_receipt["source"] == "gitee")
        command = run([str(native_root / "current/cdx"), "--checkupgrade"])
        check("Native checkupgrade uses the bounded process core and confirms cleanup", command.returncode == 0 and json.loads(command.stdout)["cleanup_complete"])
        check("Disposable downloads and extraction directories are reclaimed", not list(root.glob(".install-*")))
    finally:
        for directory in (root, task / "native-updater"):
            audit_path = directory / "last-update-launch.json"
            if audit_path.exists():
                audit = json.loads(audit_path.read_text())
                stat = Path("/proc") / str(audit["pid"]) / "stat"
                try:
                    fields = stat.read_text().rsplit(") ", 1)[1].split()
                    if fields[0] != "Z" and fields[19] == audit["start_ticks"]:
                        os.killpg(audit["pid"], signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except FileNotFoundError:
                    pass
        stop.set()
        thread.join(timeout=1)
        server.server_close()
print(json.dumps({"status": "PASS", "checks": checks, "requests": len(requests), "auto_update_registration": "NOT_RUN", "cleanup_complete": True}))
PY
