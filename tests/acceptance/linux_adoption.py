"""Independent standard-library MCP client and bounded Linux lifecycle workload."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import tempfile
import time


START = time.monotonic()
DEADLINE = START + 700


def cancelled(_number, _frame):
    raise InterruptedError('Linux adoption gate cancelled')


def budget():
    if time.monotonic() >= DEADLINE:
        raise TimeoutError('Linux adoption gate exceeded 700 seconds')


def check(receipt, name, condition, **details):
    receipt['checks'].append({'name': name, 'status': 'PASS' if condition else 'FAIL', **details})
    print(('PASS ' if condition else 'FAIL ') + name, flush=True)
    if not condition:
        raise AssertionError(name)


class Client:
    def __init__(self, binary, fixture, receipt):
        self.stderr = open(fixture / 'client-stderr.txt', 'wb')
        self.process = subprocess.Popen([str(binary), '--mcp'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=self.stderr, start_new_session=True)
        self.identity = {'pid': self.process.pid, 'parent_pid': os.getpid(),
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'argv': [str(binary), '--mcp']}
        self.stat = Path('/proc') / str(self.process.pid) / 'stat'
        if self.stat.exists():
            self.identity['start_ticks'] = self.stat.read_text().rsplit(')', 1)[1].split()[19]
        receipt['processes'].append(self.identity)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending = b''
        self.request_id = 0
        self.closed = False
        try:
            initialized = self.request('initialize', {'protocolVersion': '2024-11-05',
                'capabilities': {}, 'clientInfo': {'name': 'independent-python-adoption', 'version': '1'}})
            assert initialized['serverInfo']['name'] == 'Cordonix'
            self.version = initialized['serverInfo']['version']
            self.notify('notifications/initialized', {})
        except Exception:
            self.close(crash=True)
            raise

    def send(self, message):
        budget()
        wire = json.dumps(message, ensure_ascii=False).encode('utf-8') + b'\n'
        if len(wire) > 8192:
            raise ValueError('Fixture request exceeds 8 KiB')
        self.process.stdin.write(wire)
        self.process.stdin.flush()

    def notify(self, method, params):
        self.send({'jsonrpc': '2.0', 'method': method, 'params': params})

    def request(self, method, params):
        self.request_id += 1
        if self.request_id > 2000:
            raise RuntimeError('Fixture exceeded 2000 MCP requests')
        self.send({'jsonrpc': '2.0', 'id': self.request_id, 'method': method, 'params': params})
        deadline = min(DEADLINE, time.monotonic() + 8)
        for _ in range(128):
            if b'\n' in self.pending:
                line, self.pending = self.pending.split(b'\n', 1)
                reply = json.loads(line.decode('utf-8', errors='strict'))
                if reply.get('id') == self.request_id:
                    if 'error' in reply:
                        raise RuntimeError(reply['error'])
                    return reply['result']
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.selector.select(remaining):
                raise TimeoutError('Independent client request exceeded eight seconds')
            block = os.read(self.process.stdout.fileno(), 65536)
            if not block:
                raise RuntimeError('Server closed before the response')
            self.pending += block
            if len(self.pending) > 1048576:
                raise RuntimeError('Independent client reply exceeded 1 MiB')
        raise RuntimeError('Independent client exceeded 128 response reads')

    def call(self, name, arguments):
        result = self.request('tools/call', {'name': name, 'arguments': arguments})
        return json.loads(result['content'][0]['text'])

    def close(self, crash=False):
        if self.closed:
            return
        try:
            if crash:
                os.killpg(self.process.pid, signal.SIGKILL)
            else:
                try:
                    self.process.stdin.close()
                except BrokenPipeError:
                    pass
            self.process.wait(timeout=5)
        finally:
            if self.process.poll() is None:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=3)
            self.identity['exit_code'] = self.process.returncode
            self.identity['cleanup_complete'] = not self.stat.exists()
            self.selector.close()
            try:
                self.process.stdin.close()
            except BrokenPipeError:
                pass
            self.process.stdout.close()
            self.stderr.close()
            self.closed = True


def lifecycle(client, fixture, receipt, label):
    path = fixture / '中文 space.txt'
    original = 'alpha\r\n中文\n'.encode('gbk')
    path.write_bytes(original)
    opened = client.call('workspace_open', {'path': str(path), 'encoding': 'cp936'})
    check(receipt, label + ': CP936 open and strict content', opened['status'] == 'ok')
    document = opened['document_id']
    text = client.call('workspace_read', {'document_id': document, 'raw': True})
    check(receipt, label + ': independent decoded read', text['text'] == 'alpha\r\n中文\n')
    rejected = client.call('workspace_edit', {'document_id': document, 'expected_sha256': opened['memory_sha256'],
        'old': 'alpha', 'new': 'beta', 'expected_count': 1, 'dry_run': False})
    check(receipt, label + ': mixed EOL editing is rejected without changing bytes',
          rejected['status'] == 'mixed_eol' and path.read_bytes() == original)
    client.call('workspace_close', {'document_id': document})
    original = 'alpha\r\n中文\r\n'.encode('gbk')
    path.write_bytes(original)
    opened = client.call('workspace_open', {'path': str(path), 'encoding': 'cp936'})
    document = opened['document_id']
    edited = client.call('workspace_edit', {'document_id': document, 'expected_sha256': opened['memory_sha256'],
        'old': 'alpha', 'new': 'beta', 'expected_count': 1, 'dry_run': False})
    check(receipt, label + ': memory edit leaves real disk unchanged', edited['status'] == 'ok' and
          edited['dirty_in_memory'] and path.read_bytes() == original, result=edited,
          disk_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), expected_sha256=hashlib.sha256(original).hexdigest())
    committed = client.call('workspace_commit', {'document_id': document})
    check(receipt, label + ': explicit commit retains encoding and CRLF',
          committed['status'] == 'ok' and committed['written_to_disk'] and
          path.read_bytes() == 'beta\r\n中文\r\n'.encode('gbk'))
    client.call('workspace_close', {'document_id': document})
    opened = client.call('workspace_open', {'path': str(path), 'encoding': 'cp936'})
    document = opened['document_id']
    client.call('workspace_edit', {'document_id': document, 'expected_sha256': opened['memory_sha256'],
        'old': 'beta', 'new': 'gamma', 'expected_count': 1, 'dry_run': False})
    path.write_bytes(b'foreign writer\n')
    rejected = client.call('workspace_commit', {'document_id': document})
    check(receipt, label + ': real external writer conflict preserves its bytes',
          rejected['status'] == 'file_changed' and not rejected['written_to_disk'] and
          path.read_bytes() == b'foreign writer\n')
    client.call('workspace_close', {'document_id': document, 'discard': True})


def suite(binary, parent, receipt, label, require_env):
    fixture_path = None
    client = None
    with tempfile.TemporaryDirectory(prefix='cordonix-adoption-', dir=parent) as temporary:
        fixture_path = Path(temporary)
        probe = subprocess.Popen(['/usr/bin/findmnt', '-n', '-T', str(fixture_path), '-o', 'FSTYPE'],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        process_record = {'pid': probe.pid, 'parent_pid': os.getpid(), 'argv': probe.args,
                          'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        stat_path = Path('/proc') / str(probe.pid) / 'stat'
        if stat_path.exists():
            process_record['start_ticks'] = stat_path.read_text().rsplit(')', 1)[1].split()[19]
        receipt['processes'].append(process_record)
        try:
            output, error = probe.communicate(timeout=3)
            if probe.returncode:
                raise RuntimeError(error.decode('utf-8'))
            filesystem = output.decode('utf-8').strip()
            receipt.setdefault('filesystems', []).append({'label': label, 'actual_type': filesystem})
            if label in ('ext4', 'tmpfs'):
                check(receipt, label + ': actual filesystem type', filesystem == label)
        finally:
            if probe.poll() is None:
                os.killpg(probe.pid, signal.SIGKILL)
            probe.wait(timeout=3)
            process_record.update(exit_code=probe.returncode, cleanup_complete=not stat_path.exists())
        client = Client(binary, fixture_path, receipt)
        try:
            receipt.setdefault('versions', []).append({'fixture': label, 'version': client.version})
            tools = client.request('tools/list', {})['tools']
            if len(tools) > 64:
                raise RuntimeError('Tool list exceeds the independent fixture limit')
            named = {item['name']: item for item in tools}
            check(receipt, label + ': independent MCP client and readable protocol titles',
                  'execute_program' in named and all(item.get('title') for item in tools))
            lifecycle(client, fixture_path, receipt, label)
            literal = '中文 "space" $(not-a-shell)\n'
            execution = client.call('execute_program', {'program': '/usr/bin/printf',
                'cwd': str(fixture_path), 'args': ['%s', literal], 'timeout_ms': 1000,
                'expected_exit_codes': [0]})
            check(receipt, label + ': real native program receives literal Unicode argv',
                  execution['status'] == 'completed' and execution['stdout'] == literal and
                  execution['cleanup_complete'])
            if 'create_file' in named:
                new_file = fixture_path / 'created.txt'
                created = client.call('create_file', {'path': str(new_file), 'content': 'new\r\n中文',
                    'encoding': 'utf-8-bom'})
                expected = b'\xef\xbb\xbf' + 'new\r\n中文'.encode('utf-8')
                check(receipt, label + ': actual new-file capability bytes and hash',
                      created['status'] == 'ok' and created['written_to_disk'] and
                      new_file.read_bytes() == expected and
                      created['disk_sha256'] == hashlib.sha256(expected).hexdigest())
            else:
                receipt['checks'].append({'name': label + ': new-file capability', 'status': 'NOT_RUN',
                                          'reason': 'This installed version does not expose create_file'})
            if require_env:
                execution = client.call('execute_program', {'program': '/usr/bin/printenv',
                    'args': ['CDX_ACCEPTANCE_LITERAL'], 'cwd': str(fixture_path), 'timeout_ms': 1000,
                    'env': {'CDX_ACCEPTANCE_LITERAL': '中文 $(literal)'}, 'expected_exit_codes': [0]})
                check(receipt, label + ': child environment is literal', execution['status'] == 'completed'
                      and execution['stdout'] == '中文 $(literal)\n')
                inherited = client.call('execute_program', {'program': '/usr/bin/printenv',
                    'args': ['CDX_ACCEPTANCE_LITERAL'], 'cwd': str(fixture_path), 'timeout_ms': 1000,
                    'expected_exit_codes': [1]})
                check(receipt, label + ': later child and parent environment are unchanged',
                      inherited['status'] == 'completed' and inherited['exit_code'] == 1 and
                      inherited['stdout'] == '' and 'CDX_ACCEPTANCE_LITERAL' not in os.environ)
        finally:
            client.close()
        check(receipt, label + ': EOF reclaims the independent MCP process',
              client.identity['cleanup_complete'] and client.identity['exit_code'] == 0)
    check(receipt, label + ': fixture removed', fixture_path is not None and not fixture_path.exists())


def crash_boundary(binary, receipt):
    with tempfile.TemporaryDirectory(prefix='cordonix-adoption-', dir='/tmp') as temporary:
        fixture = Path(temporary)
        path = fixture / 'crash.txt'
        path.write_bytes(b'uncommitted-before-crash\n')
        client = Client(binary, fixture, receipt)
        try:
            opened = client.call('workspace_open', {'path': str(path), 'encoding': 'utf-8'})
            edited = client.call('workspace_edit', {'document_id': opened['document_id'],
                'expected_sha256': opened['memory_sha256'], 'old': 'before', 'new': 'after',
                'expected_count': 1, 'dry_run': False})
            check(receipt, 'crash fixture has dirty memory', edited['dirty_in_memory'])
        finally:
            client.close(crash=True)
        check(receipt, 'process crash discards uncommitted memory without an implicit write',
              client.identity['exit_code'] == -signal.SIGKILL and
              path.read_bytes() == b'uncommitted-before-crash\n')


def soak(binary, receipt, rounds, seconds):
    with tempfile.TemporaryDirectory(prefix='cordonix-adoption-', dir='/tmp') as temporary:
        fixture = Path(temporary)
        path = fixture / 'soak.txt'
        path.write_bytes(b'0\n')
        client = Client(binary, fixture, receipt)
        started = time.monotonic()
        samples = []
        try:
            for index in range(rounds):
                budget()
                opened = client.call('workspace_open', {'path': str(path), 'encoding': 'utf-8'})
                document = opened['document_id']
                client.call('workspace_edit', {'document_id': document,
                    'expected_sha256': opened['memory_sha256'], 'old': str(index),
                    'new': str(index + 1), 'expected_count': 1, 'dry_run': False})
                before_commit = client.call('read_file', {'path': str(path), 'raw': True})
                assert before_commit['text'] == str(index) + '\n'
                client.call('workspace_commit', {'document_id': document})
                client.call('workspace_close', {'document_id': document})
                assert path.read_bytes() == (str(index + 1) + '\n').encode('ascii')
                info = client.call('tool_info', {})
                assert info['document_loads'] == (index + 1) * 2
                status = Path('/proc') / str(client.process.pid) / 'status'
                rss = next(line for line in status.read_text().splitlines() if line.startswith('VmRSS:'))
                samples.append({'round': index + 1, 'elapsed_seconds': time.monotonic() - started,
                                'rss': rss, 'requests': client.request_id})
                target = started + seconds * (index + 1) / rounds
                remaining = min(target - time.monotonic(), DEADLINE - time.monotonic())
                if remaining > 0:
                    time.sleep(min(remaining, 15))
                if (index + 1) % 6 == 0:
                    print(json.dumps({'soak_round': index + 1, 'rounds': rounds,
                        'elapsed_seconds': round(time.monotonic() - started, 1)}), flush=True)
        finally:
            client.close()
        elapsed = time.monotonic() - started
        receipt['soak'] = {'rounds': rounds, 'requested_seconds': seconds,
                          'elapsed_seconds': elapsed, 'samples': samples,
                          'scope': 'One Ubuntu WSL2 process on ext4; no longer-duration or other-distribution claim'}
        check(receipt, 'bounded persistent-process workload completes its rounds and duration',
              len(samples) == rounds and elapsed >= seconds and client.identity['cleanup_complete'])


def main():
    signal.signal(signal.SIGINT, cancelled)
    signal.signal(signal.SIGTERM, cancelled)
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--pilot', action='store_true')
    parser.add_argument('--require-env', action='store_true')
    parser.add_argument('--soak-seconds', type=int, default=0)
    parser.add_argument('--rounds', type=int, default=60)
    args = parser.parse_args()
    if not 0 <= args.soak_seconds <= 600 or not 1 <= args.rounds <= 60 or args.soak_seconds > args.rounds * 15:
        raise ValueError('Soak must use 1..60 rounds, 0..600 seconds and at most 15 seconds per round')
    receipt = {'status': 'FAIL', 'checks': [], 'processes': [],
               'client': 'Independent Python standard-library MCP client',
               'official_linux_sdk': 'NOT_RUN', 'other_distributions': 'NOT_RUN',
               'title_ui_rendering': 'UNKNOWN', 'pilot': args.pilot}
    try:
        binary = args.binary.resolve(strict=True)
        receipt.update(binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        suite(binary, Path('/tmp'), receipt, 'ext4', args.require_env)
        if not args.pilot:
            suite(binary, Path('/dev/shm'), receipt, 'tmpfs', args.require_env)
            suite(binary, args.receipt.parent, receipt, 'checkout-filesystem', args.require_env)
            crash_boundary(binary, receipt)
            if args.soak_seconds:
                soak(binary, receipt, args.rounds, args.soak_seconds)
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = type(error).__name__ + ': ' + str(error)
        print(receipt['error'], flush=True)
    finally:
        receipt['cleanup_complete'] = all(item.get('cleanup_complete', False) for item in receipt['processes'])
        args.receipt.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': receipt['status'], 'checks': len(receipt['checks']),
                          'cleanup_complete': receipt['cleanup_complete']}), flush=True)
    return 0 if receipt['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
