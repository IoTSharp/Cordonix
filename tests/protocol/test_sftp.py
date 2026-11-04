"""Native SFTP acceptance against a real loopback SSH/SFTP server."""
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location('ssh_fixture', Path(__file__).with_name('test_ssh.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
paramiko = fixture.paramiko
SUBSYSTEMS = []


class Subsystem(paramiko.SFTPServer):
    def __init__(self, *args, **kwargs):
        if len(SUBSYSTEMS) >= 64:
            raise RuntimeError('subsystem thread bound')
        super().__init__(*args, **kwargs)
        SUBSYSTEMS.append(self)


class Files(paramiko.SFTPServerInterface):
    def path(self, name):
        if name not in ('/fixture.bin', '/created.bin', '/lost-open.bin', '/slow.bin'):
            raise OSError(2, 'not found')
        return Path(self.server_root) / name[1:]

    def __init__(self, server, *args, root, **kwargs):
        super().__init__(server, *args, **kwargs)
        self.server_root = root
        self.transport = server.transport

    def stat(self, path):
        try:
            return paramiko.SFTPAttributes.from_stat(self.path(path).stat())
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

    def open(self, path, flags, attr):
        try:
            filename = self.path(path)
            descriptor = os.open(filename, flags | getattr(os, 'O_BINARY', 0), 0o600)
            if path == '/lost-open.bin':
                os.close(descriptor)
                self.transport.close()
                return paramiko.SFTP_FAILURE
            mode = 'wb' if flags & os.O_WRONLY else 'rb'
            file = os.fdopen(descriptor, mode)
            handle = paramiko.SFTPHandle(flags)
            if path == '/slow.bin':
                class Delayed(paramiko.SFTPHandle):
                    def read(self, offset, length):
                        fixture.STOP.wait(2)
                        return super().read(offset, length)
                handle = Delayed(flags)
            handle.readfile = file
            if flags & os.O_WRONLY:
                handle.writefile = file
            return handle
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)


original = fixture.connection
def connection(sock, cwd):
    transport = paramiko.Transport(sock)
    fixture.TRANSPORTS.append(transport)
    transport.add_server_key(fixture.KEY)
    transport.set_subsystem_handler('sftp', Subsystem, Files, root=cwd)
    try:
        server = fixture.Server()
        server.transport = transport
        transport.start_server(server=server)
        channels = []
        for _ in range(15000):
            fixture.budget()
            channel = transport.accept(timeout=.02)
            if channel is not None:
                channels.append(channel)
            if not transport.is_active():
                break
    finally:
        transport.close()


def main():
    fixture.connection = connection
    results = []
    aliases = ['cordonix-ssh-fixture-' + fixture.uuid.uuid4().hex]
    listener = fixture.socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(16)
    client = None
    with tempfile.TemporaryDirectory(prefix='cordonix-sftp-fixture-') as cwd:
        try:
            content = b'\x00\xff\r\n' + '中文 $()'.encode()
            Path(cwd, 'fixture.bin').write_bytes(content)
            Path(cwd, 'slow.bin').write_bytes(content)
            fixture.store(aliases[0], 'fixture', fixture.PASSWORD)
            fixture.worker(fixture.listen, listener, cwd)
            client = fixture.Client(Path(sys.argv[1]))
            def call(arguments, cancel=False):
                client.sequence += 1
                client.send({'jsonrpc':'2.0','id':client.sequence,'method':'tools/call',
                             'params':{'name':'sftp','arguments':arguments}})
                if cancel:
                    time.sleep(.2)
                    client.send({'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':client.sequence}})
                response = client.response()
                return json.loads(response['result']['content'][0]['text'])
            target = {'host':'127.0.0.1','port':listener.getsockname()[1],
                      'credential':aliases[0],'host_key_sha256':fixture.FINGERPRINT}
            def check(name, condition, actual):
                results.append(bool(condition))
                print(('PASS ' if condition else 'FAIL ') + name, flush=True)
                if not condition:
                    raise AssertionError(actual)
            base = {'target':target,'path':'/fixture.bin','timeout_ms':5000}
            result = call(base)
            check('SFTP stat uses the real subsystem', result['status']=='completed' and result['file_size']==len(content), result)
            result = call(dict(base,action='read',read_bytes=4,offset=1))
            check('SFTP bounded binary range preserves bytes', result['status']=='completed' and bytes.fromhex(result['data_hex'])==content[1:5] and result['file_size'] is None, result)
            result = call(dict(base,action='read'))
            check('SFTP reads binary and Unicode bytes through EOF', result['status']=='completed' and bytes.fromhex(result['data_hex'])==content and result['cleanup_complete'], result)
            result = call(dict(base,path='/missing.bin'))
            check('SFTP server rejection is known', result['status']=='sftp_error' and result['remote_outcome']=='known', result)
            write = dict(base,action='write',path='/created.bin',data_hex=content.hex())
            result = call(write)
            check('SFTP writes require explicit flag before dispatch', result['status']=='validation_error' and not Path(cwd,'created.bin').exists(), result)
            result = call(dict(write,allow_write=True))
            check('SFTP creates a new file and returns acknowledged bytes', result['status']=='completed' and result['transferred_bytes']==len(content) and Path(cwd,'created.bin').read_bytes()==content, result)
            result = call(dict(write,allow_write=True,data_hex='deadbeef'))
            check('SFTP never overwrites an existing file', result['status']=='sftp_error' and Path(cwd,'created.bin').read_bytes()==content, result)
            result = call(dict(base,route='interactive'))
            check('SFTP interactive transport boundary is explicit', result['status']=='validation_error', result)
            result = call(dict(write,allow_write=True,path='/lost-open.bin'))
            check('lost OPEN reply after create remains UNKNOWN', result['remote_outcome']=='unknown' and Path(cwd,'lost-open.bin').exists(), result)
            result = call(dict(base,action='read',path='/slow.bin',timeout_ms=500))
            check('opened SFTP handle is freed after timeout', result['status']=='timed_out' and result['remote_outcome']=='unknown' and result['cleanup_complete'] and result['session_allocations_remaining']==0, result)
            result = call(dict(base,action='read',path='/slow.bin'),cancel=True)
            check('opened SFTP handle is freed after MCP cancellation', result['status']=='cancelled' and result['remote_outcome']=='unknown' and result['cleanup_complete'] and result['session_allocations_remaining']==0, result)
            client.close()
        finally:
            fixture.STOP.set()
            listener.close()
            for transport in fixture.TRANSPORTS[:128]:
                transport.close()
            for process in fixture.PROCESSES[:128]:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
            deadline = time.monotonic()+5
            for _ in range(100):
                threads = fixture.THREADS + SUBSYSTEMS
                if not any(t.is_alive() for t in threads) or time.monotonic()>=deadline:
                    break
                for thread in threads[:320]:
                    thread.join(timeout=.01)
            for alias in aliases:
                fixture.VAULT.CredDeleteW(alias,1,0)
            cleanup = all(not t.is_alive() for t in fixture.THREADS + SUBSYSTEMS) and all(p.poll() is not None for p in fixture.PROCESSES)
            print(json.dumps({'checks':len(results),'passed':sum(results),'cleanup_complete':cleanup}),flush=True)
            if not cleanup:
                raise RuntimeError('fixture cleanup failed')


if __name__=='__main__':
    main()
