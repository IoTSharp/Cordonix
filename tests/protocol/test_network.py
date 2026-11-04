"""Bounded real loopback protocol fixtures for the native network tool."""
import argparse
import contextlib
import json
from pathlib import Path
import queue
import select
import socket
import subprocess
import threading
import time

DEADLINE = time.monotonic() + 90

def budget():
    if time.monotonic() >= DEADLINE:
        raise TimeoutError('network test deadline')

class Client:
    def __init__(self, binary, harness):
        self.binary, self.harness, self.sequence = binary, harness, 1
        self.process = None
        self.queue = queue.Queue()
        if not harness:
            self.process = subprocess.Popen([str(binary), '--mcp'], stdin=subprocess.PIPE,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            creationflags=subprocess.CREATE_NO_WINDOW)
            def reader():
                for _ in range(128):
                    line = self.process.stdout.readline(2**20)
                    if not line:
                        return
                    self.queue.put(json.loads(line))
            self.reader = threading.Thread(target=reader, daemon=True)
            self.reader.start()
            self.send({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {}})
            self.queue.get(timeout=5)

    def send(self, value):
        self.process.stdin.write((json.dumps(value) + '\n').encode())
        self.process.stdin.flush()

    def call(self, arguments, cancel=False):
        budget()
        if self.harness:
            process = subprocess.Popen([str(self.binary)] + (['--cancel'] if cancel else []), stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                stdout, stderr = process.communicate(json.dumps(arguments).encode(), timeout=5)
                if process.returncode:
                    raise RuntimeError(stderr.decode('utf-8', 'replace'))
                return json.loads(stdout)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=2)
        self.sequence += 1
        self.send({'jsonrpc': '2.0', 'id': self.sequence, 'method': 'tools/call',
                   'params': {'name': 'network', 'arguments': arguments}})
        if cancel:
            threading.Event().wait(.05)
            self.send({'jsonrpc': '2.0', 'method': 'notifications/cancelled', 'params': {'requestId': self.sequence}})
        response = self.queue.get(timeout=5)
        if 'error' in response:
            raise AssertionError(response)
        return json.loads(response['result']['content'][0]['text'])

    def close(self):
        if self.process:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=5)
            finally:
                if self.process.poll() is None:
                    self.process.kill()
                    self.process.wait(timeout=2)
            self.reader.join(timeout=2)
            assert not self.reader.is_alive()
            assert self.process.returncode == 0

def exact(sock, size):
    data = bytearray()
    for _ in range(65536):
        budget()
        if len(data) == size:
            return bytes(data)
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError('fixture expected bytes')
        data.extend(chunk)
    raise TimeoutError('fixture read bound')

def mqtt_packet(sock):
    header = exact(sock, 1)[0]
    remaining, multiplier = 0, 1
    for _ in range(4):
        digit = exact(sock, 1)[0]
        remaining += (digit & 127) * multiplier
        if not digit & 128:
            if remaining > 70000:
                raise RuntimeError('fixture MQTT bound')
            return header, exact(sock, remaining)
        multiplier *= 128
    raise RuntimeError('malformed MQTT fixture packet')

@contextlib.contextmanager
def fixture(handler):
    budget()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(.1)
    stop = threading.Event()
    errors, evidence = [], {}
    deadline = min(DEADLINE, time.monotonic() + 5)
    def serve():
        peer = None
        try:
            for _ in range(50):
                if stop.is_set() or time.monotonic() >= deadline:
                    return
                try:
                    peer, _ = listener.accept()
                    break
                except socket.timeout:
                    continue
            if peer is None:
                return
            peer.settimeout(2)
            handler(peer, evidence, stop)
        except (ConnectionResetError, BrokenPipeError):
            pass
        except BaseException as error:
            errors.append(error)
        finally:
            if peer:
                peer.close()
    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        yield {'host': '127.0.0.1', 'port': listener.getsockname()[1]}, evidence
    finally:
        stop.set()
        thread.join(timeout=3)
        listener.close()
        assert not thread.is_alive(), 'fixture thread remains'
        if errors:
            raise errors[0]

@contextlib.contextmanager
def proxy_fixture(kind, target):
    budget()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(8)
    listener.settimeout(.1)
    stop = threading.Event()
    evidence = {'probe_connections': 0, 'tunnels': 0, 'authorities': [], 'client_data': bytearray()}
    errors = []
    deadline = min(DEADLINE, time.monotonic() + 5)
    def route(peer):
        first = peer.recv(1)
        if not first:
            evidence['probe_connections'] += 1
            return
        if kind == 'http':
            header = bytearray(first)
            for _ in range(8192):
                budget()
                if stop.is_set() or time.monotonic() >= deadline:
                    return
                if header.endswith(b'\r\n\r\n'):
                    break
                header.extend(exact(peer, 1))
            else:
                raise RuntimeError('proxy HTTP header bound')
            authority = f"{target['host']}:{target['port']}"
            if not header.startswith(('CONNECT ' + authority + ' HTTP/1.1\r\n').encode()):
                raise AssertionError(bytes(header))
        else:
            if first + exact(peer, 2) != b'\x05\x01\x00':
                raise AssertionError('unexpected SOCKS greeting')
            peer.sendall(b'\x05\x00')
            request = exact(peer, 4)
            if request[:3] != b'\x05\x01\x00':
                raise AssertionError(request)
            if request[3] == 1:
                host = socket.inet_ntop(socket.AF_INET, exact(peer, 4))
            elif request[3] == 4:
                host = socket.inet_ntop(socket.AF_INET6, exact(peer, 16))
            else:
                raise AssertionError('unexpected SOCKS address type')
            port = int.from_bytes(exact(peer, 2), 'big')
            authority = f'{host}:{port}'
            if (host, port) != (target['host'], target['port']):
                raise AssertionError(authority)
        evidence['authorities'].append(authority)
        upstream = socket.create_connection((target['host'], target['port']), timeout=2)
        try:
            peer.sendall(b'HTTP/1.1 200 Connection established\r\n\r\n' if kind == 'http' else b'\x05\x00\x00\x01' + bytes(6))
            evidence['tunnels'] += 1
            for _ in range(1000):
                budget()
                if stop.is_set() or time.monotonic() >= deadline:
                    return
                ready, _, _ = select.select([peer, upstream], [], [], .01)
                for source in ready:
                    data = source.recv(8192)
                    if not data:
                        return
                    if source is peer:
                        evidence['client_data'].extend(data)
                    (upstream if source is peer else peer).sendall(data)
        finally:
            upstream.close()
    def serve():
        for _ in range(8):
            if stop.is_set() or time.monotonic() >= deadline:
                return
            try:
                peer, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                if stop.is_set():
                    return
                raise
            try:
                peer.settimeout(2)
                route(peer)
            except (ConnectionResetError, BrokenPipeError):
                pass
            except BaseException as error:
                errors.append(error)
                return
            finally:
                peer.close()
    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        yield f'{kind if kind == "http" else "socks5"}://127.0.0.1:{listener.getsockname()[1]}', evidence
    finally:
        stop.set()
        listener.close()
        thread.join(timeout=3)
        assert not thread.is_alive(), 'proxy fixture thread remains'
        if errors:
            raise errors[0]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--harness', action='store_true')
    parser.add_argument('--pilot', action='store_true')
    options = parser.parse_args()
    client = Client(options.binary.resolve(), options.harness)
    passed = []
    def check(name, condition, result=None):
        budget()
        if not condition:
            raise AssertionError(name + ': ' + repr(result))
        passed.append(name)
        print('PASS ' + name, flush=True)
    try:
        with fixture(lambda peer, evidence, stop: None) as (endpoint, evidence):
            result = client.call(endpoint)
            check('native TCP connects and closes locally', result['status'] == 'completed' and result['connected'] and result['cleanup_complete'], result)
        payload = b'\0\xff$(literal)\r\n'
        def echo(peer, evidence, stop):
            evidence['received'] = exact(peer, len(payload))
            peer.sendall(payload)
        with fixture(echo) as (endpoint, evidence):
            result = client.call(dict(endpoint, action='exchange', allow_send=True, payload_hex=payload.hex()))
        check('binary TCP roundtrip preserves NUL, FF, shell literals and CRLF', bytes.fromhex(result['data_hex']) == payload and evidence['received'] == payload, result)
        check('socket send does not claim remote application acceptance', result['remote_outcome'] == 'unknown' and result['payload_bytes_sent'] == len(payload), result)
        if not options.pilot:
            check('default auto retains a successful direct route without local proxy probes',
                  result['proxy']['selected_proxy'] == 'direct' and result['proxy']['direct_attempted'] and
                  not result['proxy']['probed_ports'] and result['proxy']['tunnel_attempts'] == 0, result)
            with fixture(lambda peer, evidence, stop: peer.sendall(b'0123456789')) as (endpoint, evidence):
                result = client.call(dict(endpoint, action='exchange', read_limit_bytes=3))
            check('read cap is an explicit partial output state', result['status'] == 'output_limit' and result['data_hex'] == b'012'.hex(), result)
            with fixture(lambda peer, evidence, stop: stop.wait(1)) as (endpoint, evidence):
                result = client.call(dict(endpoint, action='exchange', timeout_ms=100))
            check('read timeout retains no application dispatch', result['status'] == 'timed_out' and result['remote_outcome'] == 'not_run' and result['cleanup_complete'], result)
            def no_answer(peer, evidence, stop):
                evidence['write'] = exact(peer, 1)
                stop.wait(1)
            with fixture(no_answer) as (endpoint, evidence):
                result = client.call(dict(endpoint, action='exchange', payload_hex='61', allow_send=True, timeout_ms=100))
            check('timeout after a write stays UNKNOWN', result['status'] == 'timed_out' and result['remote_outcome'] == 'unknown' and evidence['write'] == b'a', result)
            check('connected transport timeout never enters proxy discovery or replays a payload',
                  result['proxy']['selected_proxy'] == 'direct' and not result['proxy']['probed_ports'] and
                  result['proxy']['tunnel_attempts'] == 0, result)
            with fixture(lambda peer, evidence, stop: stop.wait(1)) as (endpoint, evidence):
                result = client.call(dict(endpoint, action='exchange'), cancel=True)
            check('cancellation reaches native socket polling', result['status'] == 'cancelled' and result['cleanup_complete'], result)
            def telnet(peer, evidence, stop):
                evidence['payload'] = exact(peer, 4)
                peer.sendall(b'\xff')
                threading.Event().wait(.01)
                peer.sendall(b'\xfb\x01\xff\xfd\x03ok\xff\xff')
                evidence['replies'] = exact(peer, 6)
            with fixture(telnet) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='telnet', action='exchange', payload_hex='61ff62', allow_send=True))
            check('Telnet escapes outgoing IAC and refuses unsupported options', evidence['payload'] == b'a\xff\xffb' and evidence['replies'] == b'\xff\xfe\x01\xff\xfc\x03', result)
            check('Telnet separates negotiated controls from binary data', result['data_hex'] == b'ok\xff'.hex() and result['status'] == 'completed', result)
            with fixture(lambda peer, evidence, stop: peer.sendall(b'\xff\xfa\x18ignore\xff\xf0ok')) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='telnet', action='exchange'))
            check('Telnet subnegotiation is excluded from application data', result['data_hex'] == b'ok'.hex(), result)
            with fixture(lambda peer, evidence, stop: peer.sendall(b'\xff')) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='telnet', action='exchange'))
            check('incomplete Telnet control sequence fails explicitly', result['status'] == 'protocol_error', result)
            def broker(peer, evidence, stop):
                evidence['connect'] = mqtt_packet(peer)
                peer.sendall(b'\x20\x02\x00\x00')
                evidence['next'] = mqtt_packet(peer)
            with fixture(broker) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='mqtt'))
            check('MQTT CONNECT is a real 3.1.1 clean session', evidence['connect'][0] == 0x10 and evidence['connect'][1][:10] == b'\x00\x04MQTT\x04\x02\x00\x00' and result['mqtt_connack'] == 0, result)
            check('MQTT bounded connection explicitly disconnects', evidence['next'] == (0xe0, b'') and result['completion'] == 'mqtt_connected', result)
            topic, mqtt_payload = '设备/test', b'\0\xffdata'
            with fixture(broker) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='mqtt', action='publish', topic=topic,
                                          payload_hex=mqtt_payload.hex(), allow_send=True, retain=True))
            packet_type, body = evidence['next']
            topic_length = int.from_bytes(body[:2], 'big')
            check('MQTT QoS0 publish carries exact UTF8 topic, binary payload and retain bit',
                  packet_type == 0x31 and body[2:2+topic_length].decode() == topic and body[2+topic_length:] == mqtt_payload, result)
            check('QoS0 is sent but never claimed acknowledged', result['status'] == 'completed' and result['remote_outcome'] == 'unknown' and result['completion'] == 'qos0_sent_not_acknowledged', result)
            def refuse(peer, evidence, stop):
                mqtt_packet(peer)
                peer.sendall(b'\x20\x02\x00\x05')
            with fixture(refuse) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='mqtt'))
            check('MQTT broker refusal retains CONNACK reason', result['status'] == 'broker_refused' and result['mqtt_connack'] == 5, result)
            def malformed(peer, evidence, stop):
                mqtt_packet(peer)
                peer.sendall(b'\x21\x02\x00\x00')
            with fixture(malformed) as (endpoint, evidence):
                result = client.call(dict(endpoint, protocol='mqtt'))
            check('malformed MQTT CONNACK does not become successful connection', result['status'] == 'protocol_error', result)
            base = {'host': '127.0.0.1', 'port': 9}
            for name, arguments, expected in [
                ('raw send requires explicit authorization', dict(base, action='exchange', payload_hex='61'), 'validation_error'),
                ('MQTT publish requires explicit authorization', dict(base, protocol='mqtt', action='publish', topic='a'), 'validation_error'),
                ('odd hex is rejected', dict(base, payload_hex='a'), 'validation_error'),
                ('nonhex is rejected', dict(base, payload_hex='zz'), 'validation_error'),
                ('code-like host is rejected', dict(base, host='$(host)'), 'validation_error'),
                ('MQTT wildcard topic is rejected', dict(base, protocol='mqtt', action='publish', topic='a/#', allow_send=True), 'validation_error'),
                ('TLS is explicit NOT_SUPPORTED, never plaintext fallback', dict(base, tls=True), 'not_supported'),
            ]:
                result = client.call(arguments)
                check(name, result['status'] == expected and result['remote_outcome'] == 'not_run', result)
            check('unsupported TLS returns before direct or proxy connection attempts',
                  result['proxy']['status'] == 'not_run' and not result['proxy']['direct_attempted'] and
                  not result['proxy']['probed_ports'], result)
            for kind in ('http', 'socks5'):
                budget()
                with fixture(echo) as (endpoint, target_evidence):
                    with proxy_fixture(kind, endpoint) as (uri, proxy_evidence):
                        result = client.call(dict(endpoint, proxy=uri, action='exchange', allow_send=True, payload_hex=payload.hex()))
                check(f'{kind} native tunnel carries exact binary TCP bytes to the selected target',
                      result['status'] == 'completed' and bytes.fromhex(result['data_hex']) == payload and
                      target_evidence['received'] == payload and bytes(proxy_evidence['client_data']) == payload, result)
                check(f'{kind} route trace identifies one tunnel without a direct attempt',
                      result['proxy']['status'] == 'completed' and result['proxy']['selected_proxy'] == uri and
                      not result['proxy']['direct_attempted'] and result['proxy']['tunnel_attempts'] == 1 and
                      proxy_evidence['tunnels'] == 1 and result['cleanup_complete'], result)
            with fixture(telnet) as (endpoint, target_evidence):
                with proxy_fixture('http', endpoint) as (uri, proxy_evidence):
                    result = client.call(dict(endpoint, proxy=uri, protocol='telnet', action='exchange', payload_hex='61ff62', allow_send=True))
            check('Telnet framing and negotiation remain intact inside an HTTP CONNECT tunnel',
                  result['status'] == 'completed' and result['data_hex'] == b'ok\xff'.hex() and
                  target_evidence['payload'] == b'a\xff\xffb' and target_evidence['replies'] == b'\xff\xfe\x01\xff\xfc\x03' and
                  proxy_evidence['tunnels'] == 1, result)
            with fixture(broker) as (endpoint, target_evidence):
                with proxy_fixture('socks5', endpoint) as (uri, proxy_evidence):
                    result = client.call(dict(endpoint, proxy=uri, protocol='mqtt', action='publish', topic=topic,
                                              payload_hex=mqtt_payload.hex(), allow_send=True, retain=True))
            packet_type, body = target_evidence['next']
            topic_length = int.from_bytes(body[:2], 'big')
            check('MQTT CONNECT and one QoS0 PUBLISH traverse a SOCKS5 tunnel unchanged',
                  target_evidence['connect'][0] == 0x10 and packet_type == 0x31 and
                  body[2:2+topic_length].decode() == topic and body[2+topic_length:] == mqtt_payload and
                  result['completion'] == 'qos0_sent_not_acknowledged' and result['remote_outcome'] == 'unknown' and
                  proxy_evidence['tunnels'] == 1, result)
            with fixture(refuse) as (endpoint, target_evidence):
                with proxy_fixture('socks5', endpoint) as (uri, proxy_evidence):
                    result = client.call(dict(endpoint, proxy=uri, protocol='mqtt'))
            check('MQTT refusal after a connected tunnel is not retried on another route',
                  result['status'] == 'broker_refused' and result['mqtt_connack'] == 5 and
                  result['proxy']['tunnel_attempts'] == 1 and proxy_evidence['tunnels'] == 1, result)
            with fixture(no_answer) as (endpoint, target_evidence):
                with proxy_fixture('http', endpoint) as (uri, proxy_evidence):
                    result = client.call(dict(endpoint, proxy=uri, action='exchange', payload_hex='61', allow_send=True, timeout_ms=250))
            check('timeout after proxied transmission remains UNKNOWN without tunnel or payload replay',
                  result['status'] == 'timed_out' and result['remote_outcome'] == 'unknown' and target_evidence['write'] == b'a' and
                  bytes(proxy_evidence['client_data']) == b'a' and proxy_evidence['tunnels'] == 1 and
                  result['proxy']['tunnel_attempts'] == 1, result)
            with fixture(lambda peer, evidence, stop: None) as (endpoint, evidence):
                result = client.call(dict(endpoint, proxy='direct'))
            check('explicit direct suppresses all local proxy probing',
                  result['status'] == 'completed' and result['proxy']['selected_proxy'] == 'direct' and
                  result['proxy']['direct_attempted'] and not result['proxy']['probed_ports'], result)
            for name, proxy in [('nonlocal proxy URI', 'http://192.0.2.1:7890'), ('untyped proxy value', ['auto'])]:
                budget()
                result = client.call(dict(base, proxy=proxy))
                check('validation rejects ' + name + ' before any transport attempt', result['status'] == 'validation_error' and
                      not result['proxy']['direct_attempted'] and not result['proxy']['probed_ports'] and
                      result['remote_outcome'] == 'not_run', result)
    finally:
        client.close()
    print(json.dumps({'status': 'PASS', 'checks': len(passed), 'passed': len(passed), 'fixture_cleanup': True}))

if __name__ == '__main__':
    main()
