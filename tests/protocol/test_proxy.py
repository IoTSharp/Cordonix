"""Native proxy transport tests using real bounded loopback protocol peers."""
import argparse
import contextlib
import ctypes as C
from pathlib import Path
import socket
import threading
import time

DEADLINE = time.monotonic() + 60
INVALID = C.c_size_t(-1).value
KERNEL = C.WinDLL('kernel32', use_last_error=True)
KERNEL.GetTickCount64.restype = C.c_uint64
KERNEL.CreateEventW.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_wchar_p]
KERNEL.CreateEventW.restype = C.c_void_p
KERNEL.SetEvent.argtypes = [C.c_void_p]
KERNEL.CloseHandle.argtypes = [C.c_void_p]
WSA = C.WinDLL('ws2_32', use_last_error=True)
WSA.WSAStartup.argtypes = [C.c_ushort, C.c_void_p]

class Request(C.Structure):
    _fields_ = [('host', C.c_char_p), ('port', C.c_uint), ('proxy', C.c_char_p),
                ('deadline_ms', C.c_uint64), ('cancellation', C.c_void_p), ('skip_direct', C.c_int),
                ('candidate_ports', C.POINTER(C.c_ushort)), ('candidate_count', C.c_size_t)]

class Result(C.Structure):
    _fields_ = [('socket', C.c_size_t), ('status', C.c_char_p), ('native_error', C.c_uint32),
                ('direct_error', C.c_uint32), ('selected_proxy', C.c_char * 64), ('direct_attempted', C.c_int),
                ('probed_ports', C.c_ushort * 2), ('probe_open', C.c_int * 2), ('probe_count', C.c_size_t),
                ('tunnel_attempts', C.c_uint), ('cleanup_complete', C.c_int)]

def budget():
    if time.monotonic() >= DEADLINE:
        raise TimeoutError('proxy fixture deadline')

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
    raise TimeoutError('fixture read count')

@contextlib.contextmanager
def listener(protocol, http_status=200, stall=False):
    server = socket.socket()
    server.bind(('127.0.0.1', 0))
    server.listen(8)
    server.settimeout(.05)
    stop = threading.Event()
    evidence, errors, workers, peers = [], [], [], []
    deadline = min(DEADLINE, time.monotonic() + 10)
    def handle(peer):
        try:
            peer.settimeout(1)
            first = peer.recv(1)
            if not first:
                evidence.append(('probe', None))
                return
            if stall:
                stop.wait(2)
                return
            if protocol == 'invalid':
                peer.sendall(b'not a proxy\r\n\r\n')
                return
            if protocol == 'http':
                if first != b'C':
                    return
                header = bytearray(first)
                for _ in range(8192):
                    budget()
                    if header.endswith(b'\r\n\r\n'):
                        break
                    header.extend(exact(peer, 1))
                evidence.append(('http', bytes(header)))
                peer.sendall(('HTTP/1.1 %d Result\r\n\r\n' % http_status).encode())
                if not 200 <= http_status <= 299:
                    return
            else:
                if first != b'\x05':
                    return
                methods = exact(peer, 1)[0]
                offered = exact(peer, methods)
                if b'\x00' not in offered:
                    raise AssertionError('no anonymous method')
                peer.sendall(b'\x05\x00')
                prefix = exact(peer, 4)
                if prefix[:3] != b'\x05\x01\x00':
                    raise AssertionError(prefix)
                atyp = prefix[3]
                host = exact(peer, 4 if atyp == 1 else 16) if atyp in (1, 4) else exact(peer, exact(peer, 1)[0])
                port = int.from_bytes(exact(peer, 2), 'big')
                evidence.append(('socks', (atyp, host, port)))
                peer.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00')
            payload = peer.recv(1024)
            evidence.append(('business', payload))
            if payload:
                peer.sendall(payload)
        except (EOFError, ConnectionResetError, BrokenPipeError, socket.timeout):
            pass
        except BaseException as error:
            errors.append(error)
        finally:
            peer.close()
    def accept():
        for _ in range(200):
            if stop.is_set() or time.monotonic() >= deadline:
                return
            try:
                peer, _ = server.accept()
            except socket.timeout:
                continue
            if len(workers) >= 16:
                raise AssertionError('fixture connection bound')
            peers.append(peer)
            thread = threading.Thread(target=handle, args=(peer,), daemon=True)
            workers.append(thread)
            thread.start()
    acceptor = threading.Thread(target=accept, daemon=True)
    acceptor.start()
    try:
        yield server.getsockname()[1], evidence
    finally:
        stop.set()
        acceptor.join(timeout=2)
        server.close()
        for peer in peers[:16]:
            try:
                peer.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        for thread in workers[:16]:
            thread.join(timeout=1)
        if acceptor.is_alive() or any(thread.is_alive() for thread in workers):
            raise RuntimeError('proxy fixture threads remain')
        if errors:
            raise errors[0]

def unused_port():
    with socket.socket() as peer:
        peer.bind(('127.0.0.1', 0))
        return peer.getsockname()[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dll', type=Path)
    parser.add_argument('--pilot', action='store_true')
    options = parser.parse_args()
    library = C.CDLL(str(options.dll.resolve()))
    library.cdx_proxy_connect.argtypes = [C.POINTER(Request), C.POINTER(Result)]
    library.cdx_proxy_close.argtypes = [C.c_size_t]
    winsock = C.create_string_buffer(1024)
    if WSA.WSAStartup(0x202, winsock):
        raise RuntimeError('WSAStartup failed')
    checks = []
    def call(host='127.0.0.1', port=None, proxy='auto', ports=(), skip=False, timeout=3000, cancel=False):
        budget()
        candidates = (C.c_ushort * len(ports))(*ports)
        event = KERNEL.CreateEventW(None, True, False, None)
        request = Request(host.encode(), port or unused_port(), proxy.encode(), KERNEL.GetTickCount64() + timeout,
                          event, skip, candidates, len(ports))
        result = Result()
        thread = None
        if cancel:
            thread = threading.Thread(target=lambda: (threading.Event().wait(.05), KERNEL.SetEvent(event)))
            thread.start()
        try:
            library.cdx_proxy_connect(C.byref(request), C.byref(result))
        finally:
            if thread:
                thread.join(timeout=1)
                assert not thread.is_alive()
            KERNEL.CloseHandle(event)
        return result
    def check(name, value, result=None):
        if not value:
            details = {field: getattr(result, field) for field in ('status','native_error','direct_error','selected_proxy','direct_attempted','probe_count','tunnel_attempts')} if result else None
            raise AssertionError((name, details))
        checks.append(name)
        print('PASS ' + name, flush=True)
    def echo(result):
        payload = b'BUSINESS\x00\xff'
        try:
            if result.socket == INVALID:
                return False
            with socket.socket(fileno=result.socket) as peer:
                result.socket = INVALID
                peer.settimeout(1)
                peer.sendall(payload)
                return exact(peer, len(payload)) == payload
        finally:
            library.cdx_proxy_close(result.socket)
            result.socket = INVALID
    try:
        with listener('http') as (port, evidence):
            result = call(proxy='http://127.0.0.1:%d' % port)
            check('explicit HTTP CONNECT is recognized before socket transfer', result.status == b'completed' and result.selected_proxy.startswith(b'http://') and echo(result), result)
        with listener('socks') as (port, evidence):
            result = call(proxy='socks5://127.0.0.1:%d' % port, host='fixture.invalid')
            check('explicit SOCKS5 supports remote DNS and binary tunnel', result.status == b'completed' and echo(result), result)
            check('SOCKS5 sends hostname as ATYP3', any(kind == 'socks' and value[0] == 3 and value[1] == b'fixture.invalid' for kind,value in evidence), result)
        if not options.pilot:
            with listener('invalid') as (direct, direct_evidence), listener('http') as (proxy, proxy_evidence):
                result = call(port=direct, ports=(proxy,unused_port()))
                check('direct success never probes local proxies', result.status == b'completed' and result.selected_proxy == b'direct' and result.probe_count == 0 and not proxy_evidence, result)
                library.cdx_proxy_close(result.socket)
            with listener('http') as (port, evidence):
                closed = unused_port()
                result = call(ports=(port,closed))
                check('auto direct failure probes both candidates', result.direct_attempted and result.direct_error and result.probe_count == 2 and list(result.probe_open) == [1,0], result)
                check('open HTTP proxy requires successful CONNECT', result.status == b'completed' and echo(result), result)
            with listener('socks') as (port, evidence):
                result = call(ports=(port,unused_port()))
                check('SOCKS on the HTTP-preferred port is recognized by alternate handshake', result.status == b'completed' and result.tunnel_attempts == 2 and echo(result), result)
            with listener('http') as (port, evidence):
                result = call(ports=(unused_port(),port))
                check('HTTP on the SOCKS-preferred port is recognized by alternate handshake', result.status == b'completed' and result.tunnel_attempts == 2 and echo(result), result)
            with listener('http') as (port, evidence):
                result = call(host='fixture.invalid', ports=(port,unused_port()), skip=True)
                check('skip_direct supports HTTP hostname fallback without repeating direct', not result.direct_attempted and result.status == b'completed' and echo(result), result)
            with listener('invalid') as (port, evidence):
                result = call(ports=(port,unused_port()))
                check('open unrelated service is not accepted as a proxy', result.status == b'proxy_unavailable' and result.socket == INVALID and result.tunnel_attempts == 2, result)
                check('no business bytes are emitted during proxy evaluation', not any(kind == 'business' for kind,value in evidence), result)
            with listener('http', http_status=407) as (port, evidence):
                result = call(ports=(port,unused_port()))
                check('recognized HTTP refusal is not retried as another protocol', result.status == b'proxy_error' and result.tunnel_attempts == 1 and result.native_error == 407, result)
            with listener('http', http_status=2000) as (port, evidence):
                result = call(proxy='http://127.0.0.1:%d' % port)
                check('invalid four-digit HTTP status cannot establish a tunnel', result.status == b'proxy_unavailable' and result.socket == INVALID, result)
            with listener('socks') as (port, evidence):
                result = call(proxy='socks5://127.0.0.1:%d' % port, host='::1')
                check('SOCKS5 preserves an IPv6 destination', result.status == b'completed' and echo(result) and any(kind == 'socks' and value[0] == 4 and value[1] == socket.inet_pton(socket.AF_INET6,'::1') for kind,value in evidence), result)
            with listener('http') as (port, evidence):
                result = call(proxy='http://127.0.0.1:%d' % port, host='::1')
                check('HTTP CONNECT brackets IPv6 authority', result.status == b'completed' and echo(result) and any(kind == 'http' and b'CONNECT [::1]:' in value for kind,value in evidence), result)
            with listener('http', stall=True) as (port, evidence):
                result = call(proxy='http://127.0.0.1:%d' % port, timeout=100)
                check('absolute shared deadline bounds a silent listener', result.status == b'timed_out' and result.socket == INVALID and result.cleanup_complete, result)
            with listener('http', stall=True) as (port, evidence):
                result = call(proxy='http://127.0.0.1:%d' % port, cancel=True)
                check('cancellation closes in-flight tunnel sockets', result.status == b'cancelled' and result.socket == INVALID and result.cleanup_complete, result)
            result = call(proxy='direct')
            check('direct override never probes on failure', result.status == b'connection_error' and result.direct_attempted and not result.probe_count, result)
            result = call(ports=(unused_port(),unused_port()))
            check('closed local candidates do not become proxy routes', result.status == b'proxy_unavailable' and result.probe_count == 2 and not any(result.probe_open) and result.tunnel_attempts == 0, result)
            for proxy in ('http://example.com:7890','socks5://user:secret@127.0.0.1:1080','http://127.0.0.1:0','http://127.0.0.1:7890/path'):
                result = call(proxy=proxy)
                check('invalid/nonlocal/authenticated proxy URI is rejected', result.status == b'validation_error' and not result.direct_attempted and not result.probe_count, result)
    finally:
        WSA.WSACleanup()
    print({'status':'PASS','passed':len(checks),'checks':len(checks),'fixture_cleanup':True})

if __name__ == '__main__':
    main()
