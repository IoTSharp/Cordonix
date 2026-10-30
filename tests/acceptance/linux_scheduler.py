"""Observe the installer's real hourly timer in bounded, resumable stages."""

import argparse
import datetime
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time


START = time.monotonic()
DEADLINE = START + 205
ROOT = Path(__file__).resolve().parents[2]


def cancelled(_number, _frame):
    raise InterruptedError('Scheduler acceptance cancelled')


def command(state, argv, seconds=10, env=None, allow_failure=False, deadline=None):
    remaining = (DEADLINE if deadline is None else deadline) - time.monotonic()
    if remaining <= 0:
        raise TimeoutError('Scheduler stage exceeded its wall-clock budget')
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True, env=env)
    identity = {'pid': process.pid, 'parent_pid': os.getpid(), 'argv': argv,
                'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    stat_path = Path('/proc') / str(process.pid) / 'stat'
    if stat_path.exists():
        identity['start_ticks'] = stat_path.read_text().rsplit(')', 1)[1].split()[19]
    state.setdefault('processes', []).append(identity)
    try:
        stdout, stderr = process.communicate(timeout=min(seconds, remaining))
        identity['exit_code'] = process.returncode
        if len(stdout) + len(stderr) > 1048576:
            raise RuntimeError('Acceptance command exceeded the 1 MiB diagnostic bound')
        record = {'argv': argv, 'exit_code': process.returncode,
                  'stdout': stdout.decode('utf-8', errors='strict'),
                  'stderr': stderr.decode('utf-8', errors='strict')}
        state.setdefault('commands', []).append(record)
        if process.returncode and not allow_failure:
            raise RuntimeError('Command failed: ' + record['stderr'][:1000])
        return record
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=3)
        identity['cleanup_complete'] = not stat_path.exists()


def properties(state, unit, names):
    result = command(state, ['/usr/bin/systemctl', '--user', 'show', unit,
                             '--property=' + ','.join(names)])
    return dict(line.split('=', 1) for line in result['stdout'].splitlines() if '=' in line)


def snapshot(state):
    timer = properties(state, state['unit'] + '.timer', [
        'ActiveState', 'UnitFileState', 'LastTriggerUSec', 'LastTriggerUSecMonotonic',
        'NextElapseUSecMonotonic', 'TimersMonotonic', 'FragmentPath'])
    service = properties(state, state['unit'] + '.service', [
        'ActiveState', 'Result', 'ExecMainStatus', 'ExecMainStartTimestamp',
        'ExecMainStartTimestampMonotonic', 'InvocationID', 'MainPID', 'ControlGroup',
        'TimeoutStartUSec', 'KillMode', 'FragmentPath'])
    value = {'observed_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'timer': timer, 'service': service}
    state.setdefault('snapshots', []).append(value)
    return value


def pilot(state):
    result = command(state, ['/usr/bin/systemctl', '--user', 'is-system-running'],
                     allow_failure=True)
    if result['stdout'].strip() not in ('running', 'degraded'):
        state.update(status='NOT_RUN', reason='A running systemd user manager is required')
        return 77
    state.update(status='PASS', user_id=os.getuid(),
                 manager=result['stdout'].strip(),
                 bounds={'maximum_snapshots': 90, 'session_seconds': 4500,
                         'stage_seconds': 205, 'cleanup_seconds': 60, 'service_seconds': 190},
                 natural_trigger='No service start command is issued by this gate')
    return 0


def start(state):
    if pilot(state):
        return 77
    root = Path(tempfile.mkdtemp(prefix='cordonix-scheduler-', dir='/tmp')).resolve()
    state.update(status='STARTING', root=str(root), cleanup_complete=False,
                 started_epoch=time.time(), installer_sha256=hashlib.sha256(
                     (ROOT / 'install/install.sh').read_bytes()).hexdigest())
    (root / 'acceptance-owner.json').write_text(json.dumps({'uid': os.getuid(),
        'started_epoch': state['started_epoch']}), encoding='utf-8')
    state['unit'] = 'cordonix-update-' + hashlib.sha256(os.fsencode(root)).hexdigest()[:12]
    argv = ['/bin/sh', str(ROOT / 'install/install.sh'), '--root', str(root),
            '--source', 'auto', '--enable-auto-update']
    result = command(state, argv, seconds=185, allow_failure=True)
    state['installer'] = json.loads(result['stdout'].splitlines()[-1])
    if result['exit_code']:
        state.update(status='FAIL', reason='Installer did not complete scheduling; inspect its receipt')
        return 1
    units = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'systemd/user'
    state['unit_directory'] = str(units)
    state['timer_definition'] = (units / (state['unit'] + '.timer')).read_text(encoding='utf-8')
    state['service_definition'] = (units / (state['unit'] + '.service')).read_text(encoding='utf-8')
    assert 'OnBootSec=5min\nOnUnitActiveSec=1h\n' in state['timer_definition']
    assert 'TimeoutStartSec=190\nKillMode=control-group\n' in state['service_definition']
    value = snapshot(state)
    assert value['timer']['ActiveState'] == 'active'
    assert value['timer']['UnitFileState'] == 'enabled'
    state.update(status='WAITING', registration='PASS', original_hourly_definition='PASS',
                 natural_first_trigger='NOT_RUN', natural_hourly_recurrence='NOT_RUN')
    return 0


def status(state):
    observation_limit = state.get('bounds', {}).get('session_seconds', 4500)
    if observation_limit not in (4500, 7200):
        raise ValueError('Observation limit must be 75 minutes or two hours')
    if len(state.get('snapshots', [])) >= 90 or time.time() - state['started_epoch'] > observation_limit:
        state.update(status='NOT_RUN', reason='Natural recurrence observation budget expired')
        cleanup(state)
        return 77
    value = snapshot(state)
    span = value['timer'].get('LastTriggerUSecMonotonic', '0')
    units = {'us': 1, 'ms': 1000, 's': 1000000, 'min': 60000000,
             'h': 3600000000, 'd': 86400000000, 'w': 604800000000}
    parts = re.findall(r'(\d+(?:\.\d+)?)(us|ms|min|s|h|d|w)', span)
    if span != '0' and (not parts or len(parts) > 7 or ''.join(a + b for a, b in parts) != span.replace(' ', '')):
        raise ValueError('Unexpected systemd monotonic timespan: ' + span)
    trigger = sum(int(Decimal(number) * units[unit]) for number, unit in parts)
    state['status'] = 'FAIL' if state.get('service_failure_observed') else 'WAITING'
    if 'error' in state:
        state.setdefault('stage_errors', []).append(state.pop('error'))
    completed = value['service']['ActiveState'] in ('inactive', 'failed')
    successful = completed and value['service']['Result'] == 'success' and value['service']['ExecMainStatus'] == '0'
    if trigger and 'first_trigger_us' not in state:
        state['first_trigger_us'] = trigger
        state['first_trigger_utc'] = value['timer']['LastTriggerUSec']
    if trigger:
        state['natural_first_trigger'] = 'PASS'
        state['latest_natural_service_result'] = 'PASS' if successful else 'FAIL' if completed else 'RUNNING'
        if completed and not successful:
            state['service_failure_observed'] = True
            state['status'] = 'FAIL'
    if trigger and trigger - state['first_trigger_us'] >= 3600 * 1000000:
        state['natural_hourly_recurrence'] = 'PASS'
        state['hourly_interval_seconds'] = (trigger - state['first_trigger_us']) / 1000000
        state['status'] = 'PASS' if successful else 'FAIL' if completed else 'WAITING'
    if state.get('service_failure_observed'):
        state['status'] = 'FAIL'
    state['journal'] = command(state, ['/usr/bin/journalctl', '--user', '-u', state['unit'] + '.service',
        '--since', '@' + str(int(state['started_epoch'])), '--no-pager', '-n', '60', '-o', 'short-iso'],
        allow_failure=True)['stdout']
    return 1 if state['status'] == 'FAIL' else 0


def cleanup(state):
    cleanup_deadline = time.monotonic() + 60
    root = Path(state['root']).resolve()
    assert root.parent == Path('/tmp') and root.name.startswith('cordonix-scheduler-')
    marker = root / 'acceptance-owner.json'
    if not marker.exists():
        state['cleanup_complete'] = not root.exists()
        return
    owner = json.loads(marker.read_text(encoding='utf-8'))
    assert owner['uid'] == os.getuid() and owner['started_epoch'] == state['started_epoch']
    if 'unit' in state:
        try:
            command(state, ['/usr/bin/systemctl', '--user', 'stop', state['unit'] + '.service'],
                    seconds=15, allow_failure=True, deadline=cleanup_deadline)
        finally:
            state['disable'] = command(state, ['/bin/sh', str(ROOT / 'install/install.sh'), '--root', str(root),
                '--disable-auto-update'], seconds=40, allow_failure=True, deadline=cleanup_deadline)
        units = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'systemd/user'
        state['units_removed'] = all(not (units / (state['unit'] + suffix)).exists()
                                     for suffix in ('.service', '.timer'))
        if not state['units_removed']:
            raise RuntimeError('Owned user unit files remain; preserving installation root')
    shutil.rmtree(root)
    state['cleanup_complete'] = not root.exists() and state.get('units_removed', True)


def main():
    signal.signal(signal.SIGTERM, cancelled)
    signal.signal(signal.SIGINT, cancelled)
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('pilot', 'start', 'status', 'cleanup'))
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--observation-limit-seconds', type=int, choices=(4500, 7200))
    args = parser.parse_args()
    if args.mode in ('pilot', 'start') and args.receipt.exists():
        raise ValueError('Use a new receipt for a new scheduler acceptance run')
    state = json.loads(args.receipt.read_text()) if args.receipt.exists() else {'status': 'NOT_RUN'}
    code = 1
    try:
        if args.mode == 'pilot':
            code = pilot(state)
        elif args.mode == 'start':
            code = start(state)
        elif args.mode == 'status':
            if args.observation_limit_seconds is not None:
                state.setdefault('observation_adjustments', []).append({
                    'previous_seconds': state.get('bounds', {}).get('session_seconds', 4500),
                    'new_seconds': args.observation_limit_seconds,
                    'observed_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()})
                state.setdefault('bounds', {})['session_seconds'] = args.observation_limit_seconds
            code = status(state)
        else:
            cleanup(state)
            code = 0 if state['cleanup_complete'] else 1
    except Exception as error:
        state.update(status='FAIL', error=type(error).__name__ + ': ' + str(error))
    finally:
        if args.mode == 'start' and state['status'] not in ('WAITING', 'NOT_RUN') and 'root' in state:
            try:
                cleanup(state)
            except Exception as error:
                state['cleanup_error'] = str(error)
        args.receipt.write_text(json.dumps(state, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({key: state.get(key) for key in ('status', 'root', 'unit', 'registration',
            'natural_first_trigger', 'natural_hourly_recurrence', 'latest_natural_service_result',
            'cleanup_complete', 'reason', 'error')}, ensure_ascii=False), flush=True)
    return code


if __name__ == '__main__':
    raise SystemExit(main())
