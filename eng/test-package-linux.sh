#!/bin/sh
set -eu

if [ "${CDX_PACKAGE_TEST_BOUNDED:-0}" != 1 ]; then
    CDX_PACKAGE_TEST_BOUNDED=1
    export CDX_PACKAGE_TEST_BOUNDED
    exec timeout --kill-after=5s 120s sh "$0" "$@"
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=${1:-${CDX_VERSION:-0.1.0-dev}}

exec python3 - "$root" "$version" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time

root, version = Path(sys.argv[1]), sys.argv[2]
if not re.fullmatch(r'[0-9A-Za-z][0-9A-Za-z.+_-]{0,63}', version):
    raise SystemExit('Invalid test package version')
deadline = time.monotonic() + 110
task = Path(tempfile.mkdtemp(prefix='linux-package-test-', dir=root / 'build'))
checks = 0
processes = []
audit = task / 'processes.jsonl'
receipt_path = root / 'build' / 'package-linux-test.json'

def require(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1

def cancel(signum, frame):
    raise KeyboardInterrupt('Linux package test cancelled')

for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
    signal.signal(signum, cancel)

def run(args):
    budget = min(35, deadline - time.monotonic())
    if budget <= 0:
        raise TimeoutError('Linux package test deadline exceeded')
    process = subprocess.Popen(args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    record = {'pid': process.pid, 'parent_pid': os.getpid(), 'started_utc': time.time(), 'program': args[0],
              'args': args[1:], 'cwd': str(root), 'cleanup_complete': False}
    with audit.open('a') as stream:
        stream.write(json.dumps(record) + '\n')
    try:
        stdout, stderr = process.communicate(timeout=budget)
        if len(stdout) + len(stderr) > 1024 * 1024:
            raise AssertionError('Package command output exceeds 1 MiB')
        if process.returncode:
            raise RuntimeError(f'Package command failed ({process.returncode}): {stderr.decode(errors="replace")}')
        return stdout
    finally:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate(timeout=2)
        record['exit_code'] = process.returncode
        record['cleanup_complete'] = process.poll() is not None
        processes.append(record)
        with audit.open('a') as stream:
            stream.write(json.dumps(record) + '\n')

def digest(path, algorithm='sha256'):
    if time.monotonic() >= deadline:
        raise TimeoutError('Linux package test deadline exceeded')
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()

success = False
try:
    first = json.loads(run(['sh', str(root / 'eng/package-linux.sh'), version]))
    archive = Path(first['archive'])
    first_hash = digest(archive)
    second = json.loads(run(['sh', str(root / 'eng/package-linux.sh'), version]))
    require(first['status'] == second['status'] == 'PASS' and first_hash == digest(archive) == second['archive_sha256'],
            'Same inputs must create byte-identical Linux packages')
    require(archive.with_name(archive.name + '.sha256').read_text().strip() == f'{first_hash}  {archive.name}',
            'Archive checksum sidecar differs from the archive')
    extracted = task / '含 空格 的验证目录'
    extracted.mkdir()
    with tarfile.open(archive) as package:
        members = package.getmembers()
        require(1 <= len(members) <= 128, 'Unexpected archive entry count')
        for member in members:
            target = extracted / member.name
            require((member.isfile() or member.isdir()) and member.size <= 33554432 and
                    target.resolve().is_relative_to(extracted.resolve()), 'Unexpected archive entry')
        package.extractall(extracted, filter='data')
    manifest = json.loads((extracted / 'manifest.json').read_text())
    provenance = json.loads((extracted / 'provenance.json').read_text())
    sbom = json.loads((extracted / 'sbom.spdx.json').read_text())
    require(manifest['format_version'] == 1 and manifest['version'] == version and manifest['target'] == 'linux-x64',
            'Invalid Linux package manifest')
    require(1 <= len(manifest['files']) <= 65, 'Payload count exceeds package test limit')
    for record in manifest['files']:
        path = extracted / record['path']
        require(path.resolve().is_relative_to(extracted.resolve()) and path.is_file() and
                path.stat().st_size == record['size_bytes'] and digest(path) == record['sha256'], 'Payload differs from manifest')
    for record in manifest['files']:
        document = extracted / record['path']
        if document.suffix != '.md':
            continue
        for target in re.findall(r'\]\(([^)#]+)(?:#[^)]*)?\)', document.read_text(encoding='utf-8')):
            if re.match(r'^[A-Za-z][A-Za-z0-9+.-]*:', target):
                continue
            linked = (document.parent / target).resolve()
            require(linked.is_relative_to(extracted.resolve()) and linked.exists(),
                    f'Missing packaged link from {record["path"]}: {target}')
    require(digest(extracted / 'LICENSE') == digest(root / 'LICENSE'), 'Packaged license differs from source')
    build_info = json.loads(run([str(extracted / 'cdx'), '--request', str(extracted / 'examples/request.json')]))
    require(provenance['binary_build_revision'] == build_info['build_revision'] and
            provenance['binary_build_dirty'] == build_info['build_dirty'] and provenance['version'] == version and
            re.fullmatch('[0-9a-f]{40}', provenance['checkout_revision_at_packaging']) and
            provenance['checkout_dirty_at_packaging'] == bool(provenance['checkout_status_at_packaging']) and
            provenance['input_executable_sha256'] == digest(extracted / 'cdx'), 'Invalid packaging provenance')
    require(sbom['spdxVersion'] == 'SPDX-2.3' and sbom['dataLicense'] == 'CC0-1.0' and len(sbom['packages']) == 1 and
            len(sbom['files']) == len(manifest['files']) + 2 and len(sbom['relationships']) == len(sbom['files']) + 1,
            'Invalid SPDX structure')
    package = sbom['packages'][0]
    require(package['filesAnalyzed'] and package['versionInfo'] == version and package['licenseDeclared'] == 'Apache-2.0' and
            package['packageVerificationCode']['packageVerificationCodeExcludedFiles'] == ['./sbom.spdx.json'],
            'Invalid SPDX package metadata')
    require(sum(rel == {'spdxElementId': sbom['SPDXID'], 'relationshipType': 'DESCRIBES',
                        'relatedSpdxElement': package['SPDXID']} for rel in sbom['relationships']) == 1,
            'SPDX document must describe the package once')
    sums = []
    for record in sbom['files']:
        path = extracted / record['fileName']
        require(path.resolve().is_relative_to(extracted.resolve()), 'SPDX path escaped package')
        hashes = {entry['algorithm']: entry['checksumValue'] for entry in record['checksums']}
        require(digest(path, 'sha1') == hashes['SHA1'] and digest(path) == hashes['SHA256'], 'SPDX file checksum mismatch')
        sums.append(hashes['SHA1'])
        require(sum(rel == {'spdxElementId': package['SPDXID'], 'relationshipType': 'CONTAINS',
                            'relatedSpdxElement': record['SPDXID']} for rel in sbom['relationships']) == 1,
                'SPDX file must have one package relationship')
    require(package['packageVerificationCode']['packageVerificationCodeValue'] ==
            hashlib.sha1(''.join(sorted(sums)).encode()).hexdigest(), 'SPDX verification code mismatch')
    content = json.dumps(sbom['files'], separators=(',', ':'), ensure_ascii=False).encode()
    require(sbom['documentNamespace'] == 'https://github.com/IoTSharp/Cordonix/spdx/' + hashlib.sha256(content).hexdigest(),
            'SPDX namespace must bind to file content')
    for inside, suffix in (('manifest.json', 'manifest.json'), ('provenance.json', 'provenance.json'), ('sbom.spdx.json', 'sbom.spdx.json')):
        require(digest(extracted / inside) == digest(archive.parent / f'cordonix-{version}-linux-x64.{suffix}'),
                'External metadata differs from package')
    require(run([str(extracted / 'cdx'), '--version']).decode().strip() == f'Cordonix cdx {version}', 'Packaged version mismatch')
    request = task / 'tool-info.json'
    request.write_text('{"operation":"tool_info","arguments":{}}')
    info = json.loads(run([str(extracted / 'cdx'), '--request', str(request)]))
    require(info['status'] == 'ok' and info['name'] == 'Cordonix' and info['version'] == version and
            info['platform'] == 'linux' and info['runtime'] == 'C11', 'Packaged tool_info mismatch')
    require(info.get('update_command') == '--checkupgrade' and b'--checkupgrade' in run([str(extracted / 'cdx'), '--help']), 'Every release must provide the native upgrade command')
    (extracted / 'update-request.json').write_text(json.dumps({'operation':'execute_program', 'arguments':{'program':str(extracted / 'cdx'), 'args':['--version'], 'cwd':str(extracted), 'timeout_ms':2000, 'output_limit_bytes':4096, 'expected_exit_codes':[0]}}))
    upgrade = json.loads(run([str(extracted / 'cdx'), '--checkupgrade', '--install-root', str(extracted)]))
    require(upgrade['status'] == 'completed' and upgrade['cleanup_complete'], 'Packaged checkupgrade must execute its bounded update request')
    example = json.loads((extracted / 'examples/read-files.json').read_text())
    for item, target in zip(example['arguments']['files'], ['README.md', 'docs/native-http.md'], strict=True):
        item['path'] = str(extracted / target)
    batch_request = task / 'read-files.json'
    batch_request.write_text(json.dumps(example), encoding='utf-8')
    batch = json.loads(run([str(extracted / 'cdx'), '--request', str(batch_request)]))
    require(batch['status'] == 'ok' and batch['count'] == batch['completed'] == 2, 'Packaged batch example failed')
    for index, item in enumerate(batch['results']):
        original = Path(example['arguments']['files'][index]['path']).read_bytes().decode('utf-8')
        selected = item['result']['text']
        require(item['index'] == index and item['status'] == 'ok' and item['source'] == 'disk' and bool(selected) and
                (original.startswith(selected) if index == 0 else original.endswith(selected)), 'Packaged batch example returned different text')
    data_query = json.loads(run([str(extracted / 'cdx'), '--request', str(extracted / 'examples/data-query.json')]))
    require(data_query['status'] == 'ok' and data_query['scan_complete'] and data_query['matched'] == 2 and
            data_query['group_count'] == 2 and data_query['groups'] ==
            [{'value': 'Cordonix', 'missing': False, 'count': 1}, {'value': 'Skills', 'missing': False, 'count': 1}],
            'Packaged data query example failed')
    stat_request = task / 'stat.json'
    stat_request.write_text(json.dumps({'operation': 'stat', 'arguments': {'path': str(extracted / 'README.md')}}))
    metadata = json.loads(run([str(extracted / 'cdx'), '--request', str(stat_request)]))
    require(metadata['status'] == 'ok' and metadata['type'] == 'file' and metadata['size_known'] and
            metadata['size_bytes'] == (extracted / 'README.md').stat().st_size, 'Packaged stat returned different metadata')
    find_request = task / 'find-program.json'
    find_request.write_text(json.dumps({'operation': 'find_program', 'arguments': {
        'name': 'cdx', 'candidates': [str(extracted / 'cdx')], 'search_path': False}}))
    found = json.loads(run([str(extracted / 'cdx'), '--request', str(find_request)]))
    require(found['status'] == 'ok' and found['scan_complete'] and not found['truncated'] and
            found['selected'] == str((extracted / 'cdx').resolve()), 'Packaged executable discovery returned a different path')
    success = True
    receipt = {'status': 'PASS', 'checks': checks, 'version': version, 'reproducible_sha256': first_hash,
               'archive': str(archive), 'manifest_files': len(manifest['files']), 'spdx_files': len(sbom['files']),
               'signature': 'NOT_RUN', 'processes': processes}
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({key: value for key, value in receipt.items() if key != 'processes'}))
finally:
    if success:
        if task.parent.resolve() != (root / 'build').resolve() or not task.name.startswith('linux-package-test-'):
            raise RuntimeError('Unexpected package test cleanup path')
        shutil.rmtree(task)
    else:
        print(f'Package test failed; task-owned diagnostics retained at {task}', file=sys.stderr)
PY
