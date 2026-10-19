"""Opt-in, exact-preimage patch for the inspected stock beta CherryPy editor."""
# Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see ../LICENSE.txt.
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import time
import urllib.error
import urllib.request
import uuid
from installer import InstallError, require, no_links, locked, verify_package, sync_directory

HERE = Path(__file__).resolve().parent
PROFILE = json.loads((HERE / 'beta-web.json').read_text(encoding='utf-8'))
TARGET = Path('/home/music/EYESY_OS')
BACKUP = Path('/var/lib/hautlle-eyesy/web/stock')
SERVICE = 'cherrypy.service'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    no_links(path)
    require(path.is_file() and path.stat().st_nlink == 1,
            'Expected a regular file with one link: ' + str(path))


def read_files(root):
    result = {}
    for row in PROFILE['files']:
        path = root / row['path']
        regular(path)
        result[row['path']] = path.read_bytes()
    return result


def identify(files):
    for state, key in [('stock', 'before_sha256'), ('final', 'after_sha256')]:
        if all(digest(files[r['path']]) == r[key] for r in PROFILE['files']):
            return state
    raise InstallError('Unknown or mixed web files; left unchanged. Restore/review the prior patch first.')


def transform(data, row, reverse=False):
    before, after = ('after_sha256', 'before_sha256') if reverse else ('before_sha256', 'after_sha256')
    require(digest(data) == row[before], 'Web preimage differs: ' + row['path'])
    lines = data.decode('utf-8').splitlines(keepends=True)
    for hunk in reversed(row['hunks']):
        start = hunk['new_start' if reverse else 'old_start'] - 1
        old = hunk['after' if reverse else 'before'].splitlines(keepends=True)
        new = hunk['before' if reverse else 'after'].splitlines(keepends=True)
        require(start >= 0 and lines[start:start + len(old)] == old, 'Web patch context differs.')
        lines[start:start + len(old)] = new
    result = ''.join(lines).encode('utf-8')
    require(digest(result) == row[after], 'Web patch output differs: ' + row['path'])
    return result


def stage(path, data, info):
    no_links(path)
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with tmp.open('xb') as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(str(tmp), info.st_mode & 0o777)
        if os.name == 'posix':
            os.chown(str(tmp), info.st_uid, info.st_gid)
        require(tmp.read_bytes() == data, 'Staged web file differs.')
        return tmp
    except BaseException:
        if tmp.exists():
            tmp.unlink()
        raise


def replace(path, data, info):
    regular(path)
    tmp = stage(path, data, info)
    try:
        os.replace(str(tmp), str(path))
        sync_directory(path.parent)
    finally:
        if tmp.exists():
            tmp.unlink()


def save_backups(root, files, metadata):
    # Existing backups are never overwritten, including partially created backups.
    for name, data in files.items():
        dest = root / name
        no_links(dest)
        if dest.exists():
            regular(dest)
            require(dest.read_bytes() == data, 'Web backup changed: ' + name)
            continue
        dest.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        tmp = stage(dest, data, metadata[name])
        try:
            # Publish without clobbering a backup created by another actor.
            os.link(str(tmp), str(dest))
        finally:
            tmp.unlink()
        sync_directory(dest.parent)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class Operations:
    def command(self, *args):
        return subprocess.check_output(list(args), universal_newlines=True,
                                       stderr=subprocess.STDOUT, timeout=30).strip()

    def peers(self):
        return {name: self.command('systemctl', 'show', name, '-p', 'MainPID', '-p', 'ActiveState',
                                   '-p', 'ExecMainStartTimestampMonotonic')
                for name in ('eyesy-oflua.service', 'eyesy-python.service', 'eyesy-pd.service')}

    def restart(self):
        self.command('systemctl', 'restart', SERVICE)

    def healthy(self, files, final):
        # Local read-only HTTP requests only: never select/reload a mode or save edits.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
        deadline = time.monotonic() + 8
        error = None
        while time.monotonic() < deadline:
            try:
                require(self.command('systemctl', 'is-active', SERVICE) == 'active', 'Web service is inactive.')
                for route, name in [('/', 'web/static/app.html'), ('/app.js', 'web/static/app.js')]:
                    with opener.open('http://127.0.0.1:8080' + route, timeout=2) as response:
                        require(response.status == 200 and response.read(2 * 1024 * 1024) == files[name],
                                'Served web bytes differ: ' + route)
                if final:
                    with opener.open('http://127.0.0.1:8080/modes', timeout=2) as response:
                        require(response.status == 200 and b'OF/Lua Modes' in response.read(2 * 1024 * 1024),
                                'Mode selector did not respond.')
                return
            except (OSError, InstallError, subprocess.SubprocessError) as exc:
                error = exc
                time.sleep(0.25)
        raise InstallError('Web verification failed: ' + str(error))


def change(action, target=TARGET, backup=BACKUP, ops=None):
    require(action in ('apply', 'rollback'), 'Unknown web action.')
    ops = ops or Operations()
    old = read_files(target)
    state = identify(old)
    desired = 'final' if action == 'apply' else 'stock'
    if state == desired:
        return 'Web files already ' + desired + '; unchanged. Rollback of an earlier deployment needs its original backups.'
    metadata = {name: (target / name).stat() for name in old}
    if action == 'apply':
        new = {r['path']: transform(old[r['path']], r) for r in PROFILE['files']}
        save_backups(backup, old, metadata)
        new_metadata = metadata
    else:
        new = read_files(backup)
        require(identify(new) == 'stock', 'Web backup is not stock.')
        new_metadata = {name: (backup / name).stat() for name in new}
    ops.healthy(old, state == 'final')
    peers = ops.peers()
    staged = {}
    try:
        # All output is staged before the first live replacement.
        for name, data in new.items():
            staged[name] = stage(target / name, data, new_metadata[name])
        require(read_files(target) == old, 'Web files changed during preparation.')
        try:
            for name, tmp in staged.items():
                regular(target / name)
                require((target / name).read_bytes() == old[name], 'Web file changed during apply: ' + name)
                os.replace(str(tmp), str(target / name))
                sync_directory((target / name).parent)
            ops.restart()
            ops.healthy(new, desired == 'final')
            require(read_files(target) == new, 'Web files changed during verification.')
            require(ops.peers() == peers, 'Engine/PD service identity changed during web update.')
        except BaseException as original:
            # Restore only exact known bytes; preserve an independent edit and backups.
            current = read_files(target)
            require(all(current[n] in (old[n], new[n]) for n in old),
                    'Web files changed externally; backups retained for manual recovery.')
            backend_changed = current['web/cpapp.py'] != old['web/cpapp.py']
            for name in old:
                if current[name] != old[name]:
                    replace(target / name, old[name], metadata[name])
            if backend_changed:
                ops.restart()
            ops.healthy(old, state == 'final')
            require(ops.peers() == peers, 'Engine/PD identity changed; web files recovered, inspect peer services.')
            raise original
    finally:
        for tmp in staged.values():
            if tmp.exists():
                tmp.unlink()
    return 'Beta web ' + ('controls installed' if action == 'apply' else 'stock files restored') + '; hard-refresh the browser (Ctrl+F5).'


def main(action):
    require(platform.system() == 'Linux', 'Beta web actions require the reviewed Linux device.')
    verify_package(HERE.parent)
    state = identify(read_files(TARGET))
    if action == 'check':
        print('Web file profile: ' + state + '. Exact beta patch; installer transaction is locally tested only.')
        return 0
    require(os.geteuid() == 0, 'Use sudo for explicit web apply/rollback.')
    no_links(BACKUP)
    for parent in [BACKUP] + list(BACKUP.parents):
        if parent.exists():
            info = parent.stat()
            require(info.st_uid == 0 and info.st_mode & 0o022 == 0, 'Web backup path is not root-owned/private.')
    def interrupted(signum, frame):
        raise KeyboardInterrupt('Web operation interrupted by signal ' + str(signum))
    saved = {sig: signal.signal(sig, interrupted) for sig in (signal.SIGTERM, signal.SIGHUP)}
    try:
        with locked(BACKUP.parent):
            print(change(action))
    finally:
        for sig, handler in saved.items():
            signal.signal(sig, handler)
    return 0
