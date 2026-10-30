#!/bin/sh
set -eu

exec timeout --kill-after=10s 60s python3 - "$@" <<'PY'
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import stat
import struct
import subprocess
import tempfile
import time


def cancelled(signum, frame):
    raise InterruptedError('Filesystem gate cancelled')


class Mcp:
    def __init__(self, binary, directory, processes):
        self.errors = open(directory / 'mcp-stderr.txt', 'wb')
        self.process = subprocess.Popen([str(binary), '--mcp'], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.errors,
                                        start_new_session=True)
        processes.append({'pid': self.process.pid, 'parent_pid': os.getpid(),
                          'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                          'args': [str(binary), '--mcp']})
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending = b''
        self.request_id = 0

    def request(self, method, params):
        self.request_id += 1
        if self.request_id > 32:
            raise RuntimeError('MCP request count exceeds the fixture budget')
        request = {'jsonrpc': '2.0', 'id': self.request_id, 'method': method, 'params': params}
        self.process.stdin.write(json.dumps(request).encode('utf-8') + b'\n')
        self.process.stdin.flush()
        deadline = time.monotonic() + 5
        for _ in range(64):
            if b'\n' in self.pending:
                line, self.pending = self.pending.split(b'\n', 1)
                response = json.loads(line.decode('utf-8', errors='strict'))
                if response.get('id') == self.request_id:
                    if 'error' in response:
                        raise RuntimeError(response['error'])
                    return response['result']
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.selector.select(remaining):
                raise TimeoutError('MCP response exceeded five seconds')
            block = os.read(self.process.stdout.fileno(), 65536)
            if not block:
                raise RuntimeError('MCP closed stdout before its response')
            self.pending += block
            if len(self.pending) > 65536:
                raise RuntimeError('MCP response exceeded 64 KiB')
        raise RuntimeError('MCP response exceeded the read iteration budget')

    def call(self, name, arguments):
        result = self.request('tools/call', {'name': name, 'arguments': arguments})
        return json.loads(result['content'][0]['text'])

    def close(self):
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.process.wait(timeout=3)
        finally:
            self.selector.close()
            self.process.stdout.close()
            self.errors.close()
        return self.process.returncode == 0


def check(receipt, name, condition, **detail):
    receipt['checks'].append({'name': name, 'status': 'PASS' if condition else 'FAIL', **detail})
    print(('PASS ' if condition else 'FAIL ') + name, flush=True)
    if not condition:
        raise AssertionError(name)


def test_names(client, directory, receipt):
    entries = directory / 'entries'
    entries.mkdir()
    text = entries / '中文 space.txt'
    text.write_text('fixture\n', encoding='utf-8')
    result = client.call('list_directory', {'path': str(entries), 'include_hidden': True, 'max_entries': 10})
    check(receipt, 'ext4 Unicode directory entry roundtrips', result['status'] == 'ok' and
          any(item['name'] == text.name for item in result['entries']))
    invalid = os.fsencode(entries) + b'/invalid-\xff'
    descriptor = os.open(invalid, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(descriptor)
    check(receipt, 'ext4 stores the actual invalid UTF8 filename', b'invalid-\xff' in os.listdir(os.fsencode(entries)))
    result = client.call('list_directory', {'path': str(entries), 'include_hidden': True, 'max_entries': 10})
    check(receipt, 'MCP rejects invalid UTF8 OS entry without replacement', result['status'] == 'encoding_error')
    os.unlink(invalid)


def test_metadata(client, directory, receipt):
    path = directory / 'metadata.txt'
    path.write_bytes(b'old\r\n')
    path.chmod(0o640)
    os.setxattr(path, 'user.cordonix_fixture', b'owned fixture')
    # Linux POSIX ACL xattr ABI: version 2, little-endian tag/permissions/id entries.
    acl = struct.pack('<I', 2) + b''.join(struct.pack('<HHI', *entry) for entry in (
        (0x01, 6, 0xffffffff), (0x02, 4, 65534), (0x04, 4, 0xffffffff),
        (0x10, 4, 0xffffffff), (0x20, 0, 0xffffffff)))
    os.setxattr(path, 'system.posix_acl_access', acl)
    os.utime(path, ns=(1_000_000_000, 1_000_000_000))
    before = path.stat()
    check(receipt, 'ext4 ACL and user xattr fixtures are present',
          os.getxattr(path, 'system.posix_acl_access') == acl and
          os.getxattr(path, 'user.cordonix_fixture') == b'owned fixture')
    opened = client.call('workspace_open', {'path': str(path), 'encoding': 'utf-8'})
    check(receipt, 'MCP opens the metadata fixture', opened['status'] == 'ok')
    document = opened['document_id']
    edited = client.call('workspace_edit', {'document_id': document, 'expected_sha256': opened['memory_sha256'],
                         'old': 'old', 'new': 'new', 'expected_count': 1, 'dry_run': False})
    check(receipt, 'memory edit preserves disk bytes and metadata', edited['status'] == 'ok' and
          path.read_bytes() == b'old\r\n' and os.getxattr(path, 'system.posix_acl_access') == acl)
    committed = client.call('workspace_commit', {'document_id': document})
    after = path.stat()
    check(receipt, 'MCP commit replaces bytes and cleans temporary file', committed['status'] == 'ok' and
          committed['written_to_disk'] and path.read_bytes() == b'new\r\n' and
          not list(directory.glob('metadata.txt.cdx.tmp.*')))
    check(receipt, 'ext4 commit preserves owner group and mode',
          (before.st_uid, before.st_gid, stat.S_IMODE(before.st_mode)) ==
          (after.st_uid, after.st_gid, stat.S_IMODE(after.st_mode)), mode=oct(stat.S_IMODE(after.st_mode)))
    check(receipt, 'ext4 commit updates write time', after.st_mtime_ns != before.st_mtime_ns)
    attributes = os.listxattr(path)
    check(receipt, 'documented ACL non-preservation is observed', 'system.posix_acl_access' not in attributes,
          preservation='NOT_SUPPORTED')
    check(receipt, 'documented xattr non-preservation is observed', 'user.cordonix_fixture' not in attributes,
          preservation='NOT_SUPPORTED')
    client.call('workspace_close', {'document_id': document, 'discard': False})
    opened = client.call('workspace_open', {'path': str(path), 'encoding': 'utf-8'})
    document = opened['document_id']
    client.call('workspace_edit', {'document_id': document, 'expected_sha256': opened['memory_sha256'],
                'old': 'new', 'new': 'later', 'expected_count': 1, 'dry_run': False})
    path.chmod(0o600)
    rejected = client.call('workspace_commit', {'document_id': document})
    check(receipt, 'external mode change rejects commit and preserves disk', rejected['status'] == 'file_changed' and
          not rejected['written_to_disk'] and path.read_bytes() == b'new\r\n' and stat.S_IMODE(path.stat().st_mode) == 0o600)
    client.call('workspace_close', {'document_id': document, 'discard': True})


def main():
    signal.signal(signal.SIGTERM, cancelled)
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--fixture-parent', type=Path, default=Path('/tmp'))
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args()
    receipt = {'status': 'FAIL', 'pilot': args.pilot, 'checks': [], 'processes': [], 'cleanup_complete': False}
    client = None
    fixture = None
    try:
        binary = args.binary.resolve(strict=True)
        receipt['binary'] = str(binary)
        receipt['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(prefix='cordonix-ext4-', dir=args.fixture_parent) as temporary:
            fixture = Path(temporary)
            filesystem = subprocess.run(['findmnt', '-n', '-T', str(fixture), '-o', 'FSTYPE'],
                                        check=True, capture_output=True, text=True, timeout=3).stdout.strip()
            receipt['filesystem'] = filesystem
            receipt['fixture'] = str(fixture)
            if filesystem != 'ext4':
                receipt['status'] = 'NOT_RUN'
                receipt['reason'] = 'This gate requires actual ext4 fixtures.'
                return 77
            client = Mcp(binary, fixture, receipt['processes'])
            try:
                initialized = client.request('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                             'clientInfo': {'name': 'cdx-ext4-gate', 'version': '1'}})
                check(receipt, 'real MCP initialize on Linux', initialized['serverInfo']['name'] == 'Cordonix')
                receipt['version'] = initialized['serverInfo']['version']
                test_names(client, fixture, receipt)
                if not args.pilot:
                    test_metadata(client, fixture, receipt)
            finally:
                clean_exit = client.close()
                client = None
                check(receipt, 'owned MCP exits cleanly on EOF', clean_exit)
            receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = type(error).__name__ + ': ' + str(error)
        print(receipt['error'], flush=True)
    finally:
        receipt['cleanup_complete'] = fixture is not None and not fixture.exists() and client is None
        if args.receipt:
            args.receipt.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': receipt['status'], 'checks': len(receipt['checks']),
                          'cleanup_complete': receipt['cleanup_complete']}), flush=True)
    return 0 if receipt['status'] == 'PASS' else 1


raise SystemExit(main())
PY
