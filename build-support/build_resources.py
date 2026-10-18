"""Opt-in temporary swap and bounded build supervision; no persistent settings."""
# Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see ../LICENSE.txt.
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import time

MIB = 1024 ** 2
SWAP_BYTES = 1024 * MIB
POLICY = dict(address_space_MiB=1024, swap_MiB=1024, initial_RAM_MiB=512,
              RAM_hard_MiB=32, RAM_sustained_MiB=64, RAM_sustained_seconds=3,
              swap_free_MiB=256, combined_free_MiB=384,
              data_stop_MiB=640, root_stop_MiB=320, sample_seconds=0.25)


class ResourceError(RuntimeError):
    pass


def no_links(path):
    for part in [Path(path)] + list(Path(path).parents):
        if part.is_symlink():
            raise ResourceError('Symlink in build resource path: ' + str(part))


def guard_reason(memory, data, root, elapsed, timeout, low_duration):
    if memory['MemAvailable'] < 32*MIB: return 'RAM below 32 MiB'
    if memory['MemAvailable'] < 64*MIB and low_duration >= 3: return 'RAM below 64 MiB for 3 seconds'
    if memory['SwapFree'] < 256*MIB: return 'Swap free below 256 MiB'
    if memory['MemAvailable'] + memory['SwapFree'] < 384*MIB: return 'Combined headroom below 384 MiB'
    if data < 640*MIB: return 'Data free below 640 MiB'
    if root < 320*MIB: return 'Root free below 320 MiB'
    if elapsed > timeout: return 'Build stage time limit'


def child_limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (1024*MIB, 1024*MIB))
    Path('/proc/self/oom_score_adj').write_text('1000')


class Operations:
    def memory(self):
        return {s.split(':')[0]: int(s.split()[1])*1024
                for s in Path('/proc/meminfo').read_text().splitlines() if len(s.split()) >= 2}

    def swaps(self):
        return {s.split()[0]: int(s.split()[3])*1024
                for s in Path('/proc/swaps').read_text().splitlines()[1:]}

    def free(self, path):
        return shutil.disk_usage(str(path)).free

    def privileged(self, args):
        subprocess.run(['sudo', '-n'] + args, check=True, timeout=45,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def group_exists(self, group):
        try:
            os.killpg(group, 0)
            return True
        except ProcessLookupError:
            return False

    def launch(self, command, cwd, log, env):
        return subprocess.Popen(command, cwd=str(cwd), env=env, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True,
                                preexec_fn=child_limits)

    def signal_group(self, group, sig):
        try: os.killpg(group, sig)
        except ProcessLookupError: pass

    def sleep(self, seconds):
        time.sleep(seconds)

    def clock(self):
        return time.monotonic()


def preflight(data_root, system_root, ops=None):
    ops = ops or Operations()
    if ops.swaps(): raise ResourceError('Existing swap is outside this temporary-swap profile; leave it unchanged.')
    if ops.memory()['MemAvailable'] < 512*MIB: raise ResourceError('At least 512 MiB available RAM is required before the build.')
    if ops.free(data_root) < SWAP_BYTES + 800*MIB: raise ResourceError('Insufficient space for temporary swap and build reserve.')
    if ops.free(system_root) < 320*MIB: raise ResourceError('Root free space is below 320 MiB.')


class BuildResources:
    def __init__(self, candidate, data_root, system_root=Path('/'), ops=None):
        self.candidate, self.data_root, self.system_root = map(Path, (candidate, data_root, system_root))
        self.path = self.candidate / 'temporary-build.swap'
        self.record = self.candidate / 'build-resources.json'
        self.ops = ops or Operations()
        self.active = None
        self.identity = None
        self.started = False
        self.state = dict(schema=1, complete=False, swap_cleanup_verified=False,
                          policy=POLICY, phase='initial', swap_path=str(self.path),
                          started=time.time(), stages=[])

    def save(self, durable=True):
        no_links(self.record)
        tmp = self.record.with_name(self.record.name + '.tmp')
        no_links(tmp)
        with tmp.open('w', encoding='utf-8') as handle:
            json.dump(self.state, handle, indent=2);handle.write('\n');handle.flush()
            if durable: os.fsync(handle.fileno())
        os.replace(str(tmp), str(self.record))
        if durable and os.name == 'posix':
            fd = os.open(str(self.record.parent),os.O_RDONLY | os.O_DIRECTORY)
            try: os.fsync(fd)
            finally: os.close(fd)

    def verify_identity(self):
        no_links(self.path)
        st = self.path.lstat()
        if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1 or [st.st_dev, st.st_ino] != self.identity:
            raise ResourceError('Temporary swap identity changed; file retained for review.')

    def allocate(self):
        preflight(self.data_root, self.system_root, self.ops)
        no_links(self.candidate);no_links(self.path)
        if self.record.exists(): raise ResourceError('Resource journal already exists; use build cleanup before retrying.')
        if self.path.exists(): raise ResourceError('Temporary swap path already exists; preserved for review.')
        # Fail before allocating a large file if noninteractive privilege is absent.
        self.ops.privileged(['true'])
        self.save()
        self.started = True
        fd = os.open(str(self.path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as handle:
            st = os.fstat(handle.fileno());self.identity = [st.st_dev, st.st_ino]
            self.state.update(swap_identity=self.identity, phase='allocating');self.save()
            written = 0
            while written < SWAP_BYTES:
                count = min(MIB, SWAP_BYTES-written);handle.write(b'\0'*count);written += count
                if written % (16*MIB) == 0:
                    handle.flush();os.fsync(handle.fileno())
                    if self.ops.free(self.data_root) < 800*MIB:
                        raise ResourceError('Swap allocation would consume build reserve.')
            handle.flush();os.fsync(handle.fileno())
        self.verify_identity()
        self.ops.privileged(['chown', 'root:root', str(self.path)])
        self.verify_identity()
        self.ops.privileged(['/sbin/mkswap', str(self.path)])
        self.verify_identity()
        self.ops.privileged(['/sbin/swapon', str(self.path)])
        self.verify_identity()
        if set(self.ops.swaps()) != {str(self.path)}:
            raise ResourceError('Temporary swap activation differs from the approved file.')
        self.state['phase'] = 'ready';self.save()

    def terminate(self):
        if self.active is None: return
        group = self.active.pid
        # Kill the owned group even if its leader exited while a compiler survived.
        if self.ops.group_exists(group): self.ops.signal_group(group, signal.SIGTERM)
        try: self.active.wait(timeout=3)
        except subprocess.TimeoutExpired: pass
        if self.ops.group_exists(group): self.ops.signal_group(group, signal.SIGKILL)
        self.active.wait(timeout=5)
        deadline = self.ops.clock() + 5
        while self.ops.group_exists(group) and self.ops.clock() < deadline: self.ops.sleep(0.1)
        if self.ops.group_exists(group): raise ResourceError('Build process group remains; swap retained.')
        self.active = None
        self.state['child_group'] = None

    def run(self, command, label, timeout):
        self.state.update(phase=label, command=list(command));self.save()
        scratch = self.candidate / 'compiler-tmp';no_links(scratch);scratch.mkdir(exist_ok=True)
        env = dict(os.environ, TMPDIR=str(scratch), PYTHONDONTWRITEBYTECODE='1')
        start = self.ops.clock();low = None;code = None
        with (self.candidate/(label+'.log')).open('x') as log:
            self.active = self.ops.launch(command, self.candidate, log, env)
            self.state['child_group'] = self.active.pid;self.save()
            try:
                while self.active.poll() is None:
                    now = self.ops.clock();m = self.ops.memory()
                    low = (now if low is None else low) if m['MemAvailable'] < 64*MIB else None
                    data, root = self.ops.free(self.data_root), self.ops.free(self.system_root)
                    self.state.update(updated=time.time(), minimum_available=min(self.state.get('minimum_available',m['MemAvailable']),m['MemAvailable']),
                                      maximum_swap_used=max(self.state.get('maximum_swap_used',0),m['SwapTotal']-m['SwapFree']),
                                      minimum_data_free=min(self.state.get('minimum_data_free',data),data),
                                      minimum_root_free=min(self.state.get('minimum_root_free',root),root))
                    self.save(durable=False)
                    why = guard_reason(m, data, root, now-start, timeout, 0 if low is None else now-low)
                    if why: raise ResourceError(why)
                    self.ops.sleep(0.25)
                code = self.active.wait()
                if code: raise ResourceError('%s failed with exit %d' % (label,code))
            finally:
                self.terminate()
                self.state['stages'].append(dict(label=label, command=list(command), exit=code, seconds=self.ops.clock()-start))
                self.save()

    def cleanup(self, recovering=False):
        if recovering:
            no_links(self.record)
            self.state = json.loads(self.record.read_text(encoding='utf-8'))
            if self.state.get('schema') != 1 or self.state.get('swap_path') != str(self.path):
                raise ResourceError('Resource journal path/schema differs.')
            self.identity = self.state.get('swap_identity')
            group = self.state.get('child_group')
            if group is not None and (not isinstance(group,int) or group <= 1 or self.ops.group_exists(group)):
                raise ResourceError('Build group may still be running; cleanup did not signal or change it.')
        self.terminate()
        self.state['phase'] = 'cleanup';self.save()
        if self.path.exists() or self.path.is_symlink():
            self.verify_identity()
            if str(self.path) in self.ops.swaps():
                for _ in range(30):
                    used = self.ops.swaps().get(str(self.path))
                    if used is None or self.ops.memory()['MemAvailable'] >= used + 128*MIB: break
                    self.ops.sleep(1)
                if used is not None:
                    if self.ops.memory()['MemAvailable'] < used + 128*MIB:
                        raise ResourceError('Insufficient RAM for swapoff; temporary file retained active.')
                    self.verify_identity();self.ops.privileged(['/sbin/swapoff', str(self.path)])
            if str(self.path) in self.ops.swaps(): raise ResourceError('Temporary swap remains active; never deleting it.')
            self.verify_identity();self.path.unlink()
        if str(self.path) in self.ops.swaps(): raise ResourceError('Swap remains active despite missing path; maintainer review required.')
        self.state.update(swap_cleanup_verified=True, finished=time.time(), phase='cleaned');self.save()

    @contextmanager
    def session(self):
        handlers = {}
        def interrupted(signum, frame): raise ResourceError('Build interrupted by signal %d' % signum)
        for name in ('SIGINT','SIGTERM','SIGHUP'):
            sig = getattr(signal,name,None)
            if sig is not None:
                handlers[sig] = signal.signal(sig,interrupted)
        try:
            try:
                self.allocate()
                yield self
                self.state['complete'] = True
            except BaseException as exc:
                self.state['error'] = str(exc)
                raise
            finally:
                try:
                    if self.started: self.cleanup()
                except BaseException as exc:
                    self.state.update(cleanup_error=str(exc),swap_cleanup_verified=False)
                    self.save()
                    raise ResourceError('Build cleanup needs attention: %s. Run cleanup-build with the same workspace.' % exc) from exc
        finally:
            for sig,handler in handlers.items(): signal.signal(sig,handler)
