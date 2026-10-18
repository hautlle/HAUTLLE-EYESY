"""Compile only the actual update method and recorder in a CPU-only fixture.

No downloads, image execution, GL, audio, OSC sockets or systemd calls.
Exit 2 means an installed C++11 compiler is unavailable; it is not a test pass.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]


def fixture():
    source = (PACKAGE / 'source/app/src/ofApp.cpp').read_text(encoding='utf-8')
    start = source.index('void ofApp::update() {')
    end = source.index('//--------------------------------------------------------------', start)
    update = source[start:end].strip()
    # Deliberately no ShaderHost stub: reintroducing calls into this handler fails compilation.
    template = (PACKAGE / 'tests/controls_fixture.cpp').read_text(encoding='utf-8')
    assert template.count('// INSERT_ACTUAL_UPDATE_HERE') == 1
    return template.replace('// INSERT_ACTUAL_UPDATE_HERE', update)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='Existing local C++ compiler; no compiler is installed by this tool')
    args = parser.parse_args()
    body = fixture()
    compiler = args.compiler or shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
    if not compiler:
        print(json.dumps({'status': 'NOT RUN: no installed C++ compiler',
                          'generated_fixture_sha256': hashlib.sha256(body.encode()).hexdigest()}))
        return 2
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        source, binary = root / 'controls.cpp', root / 'controls-test.exe'
        source.write_text(body, encoding='utf-8')
        bridge = PACKAGE / 'source/app/src/bridge'
        subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-pedantic',
                        '-I' + str(bridge), str(source), str(bridge / 'KnobRecorder.cpp'),
                        '-o', str(binary)], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=10)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
