"""Disposable beta fixtures; never execute imported image files or systemctl."""
import copy
from pathlib import Path
import unittest
from unittest import mock
import test_installer as base

mod, put = base.mod, base.put
LAUNCHER = ('#!/bin/bash\nexport LUA_PATH="/home/music/EYESY_OS/engines/oflua/?.lua;;"\n\n'
            '# make sure log ownership is not root\nsudo chown music:music /tmp/video.log\n\n'
            'cd /home/music/openFrameworks/apps/myApps/eyesy\nstdbuf -o0 bin/eyesy &> /tmp/video.log\n')
UNIT = ('[Unit] \nDescription=oFLua Video Engine\n\n[Service] \nType=simple \n'
        'WorkingDirectory=/home/music/EYESY_OS/engines/oflua\n'
        'ExecStart=/home/music/EYESY_OS/engines/oflua/run.sh\nUser=music\nGroup=music\n'
        '\n[Install] \nWantedBy=multi-user.target\n')


class BetaHost(base.FakeHost):
    def __init__(self, layout):
        super().__init__(layout)
        self.unit = layout.root / 'usr/lib/systemd/system/eyesy-oflua.service'
        self.old, self.launch_entry = layout.canonical_binary, layout.canonical_launcher
        self.working = self.launch_entry.parent
        self.peer = layout.root / 'usr/lib/systemd/system/eyesy-python.service'
        self.peer_active, self.peer_pid, self.peer_enabled = 'inactive', '0', 'disabled'
        self.shell = layout.root / 'usr/bin/bash'
        self.children = {'123', '124'}
        self.wrong_parent = self.wrong_uid = self.wrong_group = self.wrong_child = False
        self.peer_on_start = False
        for path, body in [(self.old, 'beta fixture engine'), (self.shell, 'fixture bash'),
                           (self.peer, '[Service]\nExecStart=python\n')]:
            put(path, body)
        for path, body in [(self.unit, UNIT), (self.launch_entry, LAUNCHER)]:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body.encode())
        put(layout.root / 'etc/passwd', 'music:x:1000:1000::/home/music:/bin/bash\n')

    def platform_check(self, layout, build=False, recovery=False):
        super().platform_check(layout, build, recovery)
        if not recovery:
            mod.beta_identity(layout)

    def properties(self):
        p = super().properties()
        p.update(User='music', Group='music', ControlGroup='/system.slice/eyesy-oflua.service',
                 Type='simple', KillMode='control-group')
        return p

    def unit_properties(self, unit, missing=False):
        assert unit == mod.PEER_SERVICE
        return dict(LoadState='loaded', ActiveState=self.peer_active, MainPID=self.peer_pid,
                    NeedDaemonReload='no', FragmentPath=str(self.peer), DropInPaths='',
                    UnitFileState=self.peer_enabled, ExecStart='python', WorkingDirectory='python',
                    User='music', Group='music')

    peer_snapshot = mod.Host.peer_snapshot

    def identity(self, pid):
        props = self.properties()
        entry = mod.executable_path(props)
        if entry == self.layout.canonical_launcher:
            if str(pid) == '123' and len(self.children) > 1:
                return str(self.shell), str(self.working)
            return str(self.shell if self.wrong_child else self.old), str(self.selected_mode or self.layout.canonical_cwd)
        return str(entry.parent / 'EYESY_SOURCE_BUILD'), str(self.selected_mode or entry.parent)

    def unit_pids(self, group):
        assert group == '/system.slice/eyesy-oflua.service'
        return self.children.copy()

    def process_record(self, pid):
        return {'parent': '999' if self.wrong_parent else ('123' if pid == '124' else '1'),
                'start': '100', 'uids': ['0' if self.wrong_uid else '1000'] * 4,
                'groups': ['/other' if self.wrong_group else '/system.slice/eyesy-oflua.service'],
                'argv': [str(self.layout.root / 'bin/bash'), str(self.launch_entry)]}

    def wait_route_healthy(self, route, restarts, modes):
        mod.require(self.active and self.restarts == restarts and
                    self.route_process_matches(self.properties(), route, modes), 'Fixture beta identity failed.')

    def control(self, verb):
        super().control(verb)
        if verb == 'start' and self.peer_on_start:
            self.peer_active, self.peer_pid = 'active', '999'


class BetaTests(unittest.TestCase):
    def setUp(self):
        base.InstallerTests.setUp(self)
        self.layout = mod.Layout(self.root, mod.BETA)
        self.host = BetaHost(self.layout)
        self.installer = mod.Installer(self.package, self.layout, self.host)
        patch = mock.patch.object(mod, 'BETA_BINARY_SHA256', mod.digest(self.host.old))
        patch.start(); self.addCleanup(patch.stop)
        # Only the large executable pin is substituted; unit/launcher are exact bytes.
        self.assertEqual(mod.digest(self.host.unit), mod.BETA_UNIT_SHA256)
        self.assertEqual(mod.digest(self.host.launch_entry), mod.BETA_LAUNCHER_SHA256)

    def test_known_profile_and_account(self):
        mod.beta_identity(self.layout)
        self.assertEqual(self.layout.service, 'eyesy-oflua.service')
        self.assertEqual(self.layout.home, self.root / 'home/music')
        self.assertIn('eyesy-oflua.service.d', str(self.layout.dropin))

    def test_auto_rejects_ambiguous_profiles(self):
        with mock.patch.object(mod.platform, 'system', return_value='Linux'):
            with self.assertRaisesRegex(mod.InstallError, 'ambiguous'):
                mod.detect_layout(self.root)
            self.assertEqual(mod.detect_layout(self.root, mod.BETA).profile, mod.BETA)

    def test_wrong_account_unit_launcher_binary_each_rejected(self):
        for path in [self.layout.root / 'etc/passwd', self.host.unit, self.host.launch_entry, self.host.old]:
            data = path.read_bytes()
            path.write_bytes(b'changed')
            with self.assertRaises(mod.InstallError):
                self.installer.prepare(dry=True)
            path.write_bytes(data)
        self.assertFalse(self.layout.work.exists())
        self.assertEqual(self.host.calls, [])

    def test_dry_run_has_no_writes(self):
        before = mod.inventory(self.root)
        self.installer.prepare(dry=True)
        self.assertEqual(mod.inventory(self.root), before)

    def test_small_root_space_is_rejected(self):
        with mock.patch.object(mod.shutil, 'disk_usage', return_value=type('Usage', (), {'free': 461447168})()):
            with self.assertRaisesRegex(mod.InstallError, '1 GiB'):
                self.installer.prepare()
        self.assertFalse(self.layout.work.exists())

    def test_activation_rollback_preserves_originals_and_peer_enablement(self):
        self.host.selected_mode = self.layout.modes / 'oFLua/Existing'
        original = {p: p.read_bytes() for p in [self.host.unit, self.host.launch_entry, self.host.old, self.host.peer]}
        self.installer.prepare(); self.installer.activate()
        journal = mod.read_json(self.layout.journal)
        self.assertEqual(journal['profile'], mod.BETA)
        self.assertTrue(journal['old_route']['shell_parent'])
        self.assertTrue((Path(journal['backup']) / 'launcher.sh').is_file())
        self.assertTrue((Path(journal['backup']) / 'python-service-00.conf').is_file())
        entry = mod.executable_path(self.host.properties())
        body = entry.read_text()
        self.assertIn('exec /usr/bin/stdbuf -o0', body)
        self.assertIn('LUA_PATH=', body)
        self.installer.rollback(); self.installer.rollback()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.assertEqual(self.host.peer_enabled, 'disabled')
        for path, data in original.items(): self.assertEqual(path.read_bytes(), data)

    def test_inactive_original_is_restored_inactive(self):
        self.host.active = False
        self.installer.prepare(); self.installer.activate(); self.installer.rollback()
        self.assertFalse(self.host.active)

    def test_candidate_start_failure_recovers_shell_engine(self):
        self.installer.prepare(); self.host.fail_new_start = True
        with self.assertRaisesRegex(mod.InstallError, 'were restored'):
            self.installer.activate()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())

    def test_active_transitional_and_orphan_peer_pid_rejected(self):
        for state, pid in [('active', '999'), ('activating', '0'), ('deactivating', '0'), ('inactive', '999')]:
            self.host.peer_active, self.host.peer_pid = state, pid
            with self.assertRaisesRegex(mod.InstallError, 'Python renderer'):
                self.installer.prepare(dry=True)
        self.assertEqual(self.host.calls, [])

    def test_python_start_during_activation_leaves_of_stopped_for_recovery(self):
        self.installer.prepare(); self.host.peer_on_start = True
        with self.assertRaisesRegex(mod.InstallError, 'automatic recovery could not finish'):
            self.installer.activate()
        self.assertFalse(self.host.active)
        self.assertEqual(self.host.peer_active, 'active')
        self.assertFalse(self.layout.dropin.exists())
        self.assertEqual(mod.read_json(self.layout.journal)['phase'], 'recovery_needed')
        self.host.peer_on_start = False
        self.host.peer_active, self.host.peer_pid = 'inactive', '0'
        self.installer.rollback()
        self.assertTrue(self.host.active)

    def test_changed_peer_configuration_blocks_rollback(self):
        self.installer.prepare(); self.installer.activate()
        put(self.host.peer, 'user edited config')
        before = list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'Python renderer configuration changed'):
            self.installer.rollback()
        self.assertEqual(self.host.calls, before)

    def test_wrong_prepared_or_journal_profile_rejected(self):
        self.installer.prepare()
        pointer = self.layout.work / 'prepared.json'
        state = mod.read_json(pointer)
        state['profile'] = mod.CUSTOM
        mod.save_json(pointer, state)
        mod.save_json(self.layout.candidate(state['id']) / 'candidate.json', state)
        with self.assertRaisesRegex(mod.InstallError, 'profile differs'):
            self.installer.activate()

    def route(self):
        return mod.launch_route(self.host.properties(), self.layout)

    def matches(self):
        return self.host.route_process_matches(self.host.properties(), self.route(), self.layout.modes / 'oFLua')

    def test_shell_parent_child_and_direct_exec_are_both_recognized(self):
        self.assertTrue(self.matches())
        self.host.children = {'123'}
        self.assertTrue(self.matches())

    def test_wrong_child_parent_uid_cgroup_or_extra_process_rejected(self):
        for flag in ('wrong_child', 'wrong_parent', 'wrong_uid', 'wrong_group'):
            setattr(self.host, flag, True)
            self.assertFalse(self.matches(), flag)
            setattr(self.host, flag, False)
        self.host.children.add('125')
        self.assertFalse(self.matches())

    def test_pid_reuse_during_observation_rejected(self):
        original = self.host.process_record
        counts = {}
        def record(pid):
            counts[pid] = counts.get(pid, 0) + 1
            result = original(pid)
            result['start'] = str(counts[pid])
            return result
        with mock.patch.object(self.host, 'process_record', side_effect=record):
            self.assertFalse(self.matches())

    def test_interpreter_drift_blocks_rollback(self):
        self.installer.prepare(); self.installer.activate()
        put(self.host.shell, 'new bash')
        with self.assertRaisesRegex(mod.InstallError, 'Launch route changed'):
            self.installer.rollback()

    def test_peer_configuration_drift_before_start_aborts(self):
        self.installer.prepare()
        original = self.host.control
        def control(verb):
            original(verb)
            if verb == 'daemon-reload': self.host.peer_enabled = 'enabled'
        with mock.patch.object(self.host, 'control', side_effect=control):
            with self.assertRaises(mod.InstallError): self.installer.activate()
        self.assertNotIn('start', self.host.calls)

    def test_peer_appears_during_rollback_start_leaves_of_stopped(self):
        self.installer.prepare(); self.installer.activate()
        self.host.peer_on_start = True
        with self.assertRaisesRegex(mod.InstallError, 'Python renderer'):
            self.installer.rollback()
        self.assertFalse(self.host.active)
        self.assertEqual(self.host.peer_active, 'active')

    def test_auto_detects_beta_without_custom_unit(self):
        (self.root / 'etc/systemd/system/eyesyof.service').unlink()
        with mock.patch.object(mod.platform, 'system', return_value='Linux'):
            self.assertEqual(mod.detect_layout(self.root).profile, mod.BETA)

    def test_proc_parser_and_cgroup_membership_file(self):
        group = '/system.slice/eyesy-oflua.service'
        put(self.root / ('sys/fs/cgroup/systemd' + group) / 'cgroup.procs', '123\n124\n')
        proc = self.root / 'proc/124'
        # Field 22 is starttime; the process name includes spaces and parentheses.
        fields = ['S', '123'] + ['0'] * 17 + ['87654']
        put(proc / 'stat', '124 (engine (name)) ' + ' '.join(fields))
        put(proc / 'status', 'Name:\tengine\nUid:\t1000\t1000\t1000\t1000\n')
        put(proc / 'cgroup', '2:cpu:/other\n1:name=systemd:' + group + '\n')
        (proc / 'cmdline').write_bytes(b'engine\0--flag\0')
        host = mod.Host(self.layout)
        self.assertEqual(host.unit_pids(group), {'123', '124'})
        self.assertEqual(host.process_record('124'), dict(parent='123', start='87654',
            uids=['1000'] * 4, groups=[group], argv=['engine', '--flag']))
        with self.assertRaises(mod.InstallError): host.unit_pids('/unrelated')

    def test_wrong_shell_arguments_or_missing_child_rejected(self):
        original = self.host.process_record
        def record(pid):
            result = original(pid)
            if pid == '123': result['argv'] = [str(self.host.shell), '/unrelated/script.sh']
            return result
        with mock.patch.object(self.host, 'process_record', side_effect=record):
            self.assertFalse(self.matches())
        with mock.patch.object(self.host, 'process_record', side_effect=FileNotFoundError):
            self.assertFalse(self.matches())

