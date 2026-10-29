#!/bin/sh
set -eu
command -v python3 >/dev/null 2>&1 || { echo 'Python 3 is required for the isolated installer gate.' >&2; exit 2; }
command -v timeout >/dev/null 2>&1 || { echo 'GNU timeout is required for the isolated installer gate.' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec timeout --kill-after=5s 180s python3 - "$root" <<'PY'
import hashlib
import http.server
import io
import json
import os
from pathlib import Path
import shutil
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
        status, data = routes.get(self.path, (404, b"missing"))
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

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
            result = run(["sh", str(installer), "--root", str(root), "--github-api", base + "/github", "--gitee-api", base + "/gitee", "--allow-test-http", *arguments])
            if succeeds != (result.returncode == 0):
                raise AssertionError(result.stdout + "\n" + result.stderr)
            return json.loads(result.stdout)

        release1 = package(binaries["0.0.1"], "0.0.1")
        routes["/github"] = (503, b"unavailable")
        routes["/gitee"] = (200, json.dumps([release1]).encode("utf-8"))
        receipt = invoke()
        check("GitHub HTTP failure falls back to Gitee and installs a verified runtime", receipt["source"] == "gitee" and receipt["installed_version"] == "0.0.1" and receipt["operation"] == "install")
        check("Unicode spaced install path produces a working stable entry", run([str(root / "current/cdx"), "--version"]).stdout.strip() == "Cordonix cdx 0.0.1")

        before = len(requests)
        receipt = invoke("--source", "gitee")
        check("Same version is a no-op without asset downloads", receipt["operation"] == "up_to_date" and requests[before:] == ["/gitee"])
        release2 = package(binaries["0.0.2"], "0.0.2")
        routes["/gitee"] = (200, json.dumps([release1, {"tag_name": "v9.0.0-rc1"}, release2]).encode("utf-8"))
        receipt = invoke("--source", "gitee")
        check("Highest stable version updates and retains the old runtime", receipt["installed_version"] == "0.0.2" and (root / "versions/0.0.1/cdx").exists())
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
        check("Hourly user timer and bounded service definitions are generated without registration", receipt["auto_update"] == "definition_only_NOT_RUN" and "OnUnitActiveSec=1h" in timer and "TimeoutStartSec=190" in service and "--root" in service and (root / "updater/install.sh").exists())
        if shutil.which("systemd-analyze"):
            units = [str(path) for path in (root / "systemd").glob("cordonix-update-*")]
            result = run(["systemd-analyze", "--user", "verify", *units])
            check("The real systemd parser accepts the generated service and timer", result.returncode == 0)
        receipt = invoke("--source", "gitee", "--disable-auto-update", "--schedule-dry-run")
        check("Disabling the isolated schedule removes only its owned unit definitions", receipt["auto_update"] == "disabled" and not list((root / "systemd").glob("cordonix-update-*")))
        check("Disposable downloads and extraction directories are reclaimed", not list(root.glob(".install-*")))
    finally:
        stop.set()
        thread.join(timeout=1)
        server.server_close()
print(json.dumps({"status": "PASS", "checks": checks, "requests": len(requests), "auto_update_registration": "NOT_RUN", "cleanup_complete": True}))
PY
