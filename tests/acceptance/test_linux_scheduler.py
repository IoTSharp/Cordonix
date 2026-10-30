"""Regress real failed-service reporting and cleanup after an expired stage."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import tempfile
import time

import linux_scheduler as gate


def main():
    signal.signal(signal.SIGINT, gate.cancelled)
    signal.signal(signal.SIGTERM, gate.cancelled)
    parser = argparse.ArgumentParser()
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    state = {'status': 'FAIL', 'checks': []}
    root = None
    code = 1
    try:
        if gate.pilot(state):
            return 77
        root = Path(tempfile.mkdtemp(prefix='cordonix-scheduler-', dir='/tmp')).resolve()
        state.update(root=str(root), started_epoch=time.time(), cleanup_complete=False)
        (root / 'acceptance-owner.json').write_text(json.dumps({'uid': os.getuid(),
            'started_epoch': state['started_epoch']}), encoding='utf-8')
        state['unit'] = 'cordonix-update-' + hashlib.sha256(os.fsencode(root)).hexdigest()[:12]
        directory = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'systemd/user'
        marker = '# Cordonix installer: ' + str(root) + '\n'
        service = directory / (state['unit'] + '.service')
        timer = directory / (state['unit'] + '.timer')
        service.write_text(marker + '[Service]\nType=oneshot\nTimeoutStartSec=10\nKillMode=control-group\nExecStart=/usr/bin/false\n', encoding='utf-8')
        timer.write_text(marker + '[Timer]\nOnActiveSec=1s\nAccuracySec=100ms\n[Install]\nWantedBy=timers.target\n', encoding='utf-8')
        gate.command(state, ['/usr/bin/systemctl', '--user', 'daemon-reload'])
        gate.command(state, ['/usr/bin/systemctl', '--user', 'enable', '--now', state['unit'] + '.timer'])
        deadline = time.monotonic() + 20
        for _ in range(20):
            if time.monotonic() >= deadline:
                raise TimeoutError('Failed-service fixture did not trigger within 20 seconds')
            result = gate.status(state)
            if state.get('latest_natural_service_result') == 'FAIL':
                assert result == 1 and state['status'] == 'FAIL'
                state['checks'].append({'name': 'Actual failed natural service returns exit 1', 'status': 'PASS'})
                break
            time.sleep(0.5)
        else:
            raise RuntimeError('Failed-service fixture exceeded 20 observations')
        service.write_text(marker + '[Service]\nType=oneshot\nTimeoutStartSec=10\nKillMode=control-group\nExecStart=/usr/bin/true\n', encoding='utf-8')
        gate.command(state, ['/usr/bin/systemctl', '--user', 'daemon-reload'])
        gate.command(state, ['/usr/bin/systemctl', '--user', 'restart', state['unit'] + '.timer'])
        deadline = time.monotonic() + 20
        for _ in range(20):
            if time.monotonic() >= deadline:
                raise TimeoutError('Success-after-failure fixture did not trigger within 20 seconds')
            result = gate.status(state)
            if state.get('latest_natural_service_result') == 'PASS':
                assert result == 1 and state['status'] == 'FAIL' and state['service_failure_observed']
                state['checks'].append({'name': 'Later natural service success preserves the earlier FAIL outcome', 'status': 'PASS'})
                break
            time.sleep(0.5)
        else:
            raise RuntimeError('Success-after-failure fixture exceeded 20 observations')
        gate.DEADLINE = time.monotonic() - 1
        gate.cleanup(state)
        assert state['cleanup_complete'] and not service.exists() and not timer.exists() and not root.exists()
        state['checks'].append({'name': 'Expired execution budget still permits real owned-unit cleanup', 'status': 'PASS'})
        state['status'] = 'PASS'
        code = 0
    except Exception as error:
        state.update(status='FAIL', error=type(error).__name__ + ': ' + str(error))
    finally:
        if root is not None and root.exists():
            try:
                gate.cleanup(state)
            except Exception as error:
                state['cleanup_error'] = str(error)
        args.receipt.write_text(json.dumps(state, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'status': state['status'], 'checks': state['checks'],
                          'cleanup_complete': state.get('cleanup_complete')}))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
