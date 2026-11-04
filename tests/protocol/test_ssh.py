"""Real loopback SSH protocol acceptance; Paramiko is a test-server dependency only."""
import argparse
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import select
import socket
import subprocess
import tempfile
import threading
import time
import uuid

import paramiko

DEADLINE = time.monotonic() + 150
STOP = threading.Event()
PASSWORD = "fixture-密码-$()-" + uuid.uuid4().hex
KEY = paramiko.RSAKey.generate(2048)
FINGERPRINT = hashlib.sha256(KEY.asbytes()).hexdigest()
THREADS = []
TRANSPORTS = []
PROCESSES = []
EVENTS = []


def budget():
    if STOP.is_set() or time.monotonic() >= DEADLINE:
        raise TimeoutError("SSH fixture deadline/cancellation")


def worker(function, *args):
    if len(THREADS) >= 256:
        raise RuntimeError("fixture thread bound")
    def run():
        try:
            function(*args)
        except (EOFError, OSError, TimeoutError, paramiko.SSHException):
            pass
    thread = threading.Thread(target=run, daemon=True)
    THREADS.append(thread)
    thread.start()
    return thread


class Credential(ctypes.Structure):
    _fields_ = [("Flags", wintypes.DWORD), ("Type", wintypes.DWORD),
                ("TargetName", wintypes.LPWSTR), ("Comment", wintypes.LPWSTR),
                ("LastWritten", wintypes.FILETIME), ("CredentialBlobSize", wintypes.DWORD),
                ("CredentialBlob", ctypes.POINTER(ctypes.c_ubyte)), ("Persist", wintypes.DWORD),
                ("AttributeCount", wintypes.DWORD), ("Attributes", ctypes.c_void_p),
                ("TargetAlias", wintypes.LPWSTR), ("UserName", wintypes.LPWSTR)]


VAULT = ctypes.WinDLL("advapi32", use_last_error=True)
VAULT.CredWriteW.argtypes = [ctypes.POINTER(Credential), wintypes.DWORD]
VAULT.CredDeleteW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD]


def store(alias, user, secret):
    blob = secret.encode("utf-16-le")
    data = (ctypes.c_ubyte * len(blob)).from_buffer_copy(blob)
    value = Credential(Type=1, TargetName=alias, CredentialBlobSize=len(blob),
                       CredentialBlob=data, Persist=1, UserName=user)
    if not VAULT.CredWriteW(ctypes.byref(value), 0):
        raise ctypes.WinError(ctypes.get_last_error())


def shell(script, cwd):
    budget()
    process = subprocess.Popen([r"C:\Program Files\Git\bin\bash.exe", "--noprofile", "--norc", "-s"],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               cwd=cwd, creationflags=subprocess.CREATE_NO_WINDOW)
    PROCESSES.append(process)
    try:
        stdout, stderr = process.communicate(script, timeout=min(8, DEADLINE - time.monotonic()))
        return stdout, stderr, process.returncode
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate(timeout=2)
        raise


def receive_line(channel):
    data = bytearray()
    for _ in range(5000):
        budget()
        if len(data) > 70000:
            raise RuntimeError("fixture input bound")
        if channel.recv_ready():
            ch = channel.recv(1)
            if not ch:
                raise EOFError()
            if ch in (b"\n", b"\r"):
                return data.decode("utf-8")
            data.extend(ch)
        elif channel.closed:
            raise EOFError()
        else:
            STOP.wait(.01)
    raise TimeoutError("fixture line bound")


class Server(paramiko.ServerInterface):
    def __init__(self):
        self.forward = {}
        self.requests = {}

    def check_auth_password(self, username, password):
        EVENTS.append(("auth", username))
        return paramiko.AUTH_SUCCESSFUL if username == "fixture" and password == PASSWORD else paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        EVENTS.append(("key_auth", username))
        return paramiko.AUTH_SUCCESSFUL if username == "fixture" and key == KEY else paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return "password,publickey"

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_direct_tcpip_request(self, chanid, origin, destination):
        if destination[0] != "127.0.0.1":
            return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
        self.forward[chanid] = destination
        EVENTS.append(("forward", destination))
        return paramiko.OPEN_SUCCEEDED

    def check_channel_pty_request(self, channel, term, width, height, pxwidth, pxheight, modes):
        return True

    def check_channel_exec_request(self, channel, command):
        self.requests[channel.get_id()] = ("exec", command)
        return command == b"bash -s"

    def check_channel_shell_request(self, channel):
        self.requests[channel.get_id()] = ("shell", b"")
        return True


def forward(channel, destination):
    peer = socket.create_connection(destination, timeout=2)
    try:
        for _ in range(15000):
            budget()
            ready, _, _ = select.select([channel, peer], [], [], .01)
            for source in ready:
                data = source.recv(32768)
                if not data:
                    return
                (peer if source is channel else channel).sendall(data)
    finally:
        peer.close()
        channel.close()


def exec_request(channel, cwd):
    script = bytearray()
    for _ in range(15000):
        budget()
        if channel.recv_ready():
            script.extend(channel.recv(4096))
            if len(script) > 70000:
                raise RuntimeError("fixture script bound")
        elif channel.eof_received:
            break
        else:
            STOP.wait(.01)
    else:
        raise TimeoutError("fixture script deadline")
    EVENTS.append(("script", bytes(script)))
    out, err, code = shell(bytes(script), cwd)
    if out:
        channel.sendall(out)
    if err:
        channel.sendall_stderr(err)
    channel.send_exit_status(code)
    channel.close()


def interactive_request(channel, cwd):
    channel.sendall(b"\x1b[32mGeneric jump menu\x1b[0m\r\nmenu> ")
    selection = receive_line(channel)
    EVENTS.append(("selection", selection))
    if selection != ":connect fixture@127.0.0.1:2222":
        channel.close()
        return
    channel.sendall(b"Password:")
    if receive_line(channel) != PASSWORD:
        channel.close()
        return
    channel.sendall(b"fixture@asset$ ")
    line = receive_line(channel)
    if line == "su - root":
        channel.sendall(b"Password:")
        if receive_line(channel) != PASSWORD:
            channel.close()
            return
        channel.sendall(b"root@asset# ")
        line = receive_line(channel)
    token = re.search(r"'CDX_READY_' '([a-f0-9]{32})'", line)
    if not token:
        channel.close()
        return
    token = token.group(1)
    channel.sendall(("\r\nCDX_READY_" + token + "\r\n").encode())
    wrapper = []
    for _ in range(3000):
        budget()
        line = receive_line(channel)
        wrapper.append(line)
        if line.startswith("cdx_result=$?;"):
            break
    else:
        raise RuntimeError("fixture heredoc bound")
    out, err, code = shell(("\n".join(wrapper) + "\n").encode(), cwd)
    EVENTS.append(("interactive_wrapper", "\n".join(wrapper)))
    channel.sendall(out.replace(b"\n", b"\r\n") + err)
    channel.send_exit_status(code)
    channel.close()


def connection(sock, cwd):
    transport = paramiko.Transport(sock)
    TRANSPORTS.append(transport)
    transport.add_server_key(KEY)
    interface = Server()
    try:
        transport.start_server(server=interface)
        for _ in range(16):
            budget()
            channel = transport.accept(timeout=.2)
            if channel is None:
                if not transport.is_active():
                    return
                continue
            if channel.get_id() in interface.forward:
                worker(forward, channel, interface.forward[channel.get_id()])
                continue
            for _ in range(500):
                budget()
                request = interface.requests.get(channel.get_id())
                if request:
                    worker(exec_request if request[0] == "exec" else interactive_request, channel, cwd)
                    break
                STOP.wait(.01)
        for _ in range(15000):
            budget()
            if not transport.is_active():
                return
            STOP.wait(.01)
    finally:
        transport.close()


def listen(listener, cwd):
    listener.settimeout(.2)
    for _ in range(256):
        budget()
        try:
            sock, _ = listener.accept()
        except socket.timeout:
            continue
        worker(connection, sock, cwd)


class Client:
    def __init__(self, binary):
        self.process = subprocess.Popen([str(binary), "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        PROCESSES.append(self.process)
        self.lines = queue.Queue()
        def read():
            for _ in range(256):
                line = self.process.stdout.readline(2**21)
                if not line:
                    return
                self.lines.put(json.loads(line))
        worker(read)
        self.sequence = 1
        self.send({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {}})
        self.response()

    def send(self, request):
        self.process.stdin.write((json.dumps(request, ensure_ascii=False) + "\n").encode())
        self.process.stdin.flush()

    def response(self):
        budget()
        return self.lines.get(timeout=min(15, DEADLINE - time.monotonic()))

    def call(self, arguments, cancel=False):
        self.sequence += 1
        self.send({"jsonrpc": "2.0", "id": self.sequence, "method": "tools/call",
                   "params": {"name": "ssh", "arguments": arguments}})
        if cancel:
            time.sleep(.2)
            self.send({"jsonrpc": "2.0", "method": "notifications/cancelled",
                       "params": {"requestId": self.sequence}})
        response = self.response()
        if "error" in response:
            raise AssertionError(response)
        return json.loads(response["result"]["content"][0]["text"])

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(timeout=5)
        assert self.process.returncode == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--pilot", action="store_true")
    options = parser.parse_args()
    results = []
    aliases = ["cordonix-ssh-fixture-" + uuid.uuid4().hex + suffix for suffix in ("", "-wrong", "-root")]
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(16)
    port = listener.getsockname()[1]
    client = None
    def check(name, condition, value=None):
        results.append({"name": name, "passed": bool(condition)})
        print(("PASS " if condition else "FAIL ") + name, flush=True)
        if not condition:
            raise AssertionError(name + ": " + repr(value))
    with tempfile.TemporaryDirectory(prefix="cordonix-ssh-fixture-") as cwd:
        try:
            store(aliases[0], "fixture", PASSWORD)
            store(aliases[1], "fixture", "wrong-secret")
            store(aliases[2], "root", PASSWORD)
            worker(listen, listener, cwd)
            client = Client(options.binary.resolve())
            endpoint = {"host": "127.0.0.1", "port": port, "credential": aliases[0]}
            probe = client.call({"target": endpoint})
            check("native SSH probe observes the actual server key before authentication",
                  probe["status"] == "completed" and probe["host_key_sha256"] == FINGERPRINT and not EVENTS, probe)
            target = dict(endpoint, host_key_sha256=FINGERPRINT)
            script = "printf '%s\\n' 'two words' '中文' '$(literal)' 'a\"b'; printf 'err\\n' >&2; exit 7\n"
            direct = {"action": "execute", "target": target, "script": script}
            result = client.call(direct)
            check("native password authentication, literal script, Unicode, stderr and real exit status",
                  result["status"] == "failed" and result["exit_code"] == 7 and result["stdout"] == 'two words\n中文\n$(literal)\na"b\n'
                  and result["stderr"] == "err\n" and result["remote_outcome"] == "known" and result["target_ssh_verified"] and result["cleanup_complete"], result)
            check("remote script bytes arrive unchanged", ("script", script.encode()) in EVENTS)
            if not options.pilot:
                before = len([e for e in EVENTS if e[0] == "auth"])
                result = client.call(dict(direct, target=dict(target, host_key_sha256="0" * 64)))
                check("host-key mismatch stops before authentication or command dispatch",
                      result["status"] == "host_key_mismatch" and result["remote_outcome"] == "not_run" and
                      len([e for e in EVENTS if e[0] == "auth"]) == before, result)
                result = client.call(dict(direct, target=dict(endpoint)))
                check("execute requires a pinned host key", result["status"] == "host_key_required" and not result["remote_command_started"], result)
                result = client.call(dict(direct, target=dict(target, credential=aliases[1])))
                check("authentication failure is explicit", result["status"] == "authentication_failed" and result["remote_outcome"] == "not_run", result)
                result = client.call(dict(direct, target=dict(target, credential="missing-" + uuid.uuid4().hex)))
                check("missing credential is explicit", result["status"] == "credential_error", result)
                key_path = Path(cwd) / "中文 key.pem"
                KEY.write_private_key_file(str(key_path))
                result = client.call(dict(direct, target={"host": "127.0.0.1", "port": port, "user": "fixture",
                                                         "host_key_sha256": FINGERPRINT, "private_key_path": str(key_path)}))
                check("native CNG key authentication reads a Unicode private-key path", result["exit_code"] == 7 and result["status"] == "failed", result)
                result = client.call(dict(direct, route="jump", bastion=target))
                check("native SSH direct-tcpip carries an independently authenticated second SSH session",
                      result["exit_code"] == 7 and result["host_key_sha256"] == FINGERPRINT and
                      result["bastion_host_key_sha256"] == FINGERPRINT and any(e[0] == "forward" for e in EVENTS), result)
                result = client.call(dict(direct, route="jump", bastion=dict(target, host_key_sha256="1" * 64)))
                check("jump rejects a changed bastion key", result["status"] == "host_key_mismatch" and result["remote_outcome"] == "not_run", result)
                interaction = {"ready_prompt": "menu> ", "selection_prefix": ":",
                               "selection": "connect {user}@{host}:{port}", "shell_prompt": "fixture@asset$ "}
                interactive = dict(direct, route="interactive", bastion=target,
                                   target=dict(endpoint, port=2222), interaction=interaction)
                result = client.call(interactive)
                check("generic PTY menu selects the exact IP/user/port with no product binding",
                      result["exit_code"] == 7 and result["combined_output"] and not result["target_ssh_verified"] and
                      ("selection", ":connect fixture@127.0.0.1:2222") in EVENTS and PASSWORD not in json.dumps(result), result)
                result = client.call(dict(interactive, interaction=dict(interaction, su_credential=aliases[2], root_prompt="root@asset# ")))
                check("generic interactive root elevation uses its separate credential", result["exit_code"] == 7, result)
                result = client.call(dict(direct, script="printf should-not-run\n", require_root=True))
                check("root guard verifies the UID before user code", result["exit_code"] == 77 and "should-not-run" not in result["stdout"], result)
                result = client.call(dict(direct, script="printf '\\377'\n"))
                check("decoding failure preserves raw bytes and independent remote outcome",
                      result["status"] == "encoding_error" and result["transport_status"] == "completed" and
                      base64.b64decode(result["stdout_base64"]) == b"\xff" and result["remote_outcome"] == "known", result)
                result = client.call(dict(direct, script="printf '\\304\\343\\272\\303'\n", output_encoding="cp936"))
                check("explicit CP936 remote output decodes without loss", result["status"] == "completed" and result["stdout"] == "你好", result)
                result = client.call(dict(direct, script="printf '12345678901234567890'\n", output_limit_bytes=8))
                check("output limit retains unknown remote outcome", result["status"] == "output_limit" and result["remote_outcome"] == "unknown" and len(result["stdout"]) <= 8, result)
                result = client.call(dict(direct, script="sleep 1\nprintf late\n", timeout_ms=500))
                check("timeout closes local resources and retains unknown remote outcome", result["status"] == "timed_out" and result["remote_outcome"] == "unknown" and result["cleanup_complete"], result)
                result = client.call(dict(direct, script="sleep 1\nprintf late\n"), cancel=True)
                check("MCP cancellation reaches native SSH", result["status"] == "cancelled" and result["remote_outcome"] == "unknown" and result["cleanup_complete"], result)
                for name, arguments in [
                    ("code-shaped host", dict(direct, target=dict(target, host="$(hostname)"))),
                    ("invalid port", dict(direct, target=dict(target, port=0))),
                    ("plaintext password", dict(direct, target=dict(target, password="not-allowed"))),
                    ("multiline selection", dict(interactive, interaction=dict(interaction, selection="connect {host}\necho unexpected"))),
                    ("missing target placeholder", dict(interactive, interaction=dict(interaction, selection="1"))),
                    ("oversized PTY line", dict(interactive, script="x" * 2049)),
                    ("local key for interactive asset", dict(interactive, target=dict(endpoint, credential="", private_key_path=str(key_path), user="fixture"))),
                    ("unenforceable interactive asset key pin", dict(interactive, target=dict(endpoint, host_key_sha256=FINGERPRINT))),
                ]:
                    budget()
                    result = client.call(arguments)
                    check("validation rejects " + name, result["status"] == "validation_error" and result["remote_outcome"] == "not_run", result)
            client.close()
            check("MCP session shuts down cleanly", client.process.returncode == 0)
        finally:
            STOP.set()
            listener.close()
            for transport in TRANSPORTS[:128]:
                transport.close()
            for process in PROCESSES[:128]:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
            cleanup_deadline = time.monotonic() + 5
            for _ in range(100):
                if not any(t.is_alive() for t in THREADS) or time.monotonic() >= cleanup_deadline:
                    break
                for thread in THREADS[:256]:
                    thread.join(timeout=.01)
            for alias in aliases:
                if not VAULT.CredDeleteW(alias, 1, 0) and ctypes.get_last_error() != 1168:
                    raise ctypes.WinError(ctypes.get_last_error())
            print(json.dumps({"checks": len(results), "passed": sum(r["passed"] for r in results),
                              "processes_reaped": all(p.poll() is not None for p in PROCESSES),
                              "fixture_threads_stopped": all(not t.is_alive() for t in THREADS)}, ensure_ascii=False), flush=True)
            if any(t.is_alive() for t in THREADS) or any(p.poll() is None for p in PROCESSES):
                raise RuntimeError("SSH fixture cleanup incomplete")


if __name__ == "__main__":
    main()
