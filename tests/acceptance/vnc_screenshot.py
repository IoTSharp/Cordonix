"""Bounded real RFB/MCP fixtures; stdlib only, no remote hosts or real passwords."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
import uuid
import zlib

DEADLINE = time.monotonic() + 120
PIXELS = bytes.fromhex("ff000000ff000000ffffffff")  # RGB: red, green, blue, white
WIRE = bytes.fromhex("0000ff0000ff0000ff000000ffffff00")
# Independent OpenSSL DES-EDE3/ECB vector with the same eight-byte key repeated.
RESPONSE = bytes.fromhex("ee22539f33a5983ec12f9c2edbc995dd")
CHECKS = []


def check(name, condition):
    CHECKS.append({"name": name, "passed": bool(condition)})
    print(f"{name}: {'PASS' if condition else 'FAIL'}", flush=True)
    if not condition:
        raise AssertionError(name)


class Client:
    def __init__(self, executable):
        self.process = subprocess.Popen([str(executable), "--mcp"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        self.lines = queue.Queue(maxsize=256)
        self.sequence = 0
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        print(f"mcp_owner_pid={self.process.pid} parent_pid={os.getpid()} started_ns={time.time_ns()} command={self.process.args}", flush=True)

    def read(self):
        for _ in range(256):
            line = self.process.stdout.readline(131073)
            if not line:
                return
            self.lines.put_nowait(line)

    def send(self, method, params, notify=False):
        self.sequence += 1
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notify:
            message["id"] = self.sequence
        self.process.stdin.write((json.dumps(message, ensure_ascii=True) + "\n").encode())
        self.process.stdin.flush()
        return self.sequence

    def receive(self, identifier, timeout=8):
        until = min(DEADLINE, time.monotonic() + timeout)
        for _ in range(48):
            remaining = until - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("MCP fixture deadline")
            data = json.loads(self.lines.get(timeout=remaining))
            if data.get("id") == identifier:
                return data
        raise RuntimeError("MCP response limit")

    def call(self, arguments, cancel=False):
        identifier = self.send("tools/call", {"name": "vnc_screenshot", "arguments": arguments})
        if cancel:
            time.sleep(0.15)
            self.send("notifications/cancelled", {"requestId": identifier}, notify=True)
        reply = self.receive(identifier)
        assert "result" in reply, reply
        content = reply["result"]["content"][0]["text"]
        assert '"password"' not in content and '"secret"' not in content
        result = json.loads(content)
        if result["status"] in ("authentication_failed", "protocol_error", "too_large", "already_exists"):
            assert reply["result"].get("isError") is True, "Deterministic VNC failure must carry MCP isError"
        return result

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=2)
            raise AssertionError("MCP failed to shut down gracefully")
        finally:
            self.reader.join(timeout=2)
            self.process.stdout.close()
            self.process.stderr.close()
        assert not self.reader.is_alive()


class Fixture:
    def __init__(self, version=8, auth=1, mode="full", port=0, reject=False):
        self.version, self.auth, self.mode, self.reject = version, auth, mode, reject
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", port))
        self.listener.listen(1)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.ready = threading.Event()
        self.error = None
        self.selected = None
        self.received_response = None
        self.peer = None
        self.until = min(DEADLINE, time.monotonic() + 10)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def receive(self, length):
        data = bytearray()
        for _ in range(1024):
            if self.stop.is_set() or time.monotonic() >= self.until:
                raise TimeoutError("RFB fixture stopped")
            try:
                part = self.peer.recv(length - len(data))
            except socket.timeout:
                continue
            if not part:
                raise EOFError()
            data.extend(part)
            if len(data) == length:
                return bytes(data)
        raise TimeoutError("RFB fixture read limit")

    def send(self, data):
        # Deliberately fragment the protocol to exercise exact-byte reads.
        for i in range(0, len(data), 3):
            if self.stop.is_set() or time.monotonic() >= self.until:
                raise TimeoutError("RFB fixture send limit")
            self.peer.sendall(data[i:i + 3])

    def rectangle(self, x, y, w, h, data, encoding=0):
        self.send(b"\x00\x00\x00\x01" + struct.pack(">HHHHi", x, y, w, h, encoding) + data)

    def run(self):
        try:
            for _ in range(100):
                if self.stop.is_set() or time.monotonic() >= self.until:
                    return
                try:
                    self.peer, _ = self.listener.accept()
                    break
                except socket.timeout:
                    continue
            if self.peer is None:
                return
            self.peer.settimeout(0.1)
            self.peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            if self.mode == "stall":
                self.ready.set()
                self.receive(1)
                return
            self.send(f"RFB 003.{self.version:03}\n".encode())
            assert self.receive(12) == f"RFB 003.{self.version:03}\n".encode()
            if self.version == 3:
                self.send(struct.pack(">I", self.auth))
                self.selected = self.auth
            else:
                offered = bytes([1, 2]) if self.auth == 12 else bytes([self.auth])
                self.send(bytes([len(offered)]) + offered)
                self.selected = self.receive(1)[0]
            if self.selected == 2:
                self.send(bytes(range(16)))
                self.ready.set()
                self.received_response = self.receive(16)
                accepted = self.received_response == RESPONSE and not self.reject
                self.send(struct.pack(">I", 0 if accepted else 1))
                if not accepted:
                    return
            elif self.version == 8:
                self.send(b"\x00" * 4)
            assert self.receive(1) == b"\x01", "Shared flag must preserve existing viewers"
            width, height = (65535, 65535) if self.mode == "oversized" else (2, 2)
            self.send(struct.pack(">HH", width, height) + bytes(16) + struct.pack(">I", 4) + b"test")
            pixel_format = self.receive(20)
            assert pixel_format == bytes.fromhex("000000002018000100ff00ff00ff100800000000")
            assert self.receive(8) == bytes.fromhex("0200000100000000"), "Only Raw encoding"
            assert self.receive(10) == bytes.fromhex("03000000000000020002"), "Full non-incremental frame"
            if self.mode == "invalid_rect":
                self.rectangle(1, 0, 2, 2, b"")
            elif self.mode == "unsupported_encoding":
                self.rectangle(0, 0, 2, 2, b"", encoding=16)
            elif self.mode == "partial":
                self.rectangle(0, 0, 2, 1, WIRE[:8])
                self.receive(1)
            elif self.mode == "split":
                self.send(b"\x02")  # Bell
                self.send(b"\x03\x00\x00\x00\x00\x00\x00\x03abc")  # ignored server clipboard
                self.rectangle(0, 0, 2, 1, WIRE[:8])
                self.rectangle(0, 0, 2, 1, WIRE[:8])  # overlap must not count twice
                self.rectangle(0, 1, 2, 1, WIRE[8:])
            else:
                self.rectangle(0, 0, 2, 2, WIRE)
        except (EOFError, ConnectionError, TimeoutError, OSError):
            pass  # Early client rejection/cancellation closes its socket.
        except BaseException as error:
            self.error = str(error)
        finally:
            if self.peer is not None:
                self.peer.close()

    def close(self):
        self.stop.set()
        self.listener.close()
        self.thread.join(timeout=2)
        assert not self.thread.is_alive(), "Fixture thread leaked"
        assert self.error is None, self.error


class Credential(ctypes.Structure):
    _fields_ = [("Flags", wintypes.DWORD), ("Type", wintypes.DWORD),
                ("TargetName", wintypes.LPWSTR), ("Comment", wintypes.LPWSTR),
                ("LastWritten", wintypes.FILETIME), ("CredentialBlobSize", wintypes.DWORD),
                ("CredentialBlob", ctypes.POINTER(ctypes.c_ubyte)), ("Persist", wintypes.DWORD),
                ("AttributeCount", wintypes.DWORD), ("Attributes", ctypes.c_void_p),
                ("TargetAlias", wintypes.LPWSTR), ("UserName", wintypes.LPWSTR)]


def credential_blob(alias, delete=False):
    api = ctypes.WinDLL("advapi32", use_last_error=True)
    api.CredReadW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                             ctypes.POINTER(ctypes.POINTER(Credential))]
    api.CredFree.argtypes = [ctypes.c_void_p]
    api.CredDeleteW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD]
    if delete:
        assert alias.startswith("Cordonix/VNC/test/")
        return api.CredDeleteW(alias, 1, 0)
    pointer = ctypes.POINTER(Credential)()
    if not api.CredReadW(alias, 1, 0, ctypes.byref(pointer)):
        assert ctypes.get_last_error() == 1168
        return None
    try:
        entry = pointer.contents
        assert entry.Persist == 2  # Current user's credentials across logon sessions.
        return ctypes.string_at(entry.CredentialBlob, entry.CredentialBlobSize)
    finally:
        api.CredFree(pointer)


def png_pixels(path):
    png = path.read_bytes()
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed = 8, bytearray()
    width = height = channels = 0
    for _ in range(100):
        length = int.from_bytes(png[offset:offset + 4], "big")
        kind = png[offset + 4:offset + 8]
        payload = png[offset + 8:offset + 8 + length]
        assert zlib.crc32(kind + payload) == int.from_bytes(png[offset + 8 + length:offset + 12 + length], "big")
        offset += length + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            assert depth == 8 and color == 2 and compression == filtering == interlace == 0
            channels = 3
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    assert (width, height) == (2, 2)
    raw = zlib.decompress(compressed)
    previous, output = bytearray(width * channels), bytearray()
    for y in range(height):
        start = y * (width * channels + 1)
        mode = raw[start]
        row = bytearray(raw[start + 1:start + 1 + width * channels])
        for x in range(len(row)):
            left = row[x - channels] if x >= channels else 0
            up = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            p = left + up - upper_left
            distances = [abs(p - left), abs(p - up), abs(p - upper_left)]
            paeth = [left, up, upper_left][distances.index(min(distances))]
            predictor = [0, left, up, (left + up) // 2, paeth][mode]
            row[x] = (row[x] + predictor) & 255
        output.extend(row)
        previous = row
    return bytes(output)


def password_dialog(client, request, action):
    from ctypes import wintypes as wt
    api = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    api.EnumWindows.argtypes = [callback, wt.LPARAM]
    api.EnumChildWindows.argtypes = [wt.HWND, callback, wt.LPARAM]
    api.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    api.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
    api.GetWindowLongW.argtypes = [wt.HWND, ctypes.c_int]
    api.SendMessageTimeoutW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM, wt.UINT, wt.UINT, ctypes.POINTER(ctypes.c_size_t)]
    api.SendMessageTimeoutW.restype = ctypes.c_ssize_t
    api.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
    api.GetDlgItem.restype = wt.HWND

    class ProcessEntry(ctypes.Structure):
        _fields_ = [("size", wt.DWORD), ("usage", wt.DWORD), ("pid", wt.DWORD),
                    ("heap", ctypes.c_size_t), ("module", wt.DWORD), ("threads", wt.DWORD),
                    ("parent", wt.DWORD), ("priority", wt.LONG), ("flags", wt.DWORD),
                    ("exe", wt.WCHAR * 260)]

    kernel.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wt.HANDLE
    kernel.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    identifier = client.send("tools/call", {"name": "vnc_screenshot", "arguments": request})
    until = min(DEADLINE, time.monotonic() + 5)
    dialog = None
    for _ in range(100):
        if time.monotonic() >= until:
            break
        children = set()
        snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
        try:
            entry = ProcessEntry()
            entry.size = ctypes.sizeof(entry)
            valid = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
            for _ in range(10000):
                if not valid or time.monotonic() >= until:
                    break
                if entry.parent == client.process.pid:
                    children.add(entry.pid)
                valid = kernel.Process32NextW(snapshot, ctypes.byref(entry))
        finally:
            kernel.CloseHandle(snapshot)
        windows = []

        examined = 0

        @callback
        def visit(window, _):
            nonlocal examined
            examined += 1
            if examined > 4096 or time.monotonic() >= until:
                return False
            pid = wt.DWORD()
            api.GetWindowThreadProcessId(window, ctypes.byref(pid))
            if pid.value in children:
                windows.append(window)
            return len(windows) < 32

        api.EnumWindows(visit, 0)
        for window in windows:
            edits = []

            inspected = 0

            @callback
            def find_password(child, _):
                nonlocal inspected
                inspected += 1
                if inspected > 128 or time.monotonic() >= until:
                    return False
                name = ctypes.create_unicode_buffer(128)
                api.GetClassNameW(child, name, 128)
                if name.value == "Edit" and api.GetWindowLongW(child, -16) & 0x20:
                    edits.append(child)  # ES_PASSWORD
                return True

            api.EnumChildWindows(window, find_password, 0)
            if edits:
                dialog = (window, edits[0])
                break
        if dialog:
            break
        time.sleep(0.05)
    assert dialog, "No owned native masked password dialog found"
    message_result = ctypes.c_size_t()
    if action == "accept":
        value = ctypes.create_unicode_buffer("secret")
        assert api.SendMessageTimeoutW(dialog[1], 0x000C, 0, ctypes.addressof(value), 2, 500, ctypes.byref(message_result))
        button = api.GetDlgItem(dialog[0], 1)
        assert button, "No OK button in owned dialog"
        assert api.SendMessageTimeoutW(button, 0x00F5, 0, 0, 2, 500, ctypes.byref(message_result))
    elif action == "dismiss":
        button = api.GetDlgItem(dialog[0], 2)
        assert button
        assert api.SendMessageTimeoutW(button, 0x00F5, 0, 0, 2, 500, ctypes.byref(message_result))
    elif action == "cancel":
        client.send("notifications/cancelled", {"requestId": identifier}, notify=True)
    reply = client.receive(identifier)
    return json.loads(reply["result"]["content"][0]["text"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pilot", action="store_true")
    parser.add_argument("--ui", action="store_true", help="Exercise owned native dialogs on an interactive Windows desktop")
    parser.add_argument("--default-port", action="store_true", help="Requires an unused loopback port 5900")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="vnc-fixture-", dir=args.output.parent)).resolve()
    aliases = ["Cordonix/VNC/test/" + uuid.uuid4().hex for _ in range(2)]
    client = Client(args.executable.resolve())
    try:
        identifier = client.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "vnc-fixture", "version": "1"}})
        assert "result" in client.receive(identifier)
        identifier = client.send("tools/list", {})
        tools = client.receive(identifier)["result"]["tools"]
        schema = next(tool for tool in tools if tool["name"] == "vnc_screenshot")
        check("MCP VNC defaults and password contract", schema["inputSchema"]["properties"]["port"]["default"] == 5900 and schema["inputSchema"]["properties"]["password"]["writeOnly"])
        base = {"host": "127.0.0.1", "path": str(root / "远程 截图.png"), "prompt_for_password": False, "proxy": "direct", "timeout_ms": 1500}
        if os.name != "nt":
            check("Linux explicit platform boundary", client.call(base)["status"] == "not_supported")
            return

        def run(name, expected="ok", fields=None, **fixture_options):
            fixture = Fixture(**fixture_options)
            request = {**base, "path": str(root / f"{len(CHECKS)} 截图.png"), "port": fixture.port, **(fields or {})}
            try:
                result = client.call(request)
                check(name, result["status"] == expected and result["cleanup_complete"])
                if expected == "ok":
                    check(name + " PNG pixels", result["written_to_disk"] and png_pixels(Path(request["path"])) == PIXELS)
                else:
                    check(name + " no partial image", not result["written_to_disk"] and not Path(request["path"]).exists())
                return result, fixture
            finally:
                fixture.close()

        run("RFB 3.8 no password")
        if args.pilot:
            return
        if args.default_port:
            fixture = Fixture(port=5900)
            try:
                path = root / "default-port.png"
                result = client.call({**base, "path": str(path)})
                check("Omitted port connects to 5900", result["status"] == "ok" and result["credential"] == "Cordonix/VNC/127.0.0.1:5900")
                check("Default-port PNG pixels", png_pixels(path) == PIXELS)
            finally:
                fixture.close()
        if args.ui:
            for action, expected in (("accept", "ok"), ("dismiss", "cancelled"), ("cancel", "cancelled"), ("timeout", "timed_out")):
                credential_blob(aliases[1], delete=True)
                fixture = Fixture(auth=2)
                try:
                    path = root / f"dialog-{action}.png"
                    result = password_dialog(client, {**base, "path": str(path), "port": fixture.port,
                                                      "credential": aliases[1], "prompt_for_password": True,
                                                      "timeout_ms": 2500}, action)
                    check("Native masked dialog " + action, result["status"] == expected and result["cleanup_complete"])
                    check("Native dialog credential " + action, (credential_blob(aliases[1]) == "secret".encode("utf-16-le")) == (action == "accept"))
                    if action == "accept":
                        check("Native dialog screenshot pixels", png_pixels(path) == PIXELS)
                    else:
                        check("Native dialog no image " + action, not path.exists())
                finally:
                    fixture.close()
            credential_blob(aliases[1], delete=True)
        run("RFB 3.3 no password", version=3)
        run("RFB 3.7 no password", version=7)
        _, fixture = run("None preferred without supplied password", auth=12)
        check("None was selected", fixture.selected == 1)
        run("Fragmented split frame with overlapping rectangles", mode="split")
        for version in (3, 7, 8):
            result, fixture = run(f"RFB 3.{version} supplied password", version=version, auth=2,
                                  fields={"password": "secret", "credential": aliases[0]})
            check(f"RFB 3.{version} independent DES response", fixture.received_response == RESPONSE)
            check(f"RFB 3.{version} current-user credential saved", result["credential_status"] == "saved" and credential_blob(aliases[0]) == "secret".encode("utf-16-le"))
        result, _ = run("Saved password reused", auth=2, fields={"credential": aliases[0]})
        check("Reused credential receipt", result["credential_status"] == "reused")
        run("Missing password asks user", expected="credential_required", auth=2, fields={"credential": aliases[1]})
        run("Wrong password rejected", expected="authentication_failed", auth=2, fields={"password": "incorrect", "credential": aliases[1]})
        check("Rejected password was not stored", credential_blob(aliases[1]) is None)
        run("Existing credential preserved on failed replacement", expected="authentication_failed", auth=2, fields={"password": "incorrect", "credential": aliases[0]})
        check("Original credential retained", credential_blob(aliases[0]) == "secret".encode("utf-16-le"))
        run("Unsupported auth", expected="not_supported", auth=19)
        run("Oversized framebuffer", expected="too_large", mode="oversized")
        run("Rectangle outside framebuffer", expected="protocol_error", mode="invalid_rect")
        run("Unrequested encoding", expected="not_supported", mode="unsupported_encoding")
        run("No false success on incomplete frame", expected="timed_out", mode="partial", fields={"timeout_ms": 300})
        run("Network timeout", expected="timed_out", mode="stall", fields={"timeout_ms": 150})
        fixture = Fixture(mode="stall")
        try:
            result = client.call({**base, "port": fixture.port, "timeout_ms": 5000}, cancel=True)
            if result["status"] != "cancelled":
                print(json.dumps(result), flush=True)
            check("MCP asynchronous cancellation", result["status"] == "cancelled" and result["cleanup_complete"] and not result["written_to_disk"])
        finally:
            fixture.close()
        existing = root / "existing.png"
        existing.write_bytes(b"preserve")
        result = client.call({**base, "path": str(existing)})
        check("Existing file preserved before connection", result["status"] == "already_exists" and existing.read_bytes() == b"preserve")
        for fields in ({"host": "localhost"}, {"port": 0}, {"path": "relative.png"}, {"timeout_ms": 0}, {"password": 123}):
            check("Invalid input rejected " + next(iter(fields)), client.call({**base, **fields})["status"] == "validation_error")
    finally:
        client.close()
        if os.name == "nt":
            for alias in aliases:
                credential_blob(alias, delete=True)
                assert credential_blob(alias) is None
        assert root.parent == args.output.parent.resolve() and root.name.startswith("vnc-fixture-")
        shutil.rmtree(root)
        args.output.write_text(json.dumps({"passed": sum(c["passed"] for c in CHECKS), "total": len(CHECKS), "checks": CHECKS,
                                          "credential_cleanup": True, "fixture_cleanup": True}, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
