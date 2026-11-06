"""Real CLI and launch regressions for the 0.3.1 metadata/discovery fixes."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import venv


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary = args.executable.resolve(strict=True)
    receipt = {'status': 'FAIL', 'executable': str(binary), 'checks': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def check(name, condition, **evidence):
        receipt['checks'].append(dict(name=name, passed=bool(condition), **evidence))
        assert condition, name

    try:
        with tempfile.TemporaryDirectory(prefix='cdx-031-') as directory:
            root = Path(directory)

            def request(operation, arguments, env=None):
                path = root / 'request.json'
                path.write_text(json.dumps(dict(operation=operation, arguments=arguments)), encoding='utf-8')
                result = subprocess.run([str(binary), '--request', str(path)], env=env,
                                        capture_output=True, timeout=10, check=True)
                return json.loads(result.stdout.decode('utf-8'))

            no_path = {name: value for name, value in os.environ.items() if name.upper() != 'PATH'}
            found = request('find_program', dict(name='cdx', candidates=[str(binary)]), env=no_path)
            check('unset PATH preserves explicit candidates', found['status'] == 'ok' and
                  found['checked'] == 1 and found['scan_complete'], result=found)
            if os.name != 'nt':
                environment = root / 'venv'
                venv.EnvBuilder(with_pip=False, symlinks=True).create(environment)
                interpreter = environment / 'bin/python3'
                found = request('find_program', dict(name='python3', search_path=False,
                                                     candidates=[str(interpreter)]))
                check('virtual environment launch path preserved', found['status'] == 'ok' and
                      found['selected'] == str(interpreter), result=found)
                launched = subprocess.run([found['selected'], '-c', 'import sys;print(sys.prefix)'],
                                          capture_output=True, text=True, timeout=10, check=True)
                check('discovered interpreter launches inside virtual environment',
                      launched.stdout.strip() == str(environment), prefix=launched.stdout.strip())
                a, b = root / 'a', root / 'b'
                a.mkdir(); b.mkdir(); (b / 'child').mkdir()
                (a / 'file').write_bytes(b'a'); (b / 'file').write_bytes(b'correct target\n')
                (a / 'link').symlink_to(b / 'child', target_is_directory=True)
                traversal = str(a / 'link') + '/../file'
                metadata = request('stat', dict(path=traversal))
                check('stat path identifies actual symlink traversal', metadata['status'] == 'ok' and
                      metadata['path'] == traversal and
                      os.lstat(metadata['path']).st_size == metadata['size_bytes'] == 15, result=metadata)
                (b / 'dangling').symlink_to(b / 'missing')
                metadata = request('stat', dict(path=str(b / 'dangling')))
                check('final dangling symlink is still reported', metadata['status'] == 'ok' and
                      metadata['type'] == 'link', result=metadata)
                found = request('find_program', dict(name='python3', search_path=False,
                    candidates=[str(environment / 'missing') + '/../bin/python3']))
                check('missing parent traversal is not simplified', found['status'] == 'not_found' and
                      found['scan_complete'], result=found)
        receipt['status'] = 'PASS'
    finally:
        args.output.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
