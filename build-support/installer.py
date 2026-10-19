"""Source-only installer for the existing legacy CM3+ HAUTLLE/oFLua setup.

No downloads, package management, network configuration, or service enable/disable.
The simple prepare/undo interface was informed by okyeron's EYESY_OS_for_RasPi;
see docs/INSTALLER-PROVENANCE.txt. This implementation does not copy its shell code.
"""
# Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see ../LICENSE.txt.
import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import uuid

PACKAGE = Path(__file__).resolve().parent.parent
SERVICE = 'eyesyof.service'
STARTER = 'HAUTLLE_START_HERE'
MANIFEST = 'build-support/install-inputs.json'
GIB = 1024 ** 3
CUSTOM = 'custom-pi'
BETA = 'beta-20211213'
BETA_LAUNCHER_SHA256 = 'db4371abdb25e0e3ef77d0dd2fa9a23b3539d23dca1b81fcea92caffbd9f2c71'
BETA_UNIT_SHA256 = '850d62547c684f38ae65aad3d3dbfce4b576aa6318e9be8caac9f68c85b8f0bc'
BETA_BINARY_SHA256 = '9519cd52b5335b9bf4186779eb9d220603284539be6b5f6029026b7a5cfbce26'
PEER_SERVICE = 'eyesy-python.service'

# Exact reviewed account-adapted launcher; never interpret an arbitrary shell file.
CANONICAL_LAUNCHER_SHA256 = '2df83917a38a9b82e19b67feb65f5a300e2a4cfdef9bd6453d2f46f1213c94df'


class InstallError(Exception):
    pass


def support_module(name):
    spec = importlib.util.spec_from_file_location('hautlle_' + name, Path(__file__).parent / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition, message):
    if not condition:
        raise InstallError(message)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def save_json(path, data):
    path = Path(path)
    no_links(path)
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    with tmp.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
        handle.write('\n')
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(str(tmp), str(path))
    sync_directory(path.parent)


def sync_directory(path):
    if os.name == 'posix':
        fd = os.open(str(path), os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def exclusive_text(path, body):
    """Publish a complete file without replacing an existing destination."""
    no_links(path)
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with tmp.open('x', encoding='utf-8', newline='\n') as handle:
            handle.write(body); handle.flush(); os.fsync(handle.fileno())
        os.link(str(tmp), str(path))
        sync_directory(path.parent)
    finally:
        if tmp.exists():
            tmp.unlink()


def no_links(path):
    """Refuse symlink redirection of installer-owned paths, including ancestors."""
    path = Path(path).absolute()
    for part in [path] + list(path.parents):
        require(not part.is_symlink(), 'Symlink is not supported here: ' + str(part))


def inventory(root, allow_links=False):
    root = Path(root)
    no_links(root)
    require(root.is_dir(), 'Missing directory: ' + str(root))
    result = {}
    for directory, folders, files in os.walk(str(root), followlinks=False):
        for name in sorted(folders + files):
            p = Path(directory) / name
            key = p.relative_to(root).as_posix()
            if p.is_symlink():
                require(allow_links, 'Unexpected symlink: ' + str(p))
                result[key] = {'link': os.readlink(str(p))}
            elif p.is_file():
                result[key] = {'sha256': digest(p), 'bytes': p.stat().st_size}
            else:
                require(p.is_dir(), 'Special file is not supported: ' + str(p))
                result[key] = {'directory': True}
    return result


def verify_package(package):
    manifest = read_json(package / MANIFEST)
    actual = inventory(package)
    actual_files = {k for k, v in actual.items() if 'sha256' in v}
    require(actual_files == set(manifest) | {MANIFEST},
            'Source package file list differs; extract a clean source release.')
    for name, expected in manifest.items():
        require(actual[name]['sha256'] == expected, 'Source package changed: ' + name)
    return manifest


@contextmanager
def locked(root):
    no_links(root)
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    lock = root / 'installer.lock'
    no_links(lock)
    with lock.open('a+b') as handle:
        try:
            if os.name == 'nt':  # Local fixtures only; platform gate forbids installation.
                import msvcrt
                handle.seek(0)
                if handle.read(1) == b'':
                    handle.write(b'0'); handle.flush()
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise InstallError('Another installer is running; wait for it to finish.')
        yield


class Layout:
    def __init__(self, root=Path('/'), profile=CUSTOM, workspace='home'):
        # root injection is for imported unit fixtures only; no CLI override exists.
        self.root = Path(root)
        require(profile in (CUSTOM, BETA), 'Unknown installation profile.')
        self.profile = profile
        self.account = 'music' if profile == BETA else 'pi'
        self.service = 'eyesy-oflua.service' if profile == BETA else SERVICE
        self.home = self.root / 'home' / self.account
        require(workspace in ('home','sdcard'), 'Unknown workspace.')
        self.workspace = workspace
        self.work_parent = self.root / 'sdcard' if workspace == 'sdcard' else self.home
        self.work = self.work_parent / 'hautlle-eyesy'
        self.state = self.root / 'var/lib/hautlle-eyesy'
        self.modes = self.root / 'sdcard/Modes'
        self.starter = self.modes / 'oFLua' / STARTER
        self.retired = self.root / 'sdcard/.hautlle-installer-backups'
        self.dropin = self.root / 'etc/systemd/system' / (self.service + '.d') / 'zz-hautlle-source.conf'
        self.journal = self.state / 'activation.json'
        self.canonical_launcher = self.home / 'EYESY_OS/engines/oflua/run.sh'
        self.canonical_cwd = self.home / 'openFrameworks/apps/myApps/eyesy'
        self.canonical_binary = self.canonical_cwd / 'bin/eyesy'

    def candidate(self, name):
        require(isinstance(name, str) and re.fullmatch(r'[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}', name),
                'Invalid installation ID.')
        path = self.work / name
        no_links(path)
        return path


def beta_identity(layout):
    """Recognize the inspected image's static identity; this is not authentication."""
    expected = [(layout.root / 'usr/lib/systemd/system/eyesy-oflua.service', BETA_UNIT_SHA256),
                (layout.canonical_launcher, BETA_LAUNCHER_SHA256),
                (layout.canonical_binary, BETA_BINARY_SHA256)]
    for path, value in expected:
        no_links(path)
        require(path.is_file() and digest(path) == value, 'Beta profile identity differs: ' + str(path))
    passwd = (layout.root / 'etc/passwd').read_text(encoding='utf-8').splitlines()
    music = [line.split(':') for line in passwd if line.startswith('music:')]
    require(len(music) == 1 and music[0][2:4] == ['1000', '1000'] and
            music[0][5:7] == ['/home/music', '/bin/bash'], 'Beta music account differs.')


def detect_layout(root=Path('/'), profile='auto', workspace=None):
    root = Path(root)
    # Keep the existing unsupported-host diagnostic; never inspect another host.
    if platform.system() != 'Linux':
        return Layout(root, CUSTOM if profile == 'auto' else profile, workspace or 'home')
    if profile != 'auto':
        result = Layout(root, profile, workspace or 'home')
        if profile == BETA:
            beta_identity(result)
        return result
    base = Layout(root)
    if base.journal.exists():
        Host().secure_state(base)
        journal = read_json(base.journal)
        if journal.get('phase') != 'rolled_back':
            saved = journal.get('workspace','home')
            require(workspace is None or workspace == saved, 'Workspace differs from active recovery journal.')
            return Layout(root, journal.get('profile', CUSTOM), saved)
    custom = (root / 'home/pi').is_dir() and any((root / p / SERVICE).is_file()
             for p in ('etc/systemd/system', 'usr/lib/systemd/system', 'lib/systemd/system'))
    beta = (root / 'home/music/EYESY_OS/engines/oflua/run.sh').is_file()
    require(custom != beta, 'Profile is missing or ambiguous; select a reviewed --profile explicitly.')
    result = Layout(root, CUSTOM if custom else BETA, workspace or 'home')
    if beta:
        beta_identity(result)
    return result


def beta_launcher(layout, binary):
    # Generated only in the separate candidate release. Original launcher is untouched.
    require(not any(c in str(binary) + str(layout.home) for c in " %\n\r\t'\"$`"),
            'Unsupported launcher path.')
    return ('#!/bin/sh\nset -eu\n'
            'export LUA_PATH="%s/?.lua;%s/EYESY_OS/engines/oflua/?.lua;;"\n'
            'cd "%s"\nexec /usr/bin/stdbuf -o0 "%s" > /tmp/video.log 2>&1\n'
            % (binary.parent.parent / "modules", layout.home, binary.parent, binary))


class Host:
    def __init__(self, layout=None):
        self.layout = layout or Layout()

    def run(self, command, timeout=30):
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   universal_newlines=True, timeout=timeout)
        require(completed.returncode == 0, 'Command failed: %s\n%s' %
                (' '.join(command), completed.stderr.strip()))
        return completed.stdout.strip()

    def unit_properties(self, unit, missing=False):
        fields = ['LoadState', 'ActiveState', 'MainPID', 'UnitFileState', 'FragmentPath',
                  'DropInPaths', 'ExecStart', 'WorkingDirectory', 'User', 'NRestarts', 'NeedDaemonReload',
                  'Group', 'ControlGroup', 'Type', 'KillMode']
        text = self.run(['systemctl', 'show', unit] + ['--property=' + p for p in fields])
        props = dict(line.split('=', 1) for line in text.splitlines() if '=' in line)
        require(all(p in props for p in fields), 'Cannot read complete service settings.')
        if missing and props['LoadState'] == 'not-found':
            return props
        require(props['LoadState'] == 'loaded', 'Existing ' + unit + ' is required.')
        require(props['UnitFileState'] not in ('masked', 'masked-runtime'), 'Service is masked.')
        return props

    def properties(self):
        return self.unit_properties(self.layout.service)

    def peer_snapshot(self, stopped=True):
        props = self.unit_properties(PEER_SERVICE, missing=True)
        if props['LoadState'] == 'not-found':
            return {'missing': True}
        require(props['NeedDaemonReload'] == 'no', 'Python renderer settings need daemon-reload.')
        if stopped:
            require(props['ActiveState'] == 'inactive' and props['MainPID'] == '0',
                    'Python renderer is running or transitioning; stop it separately before continuing.')
        paths = [props['FragmentPath']] + shlex.split(props['DropInPaths'])
        require(paths[0], 'Python renderer has no backing unit file.')
        return {'files': {p: digest(Path(p)) for p in paths},
                'settings': {k: props[k] for k in ('UnitFileState', 'ExecStart', 'WorkingDirectory', 'User', 'Group')}}

    def log_check(self):
        if self.layout.profile != BETA:
            return
        import pwd
        path = self.layout.root / 'tmp/video.log'
        no_links(path)
        if path.exists():
            account = pwd.getpwnam(self.layout.account)
            require(path.is_file() and path.stat().st_uid == account.pw_uid and
                    path.stat().st_mode & 0o200, 'Existing /tmp/video.log must be a writable music-owned regular file.')

    def control(self, verb):
        require(verb in ('stop', 'start', 'daemon-reload'), 'Unsupported service action.')
        return self.run(['systemctl', verb] + ([] if verb == 'daemon-reload' else [self.layout.service]), 30)

    def identity(self, pid):
        return (str(Path('/proc') / str(pid) / 'exe'), str(Path('/proc') / str(pid) / 'cwd'))

    def process_matches(self, pid, binary, cwd, modes=None):
        """Match the actual executable inode/path; cwd may move into a real mode."""
        try:
            binary, cwd = Path(binary), Path(cwd)
            no_links(binary); no_links(cwd)
            exe, directory = (Path(p) for p in self.identity(pid))
            # samefile also rejects a replaced/deleted executable at the old path.
            if exe.resolve(strict=True) != binary.resolve(strict=True) or not exe.samefile(binary):
                return False
            current = directory.resolve(strict=True)
            no_links(current)
            if current == cwd.resolve(strict=True):
                return current.is_dir()
            if modes is None:
                return False
            modes = Path(modes)
            no_links(modes)
            main = current / 'main.lua'
            no_links(main)
            return (current.parent == modes.resolve(strict=True) and current.is_dir()
                    and main.is_file())
        except (OSError, RuntimeError, InstallError):
            # An exited process, dangling link or unsafe path is never a match.
            return False

    def unit_pids(self, group):
        expected = '/system.slice/' + self.layout.service
        require(group == expected, 'Unexpected service cgroup.')
        path = self.layout.root / 'sys/fs/cgroup/systemd' / group.lstrip('/') / 'cgroup.procs'
        return set(path.read_text(encoding='ascii').split())

    def process_record(self, pid):
        require(str(pid).isdigit() and int(pid) > 0, 'Invalid process ID.')
        path = self.layout.root / 'proc' / str(pid)
        fields = (path / 'stat').read_text().rsplit(')', 1)[1].split()
        status = dict(line.split(':', 1) for line in (path / 'status').read_text().splitlines() if ':' in line)
        groups = [line.split(':', 2)[2] for line in (path / 'cgroup').read_text().splitlines()
                  if line.split(':', 2)[1] == 'name=systemd']
        return {'parent': fields[1], 'start': fields[19], 'uids': status['Uid'].split(),
                'groups': groups, 'argv': (path / 'cmdline').read_bytes().rstrip(b'\0').decode().split('\0')}

    def route_process_matches(self, props, route, modes):
        if not route.get('shell_parent'):
            return self.process_matches(props['MainPID'], route['binary'], route['runtime_cwd'], modes)
        try:
            main = props['MainPID']
            group = props['ControlGroup']
            pids = self.unit_pids(group)
            if main not in pids or len(pids) not in (1, 2):
                return False
            records = {pid: self.process_record(pid) for pid in pids}
            if any(r['uids'] != ['1000'] * 4 or r['groups'] != [group] for r in records.values()):
                return False
            engines = [pid for pid in pids if self.process_matches(pid, route['binary'], route['runtime_cwd'], modes)]
            if len(engines) != 1:
                return False
            engine = engines[0]
            if engine == main:
                valid = len(pids) == 1
            else:
                shell_exe = Path(self.identity(main)[0])
                interpreter = Path(route['interpreter'])
                valid = (len(pids) == 2 and records[engine]['parent'] == main and
                         shell_exe.resolve(strict=True) == interpreter.resolve(strict=True) and
                         shell_exe.samefile(interpreter) and
                         records[main]['argv'] in ([str(self.layout.root / 'bin/bash'), route['entry']],
                                                 [str(interpreter), route['entry']]))
            # Reject PID reuse, child/cgroup movement or exit during observation.
            return valid and self.unit_pids(group) == pids and all(
                self.process_record(pid) == records[pid] for pid in pids)
        except (OSError, RuntimeError, InstallError, ValueError, KeyError, IndexError):
            return False

    def wait_route_healthy(self, route, restarts, modes):
        if not route.get('shell_parent'):
            return self.wait_healthy(Path(route['binary']), Path(route['runtime_cwd']), restarts, modes)
        deadline, healthy_since = time.monotonic() + 15, None
        while time.monotonic() < deadline:
            props = self.properties()
            require(props['NRestarts'] == restarts, 'Engine restarted during startup.')
            if props['ActiveState'] == 'active' and int(props['MainPID']) > 0:
                require(self.route_process_matches(props, route, modes), 'Beta shell/engine identity differs.')
                healthy_since = healthy_since or time.monotonic()
                if time.monotonic() - healthy_since >= 3:
                    return
            else:
                healthy_since = None
            time.sleep(0.25)
        raise InstallError('Beta engine did not remain running through the startup check.')

    def platform_check(self, layout, build=False, recovery=False):
        require(platform.system() == 'Linux' and platform.machine() in ('armv6l', 'armv7l', 'armv8l')
                and struct.calcsize('P') == 4, 'Requires the existing 32-bit ARM Linux EYESY setup.')
        import pwd
        account = pwd.getpwnam(self.layout.account)
        require(account.pw_dir == '/home/' + layout.account, 'Installation account home differs.')
        if layout.profile == BETA and not recovery:
            beta_identity(layout)
        if build:
            require(os.geteuid() == account.pw_uid, 'Prepare as ' + layout.account + ', without sudo.')
        else:
            require(os.geteuid() == 0, 'Use sudo for activation or rollback.')
        if not recovery:
            release = (layout.root / 'etc/os-release').read_text(encoding='utf-8')
            fields = dict(line.split('=', 1) for line in release.splitlines() if '=' in line)
            require(fields.get('ID', '').strip('"') == 'raspbian' and
                    fields.get('VERSION_ID', '').strip('"') == '10',
                    'This source recipe targets Raspbian 10 armhf; other systems are not qualified.')
            model = (layout.root / 'proc/device-tree/model').read_text(encoding='utf-8').strip('\0\n')
            require('Compute Module 3' in model, 'This installer targets EYESY CM3/CM3+ only.')
            for relative in ['opt/vc/include/bcm_host.h', 'opt/vc/lib/libbrcmEGL.so',
                             'opt/vc/lib/libbrcmGLESv2.so', 'dev/vchiq']:
                require((layout.root / relative).exists(), 'Missing legacy Broadcom prerequisite: /' + relative)
        if build:
            for tool in ('gcc', 'g++', 'ar', 'dpkg-query', 'systemctl'):
                require(shutil.which(tool), 'Missing prerequisite command: ' + tool)
            for compiler in ('gcc', 'g++'):
                require(self.run([compiler, '-dumpfullversion', '-dumpversion']) == '8.3.0' and
                        self.run([compiler, '-dumpmachine']) == 'arm-linux-gnueabihf',
                        'Requires the recorded GCC/G++ 8.3.0 armhf toolchain.')
            require('2.31.1' in self.run(['ar', '--version']).splitlines()[0],
                    'Requires the recorded binutils 2.31.1 toolchain.')

    def own_mode(self, path):
        import pwd
        account = pwd.getpwnam(self.layout.account)
        for directory, folders, files in os.walk(str(path)):
            os.chown(directory, account.pw_uid, account.pw_gid)
            for name in files:
                os.chown(str(Path(directory) / name), account.pw_uid, account.pw_gid)

    def secure_state(self, layout):
        # Recovery state is privileged; do not trust a user-owned ledger.
        for path in (layout.state, layout.journal):
            no_links(path)
            if path.exists():
                require(path.stat().st_uid == 0 and path.stat().st_mode & 0o022 == 0,
                        'Recovery state must be root-owned and not writable by others: ' + str(path))

    def prerequisite_packages(self, package):
        missing = []
        for name in read_json(package / 'build-support/dependencies.json')['recorded_prerequisite_packages']:
            try:
                status = self.run(['dpkg-query', '-W', '-f=${Status}', name])
                if status != 'install ok installed':
                    missing.append(name)
            except InstallError:
                missing.append(name)
        require(not missing, 'Missing installed development packages: ' + ', '.join(missing) +
                '. Ask the maintainer to prepare this system; the installer does not install packages.')

    def workspace_check(self, layout):
        import pwd
        account = pwd.getpwnam(layout.account)
        for path in (layout.work_parent, layout.work):
            no_links(path)
            if path.exists():
                info = path.stat()
                require(path.is_dir() and info.st_uid == account.pw_uid and info.st_mode & 0o022 == 0,
                        'Workspace must be owned by %s and not writable by others: %s' % (layout.account,path))
        if layout.workspace == 'sdcard':
            mount = str(layout.work_parent)
            rows = [line.split() for line in (layout.root/'proc/mounts').read_text().splitlines()]
            entries = [row for row in rows if row[1] == mount]
            require(os.path.ismount(mount) and len(entries) == 1 and entries[0][2] == 'ext4' and
                    'rw' in entries[0][3].split(',') and 'noexec' not in entries[0][3].split(','),
                    'The sdcard workspace requires the existing writable executable ext4 /sdcard mount.')

    def resource_check(self, temporary_swap):
        resources = support_module('build_resources')
        if temporary_swap:
            require(self.layout.workspace == 'sdcard', 'Temporary swap requires --workspace sdcard.')
            resources.preflight(self.layout.work_parent, self.layout.root)
            for tool in ('sudo','/sbin/mkswap','/sbin/swapon','/sbin/swapoff'):
                require(shutil.which(tool), 'Missing temporary-swap prerequisite: ' + tool)
        else:
            memory = resources.Operations().memory()
            require(memory['MemAvailable'] >= 512*1024**2 and memory['MemAvailable']+memory['SwapFree'] >= 1536*1024**2,
                    'Insufficient build memory. On clean beta use --workspace sdcard --temporary-swap, '
                    'which explicitly permits a temporary 1 GiB swap file; existing swap is never changed.')

    def build(self, package, candidate):
        commands = [
            [sys.executable, '-B', str(package / 'build-support/rebuild_dependencies.py'),
             '--output', str(candidate / 'dependencies')],
            [sys.executable, '-B', str(package / 'build-support/build.py'),
             '--source-root', str(package / 'source'), '--dependency-root', str(candidate / 'dependencies'),
             '--dependency-build-result', str(candidate / 'dependencies/dependency-build-result.json'),
             '--build-dir', str(candidate / 'build')]]
        if getattr(self, 'temporary_swap', False):
            commands[1] += ['--compiler-memory-profile','bindings-gc-v1']
            resources = support_module('build_resources')
            with resources.BuildResources(candidate, self.layout.work_parent, self.layout.root).session() as session:
                for command,label,timeout in zip(commands,('dependencies','engine'),(300,7200)):
                    print('Building %s with approved temporary swap and resource guards.' % label,flush=True)
                    session.run(command,label,timeout)
            return
        for index, command in enumerate(commands):
            print('[%d/3] %s' % (index + 1, ['Building two source dependencies and running their CPU check.',
                  'Building the engine (97 units; allow an hour or longer).'][index]), flush=True)
            subprocess.run(command, check=True, cwd=str(package))

    def wait_healthy(self, binary, cwd, expected_restarts, modes=None):
        # Checks process identity/liveness only. Does not claim visual or audio correctness.
        deadline, healthy_since = time.monotonic() + 15, None
        while time.monotonic() < deadline:
            props = self.properties()
            require(props['NRestarts'] == expected_restarts, 'Engine restarted during startup.')
            if props['ActiveState'] == 'active' and int(props['MainPID']) > 0:
                require(self.process_matches(props['MainPID'], binary, cwd, modes), 'Running engine identity differs.')
                healthy_since = healthy_since or time.monotonic()
                if time.monotonic() - healthy_since >= 3:
                    return
            else:
                healthy_since = None
            time.sleep(0.25)
        raise InstallError('Engine did not remain running through the startup check.')


def service_config(host, ignore=None):
    props = host.properties()
    paths = [props['FragmentPath']] + shlex.split(props['DropInPaths'])
    require(paths[0], 'Service has no backing unit file.')
    files = {}
    for name in paths:
        path = Path(name)
        if ignore is not None and path == ignore:
            continue
        require(path.is_absolute() and path.is_file(), 'Cannot snapshot service file: ' + name)
        files[name] = digest(path)
    return {'files': files, 'enabled': props['UnitFileState']}, props


def executable_path(props):
    paths = re.findall(r'(?:^|[ {;])path=([^ ;}]+)', props['ExecStart'])
    require(len(paths) == 1, 'Expected one simple existing service executable.')
    return Path(paths[0])


def launch_route(props, layout):
    """Keep systemd's launch entry distinct from the executable after exec()."""
    entry = executable_path(props)
    service_cwd = Path(props['WorkingDirectory'])
    require(entry.is_absolute() and service_cwd.is_absolute(), 'Startup paths must be absolute.')
    no_links(entry); no_links(service_cwd)
    require(entry.is_file(), 'Prior engine is missing; activation cannot guarantee rollback.')
    require(service_cwd.is_dir(), 'Prior working directory is missing.')
    entry_hash = digest(entry)
    binary, runtime_cwd = entry, service_cwd
    extra = {}
    if entry == layout.canonical_launcher:
        pin = BETA_LAUNCHER_SHA256 if layout.profile == BETA else CANONICAL_LAUNCHER_SHA256
        require(entry_hash == pin and props['User'] == layout.account,
                'Canonical launcher differs from the reviewed profile launcher.')
        binary, runtime_cwd = layout.canonical_binary, layout.canonical_cwd
        if layout.profile == BETA:
            beta_identity(layout)
            require(props.get('Group') == 'music' and props.get('Type') == 'simple' and
                    props.get('KillMode') == 'control-group', 'Beta service account/type/kill scope differs.')
            interpreter = layout.root / 'usr/bin/bash'
            no_links(interpreter)
            extra = {'shell_parent': True, 'interpreter': str(interpreter),
                     'interpreter_sha256': digest(interpreter)}
    elif layout.profile == BETA:
        require(entry.name == 'run.sh' and entry.parent.name == 'bin' and entry.parent.parent.name == 'release'
                and entry.parent.parent.parent.parent == layout.work, 'Unrecognized beta launch route.')
        layout.candidate(entry.parent.parent.parent.name)
        binary = entry.parent / 'EYESY_SOURCE_BUILD'
        runtime_cwd = binary.parent
        require(entry.read_text(encoding='utf-8') == beta_launcher(layout, binary) and
                props['User'] == 'music' and props.get('Group') == 'music', 'Beta candidate launcher/account differs.')
    else:
        with entry.open('rb') as handle:
            require(handle.read(2) != b'#!', 'Unrecognized startup script; only the reviewed canonical launcher is supported.')
    no_links(binary); no_links(runtime_cwd)
    require(binary.is_file(), 'Prior engine is missing; activation cannot guarantee rollback.')
    require(runtime_cwd.is_dir(), 'Prior runtime working directory is missing.')
    return {'entry': str(entry), 'entry_sha256': entry_hash,
            'binary': str(binary), 'binary_sha256': digest(binary),
            'service_cwd': str(service_cwd), 'runtime_cwd': str(runtime_cwd), 'user': props['User'], **extra}


def require_route(props, layout, expected):
    require(launch_route(props, layout) == expected, 'Launch route changed; refusing to use a different launcher or engine.')


class Installer:
    def __init__(self, package=PACKAGE, layout=None, host=None, temporary_swap=False):
        self.package = Path(package).resolve()
        self.layout = layout or detect_layout()
        self.host = host or Host(self.layout)
        self.host.layout = self.layout
        self.temporary_swap = temporary_swap
        self.host.temporary_swap = temporary_swap

    def preflight(self):
        verify_package(self.package)
        self.host.platform_check(self.layout, build=True)
        self.host.prerequisite_packages(self.package)
        for path in (self.layout.home, self.layout.modes / 'oFLua'):
            no_links(path)
            require(path.is_dir(), 'Existing compatible installation required: ' + str(path))
        no_links(self.layout.work)
        self.host.workspace_check(self.layout)
        require(self.layout.work != self.package and self.layout.work not in self.package.parents and
                self.package not in self.layout.work.parents, 'Source package and work directory must be separate.')
        props = self.host.properties()
        require(props['User'] in ('', 'root', self.layout.account), 'Unexpected service account.')
        require(props['NeedDaemonReload'] == 'no', 'Service settings need daemon-reload; resolve them before installation.')
        launch_route(props, self.layout)
        self.host.peer_snapshot()
        self.host.log_check()
        free = shutil.disk_usage(str(self.layout.work_parent)).free
        package_bytes = sum(p.stat().st_size for p in self.package.rglob('*') if p.is_file())
        need = max(GIB, package_bytes + 800*1024**2) + (GIB if self.temporary_swap else 0)
        require(free >= need, 'At least 1 GiB build space plus package/reserve and any approved swap is required in ' + str(self.layout.work_parent))
        require(shutil.disk_usage(str(self.layout.root)).free >= 320*1024**2, 'At least 320 MiB root reserve is required.')
        self.host.resource_check(self.temporary_swap)

    def prepare(self, dry=False):
        print('Checking the source release and existing legacy EYESY prerequisites.', flush=True)
        self.preflight()
        if dry:
            print('Dry run: would copy source, build 9 dependency units, run the CPU check, and build 97 engine units.\n'
                  'No files, modes, settings or services have changed.')
            return
        with locked(self.layout.work):
            # One prepared candidate at a time keeps the two-command interface unambiguous.
            pointer = self.layout.work / 'prepared.json'
            if pointer.exists():
                prior = read_json(pointer)
                require(prior.get('state') in ('failed', 'rolled_back'),
                        'A candidate already exists. Use activate, rollback, or ask the maintainer before preparing another.')
                old = self.layout.candidate(prior['id'])
                resources = old/'build-resources.json'
                require(not resources.exists() or read_json(resources).get('swap_cleanup_verified') is True,
                        'Previous build cleanup is pending; run cleanup-build with the same workspace first.')
            name = time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '-' + uuid.uuid4().hex[:8]
            candidate = self.layout.candidate(name)
            candidate.mkdir()
            state = {'schema': 1, 'id': name, 'state': 'building', 'profile': self.layout.profile,
                     'workspace':self.layout.workspace, 'temporary_swap':self.temporary_swap, 'runtime_qualified': False}
            save_json(pointer, state)
            try:
                manifest = verify_package(self.package)
                snapshot = candidate / 'package'
                for relative in sorted(set(manifest) | {MANIFEST}):
                    target = snapshot / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(str(self.package / relative), str(target))
                verify_package(snapshot)
                self.host.build(snapshot, candidate)
                self.verify_build(candidate, self.temporary_swap)
                print('[3/3] Staging the verified engine, matching data and starter mode.', flush=True)
                shutil.copytree(str(candidate / 'build/runtime'), str(candidate / 'release'))
                if (snapshot / 'modules').is_dir():
                    shutil.copytree(str(snapshot / 'modules'), str(candidate / 'release/modules'))
                if self.layout.profile == BETA:
                    launcher = candidate / 'release/bin/run.sh'
                    exclusive_text(launcher, beta_launcher(self.layout, launcher.parent / 'EYESY_SOURCE_BUILD'))
                    launcher.chmod(0o755)
                staged = inventory(candidate / 'release')
                state.update(state='prepared', release=staged,
                             package_manifest_sha256=digest(snapshot / MANIFEST))
                save_json(candidate / 'candidate.json', state)
                save_json(pointer, state)
            except BaseException as error:
                state.update(state='failed', error=str(error))
                save_json(pointer, state)
                raise
        print('Prepared successfully. The current engine is unchanged.\n'
              'When ready, run: sudo python3 install.py activate --workspace ' + self.layout.workspace + '\n'
              'Build logs and candidate: ' + str(candidate))

    def verify_build(self, candidate, temporary_swap=False):
        snapshot = candidate / 'package'
        verify_package(snapshot)
        result = read_json(candidate / 'build/build-result.json')
        require(result.get('complete') is True and result.get('public_dependency_rebuild_used') is True,
                'Engine build is incomplete or did not use the source dependency result.')
        # Run the existing strict dependency verifier from the immutable source snapshot.
        code = ('import sys; sys.path.insert(0, sys.argv[1]); '
                'import rebuild_dependencies as d; from pathlib import Path; '
                'd.validate_result(Path(sys.argv[2]), Path(sys.argv[3]))')
        self.host.run([sys.executable, '-B', '-c', code, str(snapshot / 'build-support'),
                       str(candidate / 'dependencies'), str(candidate / 'dependencies/dependency-build-result.json')])
        require(result.get('dependency_build_result_sha256') == digest(candidate / 'dependencies/dependency-build-result.json'),
                'Engine dependency identity differs.')
        for field, filename in [('source_manifest_sha256', 'source-hashes.json'),
                                ('plan_sha256', 'plan.json'), ('dependency_manifest_sha256', 'dependencies.json')]:
            require(result.get(field) == digest(snapshot / 'build-support' / filename), 'Build manifest differs: ' + field)
        plan = read_json(snapshot / 'build-support/plan.json')
        profile = result.get('compiler_memory_profile')
        require(profile == ('bindings-gc-v1' if temporary_swap else 'original'), 'Compiler memory profile differs from preparation consent.')
        if temporary_swap:
            resources = support_module('build_resources')
            status = read_json(candidate/'build-resources.json')
            require(status.get('complete') is True and status.get('swap_cleanup_verified') is True and
                    status.get('policy') == resources.POLICY and not (candidate/'temporary-build.swap').exists(),
                    'Temporary build swap cleanup is incomplete; activation refused.')
        if 'compiler_memory_profile' in result:
            recipe = support_module('build')
            effective = recipe.memory_profile(recipe.expand_plan(plan, snapshot/'source', candidate/'build',
                     candidate/'dependencies', self.layout.root, self.layout.root/'opt/vc','g++','ar'), profile)
            recorded = candidate/'build/expanded-plan.json'
            require(digest(recorded) == result.get('effective_plan_sha256') and read_json(recorded) == effective,
                    'Effective build commands differ from the recorded profile.')
            commands = {str(Path(u['source'])):u['args'] for u in effective['units']}
            for row in result.get('compiled',[]):
                command = commands.get(row['source'])
                require(command is not None and row.get('command') == command and
                        row.get('command_sha256') == recipe.command_sha256(command), 'Compile command provenance differs.')
        expected = {str(snapshot / 'source' / r['source'].replace('${SOURCE}/', '')) for r in plan['units']}
        records = result.get('compiled', [])
        require(len(records) == len(expected) == 97 and {r['source'] for r in records} == expected,
                'Expected all 97 fresh engine compile records.')
        for row in records:
            source, obj = Path(row['source']), Path(row['object'])
            no_links(source); no_links(obj)
            require(candidate / 'build/objects' in obj.parents and
                    digest(source) == row['source_sha256'] and digest(obj) == row['object_sha256'],
                    'Compiled input/output identity differs.')
        binary = candidate / 'build/runtime/bin/EYESY_SOURCE_BUILD'
        require(Path(result.get('binary', '')) == binary and binary.is_file() and
                digest(binary) == result.get('binary_sha256') and os.access(str(binary), os.X_OK),
                'Missing or changed engine executable.')
        require(inventory(binary.parent / 'data') == inventory(snapshot / 'source/app/bin/data'),
                'Runtime data is incomplete or differs from the source build.')

    def prepared(self):
        pointer = read_json(self.layout.work / 'prepared.json')
        candidate = self.layout.candidate(pointer['id'])
        state = read_json(candidate / 'candidate.json')
        require(pointer == state and state.get('state') == 'prepared', 'No complete prepared candidate.')
        require(state.get('profile', CUSTOM) == self.layout.profile, 'Prepared candidate profile differs.')
        require(state.get('workspace','home') == self.layout.workspace, 'Prepared candidate workspace differs.')
        self.verify_build(candidate, state.get('temporary_swap',False))
        require(digest(candidate / 'package' / MANIFEST) == state['package_manifest_sha256'] and
                inventory(candidate / 'release') == state['release'], 'Staged release changed.')
        return candidate, state

    def stopped(self):
        props = self.host.properties()
        require(props['ActiveState'] == 'inactive' and props['MainPID'] == '0',
                'Service has not stopped; no installation files will be switched.')
        return props

    def activate(self, dry=False):
        self.host.platform_check(self.layout)
        self.host.secure_state(self.layout)
        candidate, state = self.prepared()
        no_links(self.layout.state); no_links(self.layout.dropin); no_links(self.layout.starter)
        require(not self.layout.dropin.exists(), 'An installer drop-in already exists; use rollback first.')
        require(not self.layout.starter.exists(), STARTER + ' already exists; it will not be overwritten.')
        if self.layout.journal.exists():
            require(read_json(self.layout.journal).get('phase') == 'rolled_back',
                    'An installation/recovery is pending; use rollback first.')
        config, props = service_config(self.host)
        require(props['NeedDaemonReload'] == 'no', 'Service settings need daemon-reload; resolve them before installation.')
        require(props['ActiveState'] in ('active', 'inactive'), 'Service is transitioning or failed; resolve it first.')
        peer = self.host.peer_snapshot()
        self.host.log_check()
        old_route = launch_route(props, self.layout)
        self.check_prior_process(props, old_route)
        if dry:
            print('Dry run: would back up modes and service files, add a separate release and starter mode,\n'
                  'then start the prepared engine on ' + self.layout.service + '. Enabled settings and previous binary stay unchanged.')
            return
        with locked(self.layout.state):
            # Recheck under the lock, including all prepared bytes, before touching the service.
            candidate, state = self.prepared()
            require(not self.layout.dropin.exists() and not self.layout.starter.exists(), 'Installation target appeared.')
            current, fresh = service_config(self.host)
            require(current == config and all(fresh[k] == props[k] for k in
                    ('ActiveState', 'MainPID', 'NRestarts', 'NeedDaemonReload')), 'Service changed during preparation.')
            require_route(fresh, self.layout, old_route)
            self.check_prior_process(fresh, old_route)
            require(self.host.peer_snapshot() == peer, 'Python renderer changed before backup.')
            backup = self.layout.state / 'backups' / (state['id'] + '-' + uuid.uuid4().hex[:8])
            no_links(backup)
            backup.mkdir(parents=True)
            modes_before = inventory(self.layout.modes, allow_links=True)
            need = sum(r.get('bytes', 0) for r in modes_before.values()) + 256 * 1024 ** 2
            require(shutil.disk_usage(str(backup)).free > need, 'Insufficient space for a modes backup.')
            print('Backing up modes and startup configuration.', flush=True)
            with tarfile.open(str(backup / 'Modes.tar'), 'x', dereference=False) as archive:
                archive.add(str(self.layout.modes), arcname='Modes', recursive=True)
            require(inventory(self.layout.modes, allow_links=True) == modes_before, 'Modes changed during backup; retry later.')
            save_json(backup / 'modes-inventory.json', modes_before)
            for index, filename in enumerate(config['files']):
                saved = backup / ('service-%02d.conf' % index)
                shutil.copy2(filename, str(saved))
                require(digest(saved) == config['files'][filename], 'Service file changed during backup.')
            for index, filename in enumerate(peer.get('files', {})):
                saved = backup / ('python-service-%02d.conf' % index)
                shutil.copy2(filename, str(saved))
                require(digest(saved) == peer['files'][filename], 'Python service file changed during backup.')
            save_json(backup / 'python-service.json', peer)
            if old_route['entry'] != old_route['binary']:
                shutil.copy2(old_route['entry'], str(backup / 'launcher.sh'))
                require(digest(backup / 'launcher.sh') == old_route['entry_sha256'], 'Launcher changed during backup.')
            save_json(backup / 'service.json', {'config': config, 'properties': props, 'route': old_route})
            binary = candidate / 'release/bin/EYESY_SOURCE_BUILD'
            entry = binary.parent / 'run.sh' if self.layout.profile == BETA else binary
            body = '[Service]\nExecStart=\nExecStart=%s\nWorkingDirectory=%s\n' % (entry, binary.parent)
            require(not any(ch in str(binary) for ch in ' %\n\r\t'), 'Unsupported service path.')
            journal = {'schema': 3, 'profile': self.layout.profile, 'workspace':self.layout.workspace,
                       'peer': peer, 'phase': 'activating', 'id': state['id'], 'config': config,
                       'was_active': props['ActiveState'] == 'active', 'old_route': old_route,
                       'dropin_sha256': hashlib.sha256(body.encode('utf-8')).hexdigest(),
                       'starter': inventory(candidate / 'package/examples/HAUTLLE_MARKOV_LIVE_720_V1'),
                       'backup': str(backup), 'modes_backup_sha256': digest(backup / 'Modes.tar'),
                       'runtime_qualified': False}
            # Backups can take time: recheck the exact route/process before stopping it.
            current, fresh = service_config(self.host)
            require(current == config and all(fresh[k] == props[k] for k in
                    ('ActiveState', 'MainPID', 'NRestarts', 'NeedDaemonReload')), 'Service changed during backup.')
            require_route(fresh, self.layout, old_route)
            self.check_prior_process(fresh, old_route)
            require(self.host.peer_snapshot() == peer, 'Python renderer changed during backup.')
            save_json(self.layout.journal, journal)
            new_route = None
            try:
                self.host.control('stop')
                self.stopped()
                unchanged, stopped_props = service_config(self.host)
                require(unchanged == config, 'Service configuration changed before activation.')
                require_route(stopped_props, self.layout, old_route)
                # Construct the mode out of the menu, then rename it into place atomically.
                staging = self.layout.retired / state['id']
                no_links(staging)
                staging.mkdir(parents=True)
                shutil.copytree(str(candidate / 'package/examples/HAUTLLE_MARKOV_LIVE_720_V1'), str(staging / STARTER))
                self.host.own_mode(staging / STARTER)
                require(not self.layout.starter.exists(), 'Starter mode appeared during activation.')
                os.rename(str(staging / STARTER), str(self.layout.starter))
                self.layout.dropin.parent.mkdir(parents=True, exist_ok=True)
                exclusive_text(self.layout.dropin, body)
                self.host.control('daemon-reload')
                after, effective = service_config(self.host, self.layout.dropin)
                require(after == config and executable_path(effective) == entry and
                        effective['WorkingDirectory'] == str(binary.parent) and effective['NeedDaemonReload'] == 'no',
                        'Service did not select the staged release.')
                new_route = launch_route(effective, self.layout)
                require(new_route['binary_sha256'] == state['release']['bin/EYESY_SOURCE_BUILD']['sha256'],
                        'Staged engine changed before startup.')
                restarts = self.stopped()['NRestarts']
                print('Starting the prepared engine; checking process identity.', flush=True)
                require(self.host.peer_snapshot() == peer, 'Python renderer changed before startup.')
                self.host.log_check()
                self.host.control('start')
                self.host.wait_route_healthy(new_route, restarts, self.layout.modes / 'oFLua')
                require(self.host.peer_snapshot() == peer, 'Python renderer changed during startup.')
                require_route(self.host.properties(), self.layout, new_route)
                journal['phase'] = 'active'
                save_json(self.layout.journal, journal)
            except BaseException as error:
                journal['error'] = str(error)
                # A peer may have appeared after our final pre-start check. Stop
                # only the exact candidate process we started, even if a separate
                # configuration conflict will prevent automatic rollback.
                if new_route is not None:
                    try:
                        running = self.host.properties()
                        if running['ActiveState'] == 'active' and self.host.route_process_matches(
                                running, new_route, self.layout.modes / 'oFLua'):
                            self.host.control('stop')
                            self.stopped()
                    except (InstallError, OSError, subprocess.SubprocessError) as stop_error:
                        journal['candidate_stop_error'] = str(stop_error)
                try:
                    self.restore(journal)
                except BaseException as recovery:
                    journal.update(phase='recovery_needed', recovery_error=str(recovery))
                    save_json(self.layout.journal, journal)
                    raise InstallError('Activation failed; automatic recovery could not finish: %s. '
                                       'Keep this folder and run rollback after resolving the reported conflict.' % recovery) from error
                raise InstallError('Activation failed; previous startup choice and running state were restored: ' + str(error)) from error
        print('Activated. Select HAUTLLE_START_HERE on the EYESY. Check the picture, sound and PERSIST.\n'
              'Undo: sudo python3 install.py rollback\nBackups: ' + str(backup))

    def check_prior_process(self, props, route):
        if props['ActiveState'] == 'active':
            require(int(props['MainPID']) > 0 and self.host.route_process_matches(props, route, self.layout.modes / 'oFLua'),
                'Current process differs from the configured engine; resolve this before activation.')

    def restoration_preflight(self, journal):
        require(journal.get('profile', CUSTOM) == self.layout.profile, 'Recovery profile differs.')
        require(journal.get('workspace','home') == self.layout.workspace, 'Recovery workspace differs.')
        if journal.get('schema') == 3:
            require(self.host.peer_snapshot(stopped=False) == journal['peer'], 'Python renderer configuration changed.')
        config, props = service_config(self.host, self.layout.dropin)
        require(config == journal['config'], 'Startup configuration changed; refusing to overwrite someone else\'s changes.')
        no_links(self.layout.dropin)
        require(not self.layout.dropin.exists() or digest(self.layout.dropin) == journal['dropin_sha256'],
                'Installer drop-in was edited; recovery requires maintainer review.')
        if journal.get('schema') == 1:
            # Recover existing direct-executable journals, never reinterpret an old
            # launcher-as-binary record as permission to start an unrecorded engine.
            old = Path(journal['old_binary'])
            require(old != self.layout.canonical_launcher, 'Legacy journal lacks canonical engine identity; maintainer review required.')
            expected = dict(props, ExecStart='{ path=%s ; }' % old, WorkingDirectory=journal['old_cwd'])
            route = launch_route(expected, self.layout)
            require(route['binary_sha256'] == journal['old_binary_sha256'], 'Prior engine changed or disappeared.')
        else:
            require(journal.get('schema') in (2, 3), 'Unsupported recovery journal schema.')
            route = journal['old_route']
            expected = dict(props, ExecStart='{ path=%s ; }' % route['entry'],
                            WorkingDirectory=route['service_cwd'])
            require_route(expected, self.layout, route)
            if route['entry'] != route['binary']:
                saved = Path(journal['backup']) / 'launcher.sh'
                no_links(saved)
                require(saved.is_file() and digest(saved) == route['entry_sha256'], 'Launcher backup changed or disappeared.')
        return route

    def restore(self, journal):
        old_route = self.restoration_preflight(journal)
        journal['phase'] = 'rolling_back'
        try:
            save_json(self.layout.journal, journal)
        except OSError as error:
            print('Cannot update the recovery journal; attempting restoration using its saved transaction: ' + str(error))
        self.host.control('stop')
        self.stopped()
        if self.layout.dropin.exists():
            self.layout.dropin.unlink()
            sync_directory(self.layout.dropin.parent)
        self.host.control('daemon-reload')
        config, props = service_config(self.host)
        require(config == journal['config'] and props['NeedDaemonReload'] == 'no', 'Prior service settings were not restored.')
        require_route(props, self.layout, old_route)
        if self.layout.starter.exists():
            if inventory(self.layout.starter) == journal['starter']:
                retired = self.layout.retired / journal['id'] / STARTER
                no_links(retired)
                retired.parent.mkdir(parents=True, exist_ok=True)
                require(not retired.exists(), 'Retired starter destination already exists.')
                os.rename(str(self.layout.starter), str(retired))
            else:
                print('The starter mode was edited; it has been kept in the mode menu.')
        # Restore the saved unit, but never start either OF engine alongside Python.
        # On a conflict the OF unit remains stopped and recovery stays pending.
        self.host.peer_snapshot()
        if journal['was_active']:
            self.host.log_check()
            restarts = self.stopped()['NRestarts']
            self.host.control('start')
            try:
                self.host.wait_route_healthy(old_route, restarts, self.layout.modes / 'oFLua')
                self.host.peer_snapshot()
                require_route(self.host.properties(), self.layout, old_route)
            except BaseException:
                running = self.host.properties()
                if running['ActiveState'] == 'active' and self.host.route_process_matches(
                        running, old_route, self.layout.modes / 'oFLua'):
                    self.host.control('stop')
                    self.stopped()
                raise
        else:
            self.stopped()
        journal['phase'] = 'rolled_back'
        save_json(self.layout.journal, journal)
        self.retire_pointer(journal)

    def retire_pointer(self, journal):
        pointer = self.layout.work / 'prepared.json'
        no_links(pointer)
        if pointer.exists():
            prepared = read_json(pointer)
            if prepared.get('id') == journal['id']:
                prepared['state'] = 'rolled_back'
                save_json(pointer, prepared)

    def rollback(self, dry=False):
        self.host.platform_check(self.layout, recovery=True)
        self.host.secure_state(self.layout)
        no_links(self.layout.state); no_links(self.layout.journal)
        require(self.layout.journal.exists(), 'No installation journal; there is nothing to undo.')
        if dry:
            self.restoration_preflight(read_json(self.layout.journal))
            print('Dry run: would restore the recorded previous startup choice and running state.\n'
                  'Original modes and user settings are never overwritten.')
            return
        with locked(self.layout.state):
            journal = read_json(self.layout.journal)
            self.layout.candidate(journal['id'])
            if journal['phase'] == 'rolled_back':
                self.retire_pointer(journal)
                print('Already rolled back; nothing changed.')
                return
            self.restore(journal)
        print('Previous startup choice and running state restored. Backups and built files are retained.')

    def cleanup_build(self, dry=False):
        self.host.platform_check(self.layout, recovery=True)
        self.host.workspace_check(self.layout)
        no_links(self.layout.work/'prepared.json')
        if dry:
            pointer = read_json(self.layout.work/'prepared.json')
            require(pointer.get('workspace','home') == self.layout.workspace, 'Cleanup workspace differs.')
            candidate = self.layout.candidate(pointer['id'])
            require(pointer.get('temporary_swap') is True, 'This candidate did not request temporary swap.')
            no_links(candidate/'build-resources.json')
            require((candidate/'build-resources.json').is_file(), 'No temporary-resource journal; maintainer review required.')
            print('Dry run: would inspect the saved build group and clean only its recorded temporary swap file.')
            return
        with locked(self.layout.work):
            pointer = read_json(self.layout.work/'prepared.json')
            require(pointer.get('workspace','home') == self.layout.workspace, 'Cleanup workspace differs.')
            candidate = self.layout.candidate(pointer['id'])
            require(pointer.get('temporary_swap') is True, 'This candidate did not request temporary swap.')
            no_links(candidate/'build-resources.json')
            require((candidate/'build-resources.json').is_file(), 'No temporary-resource journal; maintainer review required.')
            support_module('build_resources').BuildResources(candidate,self.layout.work_parent,self.layout.root).cleanup(recovering=True)
            if pointer.get('state') == 'building':
                pointer.update(state='failed',error='Interrupted build cleaned; a fresh prepare is required.')
                save_json(self.layout.work/'prepared.json',pointer)
        print('Temporary build swap cleanup verified. Build output and logs are retained.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', nargs='?', default='prepare', choices=['prepare', 'check', 'activate', 'rollback','cleanup-build',
                        'controller-check','controller-apply','controller-rollback','web-check','web-apply','web-rollback'])
    parser.add_argument('--dry-run', action='store_true', help='Read-only prerequisite/plan check; no build or service changes')
    parser.add_argument('--profile', choices=['auto', CUSTOM, BETA], default='auto',
                        help='Reviewed custom pi or inspected 20211213 beta profile; stock OS3 is unsupported')
    parser.add_argument('--workspace', choices=['home','sdcard'],
                        help='Fixed validated work location; defaults to home, or the active recovery journal')
    parser.add_argument('--temporary-swap', action='store_true',
                        help='Explicitly permit one temporary 1 GiB swap file for guarded preparation; requires sdcard workspace')
    args = parser.parse_args(argv)
    try:
        if args.action.startswith('web-'):
            require(not args.temporary_swap and args.workspace is None and args.profile in ('auto',BETA),
                    'Web actions are a separate beta-only step; omit build/workspace options.')
            action=args.action[len('web-'):]
            return support_module('beta_web').main('check' if args.dry_run else action)
        if args.action.startswith('controller-'):
            require(not args.temporary_swap and args.workspace is None and args.profile in ('auto',BETA),
                    'Controller actions are a separate beta-only step; omit build/workspace options.')
            action=args.action[len('controller-'):]
            return support_module('beta_controller').main('check' if args.dry_run else action)
        require(not args.temporary_swap or args.action in ('prepare','check'), 'Temporary swap consent is for preparation only.')
        installer = Installer(layout=detect_layout(profile=args.profile, workspace=args.workspace),temporary_swap=args.temporary_swap)
        if args.action in ('prepare', 'check'):
            installer.prepare(dry=args.dry_run or args.action == 'check')
        elif args.action == 'activate':
            installer.activate(dry=args.dry_run)
        elif args.action == 'cleanup-build':
            installer.cleanup_build(dry=args.dry_run)
        else:
            installer.rollback(dry=args.dry_run)
        return 0
    except (InstallError, OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print('Stopped: ' + str(error), file=sys.stderr)
        print('No packages or network settings were changed. Keep the installer output and logs.', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('Interrupted. Keep the candidate folder. Use rollback if activation was interrupted.', file=sys.stderr)
        return 130
