"""Real SSH authentication/execution through native HTTP CONNECT and SOCKS5."""
import importlib.util
import json
from pathlib import Path
import select
import socket
import tempfile
import time

spec = importlib.util.spec_from_file_location('ssh_fixture', Path(__file__).with_name('test_ssh.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
import sys


def exact(sock, size):
    result = bytearray()
    for _ in range(size + 8):
        fixture.budget()
        part = sock.recv(size-len(result))
        if not part:
            raise EOFError()
        result.extend(part)
        if len(result)==size:
            return bytes(result)
    raise TimeoutError('proxy receive bound')


def serve_proxy(listener, kind, target_port):
    listener.settimeout(.1)
    for _ in range(64):
        fixture.budget()
        try:
            client, _ = listener.accept()
        except socket.timeout:
            continue
        fixture.worker(tunnel, client, kind, target_port)


def tunnel(client, kind, target_port):
    peer = None
    try:
        client.settimeout(2)
        if kind=='http':
            request=bytearray()
            for _ in range(4096):
                fixture.budget()
                request.extend(exact(client,1))
                if request.endswith(b'\r\n\r\n'):
                    break
            if not request.startswith(('CONNECT 127.0.0.1:'+str(target_port)+' HTTP/1.1\r\n').encode()):
                raise RuntimeError('unexpected CONNECT destination')
            peer=socket.create_connection(('127.0.0.1',target_port),timeout=2)
            client.sendall(b'HTTP/1.1 200 Connection Established\r\n\r\n')
        else:
            if exact(client,3)!=b'\x05\x01\x00':
                raise RuntimeError('unexpected SOCKS greeting')
            client.sendall(b'\x05\x00')
            head=exact(client,4)
            if head[:3]!=b'\x05\x01\x00':
                raise RuntimeError('unexpected SOCKS request')
            if head[3]==1:
                address=exact(client,4)
            elif head[3]==3:
                address=exact(client,exact(client,1)[0])
            else:
                raise RuntimeError('unsupported fixture address')
            port=int.from_bytes(exact(client,2),'big')
            if port!=target_port or address not in (b'\x7f\x00\x00\x01',b'127.0.0.1'):
                raise RuntimeError('unexpected SOCKS destination')
            peer=socket.create_connection(('127.0.0.1',port),timeout=2)
            client.sendall(b'\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00')
        for _ in range(15000):
            fixture.budget()
            readable,_,_=select.select((client,peer),(),(),.01)
            for source in readable:
                payload=source.recv(32768)
                if not payload:
                    return
                (peer if source is client else client).sendall(payload)
    finally:
        if peer is not None:
            peer.close()
        client.close()


def main():
    listeners=[]
    client=None
    aliases=['cordonix-ssh-fixture-'+fixture.uuid.uuid4().hex]
    results=[]
    def listener():
        sock=socket.socket(); sock.bind(('127.0.0.1',0)); sock.listen(16); listeners.append(sock); return sock
    def check(name, condition, result):
        results.append(bool(condition)); print(('PASS ' if condition else 'FAIL ')+name,flush=True)
        if not condition:
            raise AssertionError(result)
    with tempfile.TemporaryDirectory(prefix='cordonix-ssh-proxy-') as cwd:
        try:
            server=listener(); port=server.getsockname()[1]
            fixture.store(aliases[0],'fixture',fixture.PASSWORD)
            fixture.worker(fixture.listen,server,cwd)
            http=listener(); socks=listener()
            fixture.worker(serve_proxy,http,'http',port)
            fixture.worker(serve_proxy,socks,'socks5',port)
            client=fixture.Client(Path(sys.argv[1]).resolve())
            request={'action':'execute','target':{'host':'127.0.0.1','port':port,'credential':aliases[0],
                      'host_key_sha256':fixture.FINGERPRINT},'script':"printf '%s' '中文 $()'\n",'timeout_ms':5000}
            for mode,sock in [('http',http),('socks5',socks)]:
                fixture.budget()
                uri=mode+'://127.0.0.1:'+str(sock.getsockname()[1])
                result=client.call(dict(request,proxy=uri))
                check('SSH executes through '+mode+' proxy',result['status']=='completed' and result['stdout']=='中文 $()' and result['proxy']['selected_proxy']==uri and result['session_allocations_remaining']==0,result)
                bad=dict(request['target'],host_key_sha256='0'*64)
                result=client.call(dict(request,proxy=uri,target=bad))
                check(mode+' preserves host-key pin and does not replay after handshake',result['status']=='host_key_mismatch' and result['remote_outcome']=='not_run' and result['proxy']['tunnel_attempts']==1,result)
            result=client.call(dict(request,proxy='direct'))
            check('explicit direct bypasses proxy probes',result['status']=='completed' and result['proxy']['selected_proxy']=='direct' and result['proxy']['probed_ports']==[],result)
            result=client.call(request)
            check('healthy default auto uses direct without local probes',result['status']=='completed' and result['proxy']['direct_attempted'] and result['proxy']['probed_ports']==[],result)
            client.close()
        finally:
            fixture.STOP.set()
            for sock in listeners:
                sock.close()
            for transport in fixture.TRANSPORTS[:128]:
                transport.close()
            for process in fixture.PROCESSES[:128]:
                if process.poll() is None:
                    process.kill(); process.wait(timeout=3)
            deadline=time.monotonic()+5
            for _ in range(100):
                if not any(t.is_alive() for t in fixture.THREADS) or time.monotonic()>=deadline:
                    break
                for thread in fixture.THREADS[:256]:
                    thread.join(timeout=.01)
            for alias in aliases:
                if not fixture.VAULT.CredDeleteW(alias,1,0) and fixture.ctypes.get_last_error()!=1168:
                    raise fixture.ctypes.WinError(fixture.ctypes.get_last_error())
            cleanup=all(not t.is_alive() for t in fixture.THREADS) and all(p.poll() is not None for p in fixture.PROCESSES)
            print(json.dumps({'checks':len(results),'passed':sum(results),'fixture_cleanup':cleanup}),flush=True)
            if not cleanup:
                raise RuntimeError('fixture cleanup incomplete')


if __name__=='__main__':
    main()
