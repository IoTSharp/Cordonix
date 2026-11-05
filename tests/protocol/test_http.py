"""Native HTTP acceptance against bounded local HTTP, HTTPS and proxy peers."""
import argparse
import base64
import contextlib
import datetime
import http.server
import json
import os
from pathlib import Path
import queue
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

DEADLINE = time.monotonic() + 100
STOP = threading.Event()
EVENTS = []
PROCESSES = []


def own(process):
    process.cordonix_created_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
    PROCESSES.append(process)
    return process


def budget():
    if STOP.is_set() or time.monotonic() >= DEADLINE:
        raise TimeoutError("HTTP fixture deadline/cancellation")


class Peer(http.server.ThreadingHTTPServer):
    daemon_threads = False
    block_on_close = True
    def get_request(self):
        sock, address = super().get_request()
        sock.settimeout(2)
        return sock, address

    def handle_error(self, request, address):
        if not isinstance(sys.exception(), (BrokenPipeError, ConnectionResetError, ssl.SSLError)):
            super().handle_error(request, address)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, format, *args):
        pass

    def answer(self, payload, code=200, headers=()):
        self.send_response(code)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.send_header("X-Cordonix-Fixture", "native-peer")
        for key, value in headers:
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(payload)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                pass
        self.close_connection = True

    def do_GET(self):
        EVENTS.append((self.command, self.path))
        if self.path.startswith("http://unresolvable.invalid/"):
            self.answer(b"via-explicit-proxy")
        elif self.path == "/binary":
            self.answer(b"\xff\x00\x80\x01")
        elif self.path == "/large":
            self.answer(b"a" * 1024)
        elif self.path == "/split-utf8":
            self.answer("中文".encode("utf-8"))
        elif self.path == "/slow":
            STOP.wait(.5)
            self.answer(b"late")
        elif self.path == "/redirect":
            self.answer(b"redirect", 302, [("Location", "/should-not-follow")])
        elif self.path == "/missing":
            self.answer(b"missing", 404)
        elif self.path == "/forbidden":
            self.answer(b"forbidden", 403)
        elif self.path == "/cp936":
            self.answer("你好".encode("gbk"))
        else:
            self.answer(("hello 中文 " + self.path).encode())

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        count = int(self.headers.get("Content-Length", "0"))
        if count > 65536:
            raise RuntimeError("fixture body bound")
        body = self.rfile.read(count)
        EVENTS.append((self.command, self.path, body, self.headers.get_all("X-Literal", [])))
        if self.path == "/drop":
            self.connection.shutdown(socket.SHUT_RDWR)
            self.close_connection = True
            return
        if self.path == "/slow":
            STOP.wait(.5)
        self.answer(body)

    do_PUT = do_POST
    do_PATCH = do_POST
    do_DELETE = do_POST
    do_OPTIONS = do_POST


def server_loop(server):
    for _ in range(1000):
        if STOP.is_set() or time.monotonic() >= DEADLINE:
            return
        server.handle_request()


def unused_port():
    with socket.socket() as peer:
        peer.bind(("127.0.0.1", 0))
        return peer.getsockname()[1]


@contextlib.contextmanager
def tunnel_proxy(protocol, destination):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    listener.settimeout(.05)
    stop = threading.Event()
    evidence, peers, workers, errors = [], [], [], []
    end = min(DEADLINE, time.monotonic() + 15)
    def exact(peer, size):
        data = bytearray()
        for _ in range(8192):
            budget()
            if len(data) == size:
                return bytes(data)
            chunk = peer.recv(size - len(data))
            if not chunk:
                raise EOFError()
            data.extend(chunk)
        raise TimeoutError("proxy exact read bound")
    def handle(peer):
        upstream = None
        try:
            peer.settimeout(1)
            first = peer.recv(1)
            if not first:
                evidence.append(("probe", None))
                return
            if protocol == "http":
                if first != b'C':
                    return
                header = bytearray(first)
                for _ in range(8192):
                    budget()
                    if header.endswith(b'\r\n\r\n'):
                        break
                    header.extend(exact(peer, 1))
                evidence.append(("connect", bytes(header)))
                upstream = socket.create_connection(destination, timeout=1)
                peer.sendall(b'HTTP/1.1 200 Connection Established\r\n\r\n')
            else:
                if first != b'\x05':
                    return
                count = exact(peer, 1)[0]
                if b'\x00' not in exact(peer, count):
                    return
                peer.sendall(b'\x05\x00')
                prefix = exact(peer, 4)
                if prefix[:3] != b'\x05\x01\x00':
                    return
                kind = prefix[3]
                host = exact(peer, 4 if kind == 1 else 16) if kind in (1, 4) else exact(peer, exact(peer, 1)[0])
                target_port = int.from_bytes(exact(peer, 2), 'big')
                evidence.append(("socks", (kind, host, target_port)))
                upstream = socket.create_connection(destination, timeout=1)
                peer.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00')
            for _ in range(15000):
                if stop.is_set() or time.monotonic() >= end:
                    return
                ready, _, _ = select.select([peer, upstream], [], [], .01)
                for source in ready:
                    data = source.recv(8192)
                    if not data:
                        return
                    if source is peer:
                        evidence.append(("application", data))
                    (upstream if source is peer else peer).sendall(data)
        except (EOFError, OSError):
            pass
        except BaseException as error:
            errors.append(error)
        finally:
            peer.close()
            if upstream:
                upstream.close()
    def accept():
        for _ in range(300):
            if stop.is_set() or time.monotonic() >= end:
                return
            try:
                peer, _ = listener.accept()
            except socket.timeout:
                continue
            if len(workers) >= 32:
                errors.append(RuntimeError("proxy fixture connection bound"))
                peer.close()
                return
            peers.append(peer)
            worker = threading.Thread(target=handle, args=(peer,))
            workers.append(worker)
            worker.start()
    acceptor = threading.Thread(target=accept)
    acceptor.start()
    try:
        yield listener.getsockname()[1], evidence
    finally:
        stop.set()
        acceptor.join(timeout=2)
        listener.close()
        for peer in peers[:32]:
            try:
                peer.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        for worker in workers[:32]:
            worker.join(timeout=1)
        if acceptor.is_alive() or any(t.is_alive() for t in workers):
            raise RuntimeError("proxy fixture cleanup incomplete")
        if errors:
            raise errors[0]


class Client:
    def __init__(self, binary, shim):
        self.binary, self.shim = binary, shim
        self.process = None
        self.lines = queue.Queue()
        self.reader = None
        self.sequence = 0
        if not shim:
            self.process = own(subprocess.Popen([str(binary), "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                                stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW))
            def read():
                for _ in range(256):
                    line = self.process.stdout.readline(2**21)
                    if not line:
                        return
                    self.lines.put(json.loads(line))
            self.reader = threading.Thread(target=read)
            self.reader.start()
            self.send({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {}})
            self.receive()

    def send(self, request):
        self.process.stdin.write((json.dumps(request, ensure_ascii=False) + "\n").encode())
        self.process.stdin.flush()

    def receive(self):
        budget()
        return self.lines.get(timeout=min(12, DEADLINE - time.monotonic()))

    def call(self, request, cancel=False):
        budget()
        if self.shim:
            arguments = [str(self.binary)] + (["--cancel"] if cancel else [])
            process = own(subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           creationflags=subprocess.CREATE_NO_WINDOW))
            out, err = process.communicate(json.dumps(request, ensure_ascii=False).encode(), timeout=12)
            if process.returncode:
                raise RuntimeError((process.returncode, err.decode("utf-8", errors="backslashreplace")))
            return json.loads(out)
        self.sequence += 1
        self.send({"jsonrpc": "2.0", "id": self.sequence, "method": "tools/call",
                   "params": {"name": "http", "arguments": request}})
        if cancel:
            STOP.wait(.08)
            self.send({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": self.sequence}})
        response = self.receive()
        if "error" in response:
            raise AssertionError(response)
        return json.loads(response["result"]["content"][0]["text"])

    def close(self):
        if self.process and self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(timeout=5)
            self.reader.join(timeout=2)
            assert self.process.returncode == 0 and not self.reader.is_alive()


def check_representations(client, origin, check):
    body = "中文 $(literal) \"quoted\"\n"
    cases = [
        ("POST", "/representation-echo", {"method": "POST", "body": body},
         body.encode("utf-8"), body, "completed", 200, "known", "ok", False),
        ("CP936", "/cp936", {"output_encoding": "cp936"},
         "你好".encode("gbk"), "你好", "completed", 200, "known", "ok", False),
        ("binary", "/binary", {}, b"\xff\x00\x80\x01", None,
         "completed", 200, "known", "encoding_error", False),
        ("HTTP 404", "/missing", {}, b"missing", "missing",
         "completed", 404, "known", "ok", False),
        ("response limit", "/large", {"response_limit_bytes": 8}, b"a" * 8, "a" * 8,
         "output_limit", 200, "unknown", "ok", False),
        ("split UTF-8 limit", "/split-utf8", {"response_limit_bytes": 1}, b"\xe4", None,
         "output_limit", 200, "unknown", "encoding_error", False),
        ("POST timeout", "/slow", {"method": "POST", "body": "once-timeout", "timeout_ms": 100},
         b"", "", "timed_out", None, "unknown", "ok", False),
        ("POST cancellation", "/slow", {"method": "POST", "body": "once-cancel"},
         b"", "", "cancelled", None, "unknown", "ok", True),
    ]
    evidence_keys = (
        "status", "stage", "status_code", "http_success", "body_encoding_status", "body_bytes",
        "request_started", "response_complete", "request_send_started", "remote_outcome",
        "cleanup_complete", "proxy_bridge_used", "proxy",
    )
    for label, path, options, raw, decoded, status, status_code, outcome, encoding_status, cancel in cases:
        baseline = None
        for mode in (None, "text", "base64"):
            request = {"url": origin + path, **options}
            if mode is not None:
                request["representation"] = mode
            method = request.get("method", "GET")
            before = len([event for event in EVENTS if event[:2] == (method, path)])
            result = client.call(request, cancel=cancel)
            after = len([event for event in EVENTS if event[:2] == (method, path)])
            projected = (
                ("body" not in result if mode == "base64" else "body" in result and result["body"] == decoded)
                and ("body_base64" not in result if mode == "text" and encoding_status == "ok"
                     else "body_base64" in result and base64.b64decode(result["body_base64"], validate=True) == raw)
            )
            check(label + " " + (mode or "default") + " preserves bytes, status and one dispatch",
                  result["status"] == status and result["status_code"] == status_code
                  and result["remote_outcome"] == outcome and result["cleanup_complete"]
                  and result["body_bytes"] == len(raw) and result["body_encoding_status"] == encoding_status
                  and result["response_complete"] == (outcome == "known") and projected
                  and after == before + 1, result)
            if baseline is None:
                baseline = result
            else:
                check(label + " " + mode + " preserves default outcome and cleanup evidence",
                      all(result[key] == baseline[key] for key in evidence_keys),
                      {"default": baseline, "projection": result})
        if cancel:
            result = client.call({"url": origin + "/after-representation-cancel", "representation": "text"})
            check("HTTP MCP session remains usable after representation cancellation",
                  result["status"] == "completed" and result["body"] == "hello 中文 /after-representation-cancel"
                  and result["cleanup_complete"], result)
    for invalid in ("invalid", 42, None, True):
        before = len(EVENTS)
        result = client.call({"url": origin + "/", "representation": invalid})
        check("invalid representation " + repr(invalid) + " is rejected before dispatch",
              result["status"] == "validation_error" and result["remote_outcome"] == "not_run"
              and not result["request_started"] and result["cleanup_complete"] and len(EVENTS) == before, result)


def certificate(folder):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(minutes=1))
            .not_valid_after(now + datetime.timedelta(hours=1))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost")]), critical=False).sign(key, hashes.SHA256()))
    key_path, cert_path = Path(folder) / "key.pem", Path(folder) / "cert.pem"
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    return cert_path, key_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--shim", action="store_true")
    parser.add_argument("--pilot", action="store_true")
    parser.add_argument("--public-https", action="store_true")
    args = parser.parse_args()
    servers, threads, checks = [], [], []
    client = None
    def start(context=None):
        server = Peer(("127.0.0.1", 0), Handler)
        server.timeout = .05
        if context:
            server.socket = context.wrap_socket(server.socket, server_side=True)
        servers.append(server)
        thread = threading.Thread(target=server_loop, args=(server,))
        threads.append(thread)
        thread.start()
        return server.server_address[1]
    def check(name, condition, result):
        checks.append({"name": name, "passed": bool(condition)})
        print(("PASS " if condition else "FAIL ") + name, flush=True)
        if not condition:
            raise AssertionError((name, result))
    try:
        port = start()
        origin = "http://127.0.0.1:" + str(port)
        client = Client(args.binary.resolve(), args.shim)
        result = client.call({"url": origin + "/literal?x=$(data)&a=two%20words"})
        check("GET preserves literal query and UTF-8 response through native WinHTTP", result["status"] == "completed" and result["status_code"] == 200
              and result["body"] == "hello 中文 /literal?x=$(data)&a=two%20words" and result["remote_outcome"] == "known" and result["cleanup_complete"], result)
        if args.pilot:
            return
        if not args.shim:
            check_representations(client, origin, check)
        with tunnel_proxy("http", ("127.0.0.1", port)) as (proxy_port, evidence):
            candidates = [proxy_port, unused_port()]
            result = client.call({"url": origin + "/forbidden", "proxy_ports": candidates})
            check("HTTP 403 never probes or falls back to local proxies", result["status_code"] == 403 and
                  result["proxy"]["probed_ports"] == [] and not evidence, result)
            if args.shim:
                result = client.call({"url": "http://fixture.invalid:%d/automatic" % port, "proxy_ports": candidates, "timeout_ms": 5000})
                check("automatic pre-send direct failure uses a recognized HTTP tunnel and origin-form bridge", result["status"] == "completed" and
                      result["body"] == "hello 中文 /automatic" and result["proxy_bridge_used"] and result["proxy"]["direct_error"] and
                      result["proxy"]["selected_proxy"] == "http://127.0.0.1:%d" % proxy_port and len(result["proxy"]["probed_ports"]) == 2, result)
                check("bridged HTTP request target is origin form", any(kind == "application" and b"GET /automatic HTTP/1.1\r\n" in data for kind,data in evidence), evidence)
            before = len(evidence)
            result = client.call({"url": "http://127.0.0.1:%d/direct" % unused_port(), "proxy": "direct", "proxy_ports": candidates})
            check("explicit direct disables candidate probing after a connection failure", result["status"] == "connection_error" and
                  result["proxy"]["probed_ports"] == [] and len(evidence) == before, result)
            result = client.call({"url": origin + "/slow", "method": "POST", "body": "once", "proxy_ports": candidates, "timeout_ms": 100})
            check("POST dispatch phase forbids timeout replay through an available proxy", result["status"] == "timed_out" and
                  result["request_send_started"] and result["proxy"]["probed_ports"] == [] and len(evidence) == before, result)
            count = len([e for e in EVENTS if e[:2] == ("POST", "/drop")])
            result = client.call({"url": origin + "/drop", "method": "POST", "body": "once", "proxy_ports": candidates})
            check("connection loss after POST bytes never replays the request", result["status"] == "connection_error" and
                  result["request_send_started"] and result["proxy"]["probed_ports"] == [] and len(evidence) == before and
                  len([e for e in EVENTS if e[:2] == ("POST", "/drop")]) == count + 1, result)
        with tunnel_proxy("socks", ("127.0.0.1", port)) as (proxy_port, evidence):
            result = client.call({"url": "http://fixture.invalid:%d/socks" % port, "proxy": "socks5://127.0.0.1:%d" % proxy_port})
            check("explicit SOCKS5 bridges HTTP while keeping destination DNS at the proxy", result["status"] == "completed" and
                  result["body"] == "hello 中文 /socks" and result["proxy_bridge_used"] and not result["proxy"]["direct_attempted"] and
                  any(kind == "socks" and data[0] == 3 and data[1] == b"fixture.invalid" for kind,data in evidence), result)
            if args.shim:
                result = client.call({"url": "http://fixture.invalid:%d/auto-socks" % port, "proxy_ports": [unused_port(), proxy_port], "timeout_ms": 5000})
                check("automatic proxy evaluation can select the SOCKS route", result["status"] == "completed" and
                      result["body"] == "hello 中文 /auto-socks" and result["proxy"]["selected_proxy"].startswith("socks5://"), result)
            before = len(evidence)
            result = client.call({"url": origin + "/proxy-unsupported", "proxy": "socks5://127.0.0.1:%d" % proxy_port})
            check("loopback URL proxy limitation is rejected before creating a misleading tunnel", result["status"] == "not_supported" and
                  not result["request_started"] and not result["proxy_bridge_used"] and len(evidence) == before, result)
        body = "中文 $(literal) \"quoted\"\n"
        result = client.call({"url": origin + "/echo", "method": "POST", "body": body,
                              "headers": [{"name": "X-Literal", "value": "$(one)"}, {"name": "X-Literal", "value": "two words"}]})
        check("POST sends exact UTF-8 bytes and duplicate literal headers", result["body"] == body and
              ("POST", "/echo", body.encode(), ["$(one)", "two words"]) in EVENTS, result)
        result = client.call({"url": origin + "/binary"})
        check("binary response retains exact Base64 with no replacement text", result["status"] == "completed" and result["body"] is None and result["body_encoding_status"] == "encoding_error" and
              base64.b64decode(result["body_base64"]) == b"\xff\x00\x80\x01", result)
        result = client.call({"url": origin + "/cp936", "output_encoding": "cp936"})
        check("explicit CP936 response decoding is lossless", result["body"] == "你好", result)
        result = client.call({"url": origin + "/missing"})
        check("HTTP 404 remains distinct from transport failure", result["status"] == "completed" and result["status_code"] == 404 and not result["http_success"], result)
        result = client.call({"url": origin + "/redirect"})
        check("redirect response is returned without a second request", result["status_code"] == 302 and ("GET", "/should-not-follow") not in EVENTS, result)
        result = client.call({"url": origin + "/", "method": "HEAD"})
        check("HEAD has headers and an empty completed body", result["status"] == "completed" and result["body_bytes"] == 0 and "X-Cordonix-Fixture" in result["response_headers"], result)
        result = client.call({"url": "http://unresolvable.invalid/proxy", "proxy": "http://127.0.0.1:" + str(port)})
        check("explicit HTTP proxy handles an otherwise unresolved target", result["body"] == "via-explicit-proxy" and result["status"] == "completed", result)
        result = client.call({"url": origin + "/large", "response_limit_bytes": 8})
        check("bounded response preserves its prefix and UNKNOWN outcome", result["status"] == "output_limit" and result["body"] == "a" * 8 and result["remote_outcome"] == "unknown", result)
        before = len([e for e in EVENTS if e[:2] == ("POST", "/slow")])
        result = client.call({"url": origin + "/slow", "method": "POST", "body": "one mutation", "timeout_ms": 100})
        check("POST timeout remains UNKNOWN and is not retried", result["status"] == "timed_out" and result["remote_outcome"] == "unknown" and result["cleanup_complete"]
              and len([e for e in EVENTS if e[:2] == ("POST", "/slow")]) == before + 1, result)
        result = client.call({"url": origin + "/slow"}, cancel=True)
        check("cancellation closes asynchronous WinHTTP handles", result["status"] == "cancelled" and result["remote_outcome"] == "unknown" and result["cleanup_complete"], result)
        for name, request in [
            ("URL credentials", {"url": "http://user:secret@127.0.0.1:" + str(port)}),
            ("header line injection", {"url": origin, "headers": [{"name": "X-Test", "value": "x\r\nInjected: y"}]}),
            ("body framing override", {"url": origin, "headers": [{"name": "Content-Length", "value": "2"}]}),
            ("unsupported URL scheme", {"url": "file:///tmp/file"}),
            ("GET body", {"url": origin, "body": "not empty"}),
            ("proxy credentials", {"url": origin, "proxy": "http://user:secret@127.0.0.1:7890"}),
            ("timeout type", {"url": origin, "timeout_ms": True}),
        ]:
            result = client.call(request)
            check("validation rejects " + name, result["status"] == "validation_error" and result["remote_outcome"] == "not_run", result)
        with tempfile.TemporaryDirectory(prefix="cordonix-http-tls-") as folder:
            cert_path, key_path = certificate(folder)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert_path, key_path)
            tls_port = start(context)
            result = client.call({"url": "https://localhost:" + str(tls_port), "timeout_ms": 5000})
            check("HTTPS rejects an untrusted certificate without disabling verification", result["status"] == "tls_error" and result["status_code"] is None and result["cleanup_complete"], result)
            with tunnel_proxy("socks", ("127.0.0.1", tls_port)) as (proxy_port, evidence):
                result = client.call({"url": "https://fixture.invalid:" + str(tls_port), "proxy": "socks5://127.0.0.1:%d" % proxy_port, "timeout_ms": 5000})
                check("SOCKS HTTPS CONNECT bridge preserves system certificate rejection", result["status"] == "tls_error" and
                      result["proxy_bridge_used"] and any(kind == "application" and data[:1] == b'\x16' for kind,data in evidence), result)
        if args.public_https:
            result = client.call({"url": "https://example.com/", "proxy": "http://127.0.0.1:7890", "timeout_ms": 10000})
            check("public HTTPS completes with normal system certificate verification", result["status"] == "completed" and result["status_code"] == 200, result)
    finally:
        close_error = None
        if client:
            try:
                client.close()
            except (OSError, AssertionError, subprocess.TimeoutExpired) as error:
                close_error = str(error)
        STOP.set()
        for thread in threads[:4]:
            thread.join(timeout=3)
        for server in servers[:4]:
            server.server_close()
        for process in PROCESSES[:128]:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3)
        clean = all(not thread.is_alive() for thread in threads) and all(process.poll() is not None for process in PROCESSES)
        process_records = [{"pid": p.pid, "parent_pid": os.getpid(), "created_utc": p.cordonix_created_utc,
                            "argv": p.args, "exit_code": p.poll()} for p in PROCESSES[:128]]
        print(json.dumps({"checks": len(checks), "passed": sum(c["passed"] for c in checks), "fixture_cleanup": clean,
                          "processes": process_records}), flush=True)
        if not clean or close_error:
            raise RuntimeError("HTTP fixture cleanup incomplete")


if __name__ == "__main__":
    main()
