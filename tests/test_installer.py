"""Temporary-filesystem transactions with a fake service/build; no device calls."""
import contextlib
import importlib.util
import io
import itertools
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

PACKAGE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', PACKAGE / 'build-support/installer.py')
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def json_file(path, data):
    put(path, json.dumps(data))


def seal(package):
    json_file(package / mod.MANIFEST, {p.relative_to(package).as_posix(): mod.digest(p)
              for p in package.rglob('*') if p.is_file() and p != package / mod.MANIFEST})


class FakeHost(mod.Host):
    def __init__(self, layout):
        self.layout, self.calls = layout, []
        self.active, self.restarts = True, '0'
        self.bad_platform = self.missing_dependency = self.fail_build = False
        self.incomplete = self.fail_new_start = self.ignored_dropin = False
        self.mode_owner_set = False
        self.identity_mismatch = False
        self.unit = layout.root / 'etc/systemd/system/eyesyof.service'
        self.old = layout.home / 'production/bin/engine'
        self.working = self.old.parent
        self.launch_entry = self.old
        self.selected_mode = None
        self.needs_reload = 'no'
        put(self.old, 'original production engine')
        put(self.unit, '[Service]\nExecStart=old\n')
        self.enabled = 'enabled'

    def peer_snapshot(self, stopped=True):
        return {'missing': True}

    def log_check(self):
        pass

    def platform_check(self, layout, build=False, recovery=False):
        mod.require(not self.bad_platform, 'Unsupported fixture platform')

    def prerequisite_packages(self, package):
        mod.require(not self.missing_dependency, 'Missing fixture development package')

    def workspace_check(self, layout):
        mod.no_links(layout.work_parent);mod.no_links(layout.work)

    def resource_check(self, temporary_swap):
        mod.require(not temporary_swap or self.layout.workspace == 'sdcard', 'Temporary swap requires --workspace sdcard.')

    def properties(self):
        binary, cwd, dropins = self.launch_entry, self.working, ''
        if self.layout.dropin.exists():
            dropins = self.layout.dropin.as_posix()
            if not self.ignored_dropin:
                body = self.layout.dropin.read_text(encoding='utf-8')
                binary = Path([line.split('=', 1)[1] for line in body.splitlines()
                               if line.startswith('ExecStart=') and line != 'ExecStart='][0])
                cwd = Path([line.split('=', 1)[1] for line in body.splitlines()
                            if line.startswith('WorkingDirectory=')][0])
        return {'LoadState': 'loaded', 'ActiveState': 'active' if self.active else 'inactive',
                'MainPID': '123' if self.active else '0', 'UnitFileState': self.enabled,
                'FragmentPath': self.unit.as_posix(), 'DropInPaths': dropins,
                'ExecStart': '{ path=%s ; argv[]=%s ; }' % (binary, binary),
                'WorkingDirectory': str(cwd), 'User': 'pi', 'NRestarts': self.restarts,
                'NeedDaemonReload': self.needs_reload}

    def control(self, verb):
        self.calls.append(verb)
        if verb == 'stop':
            self.active = False
        elif verb == 'start':
            if self.fail_new_start and self.layout.dropin.exists():
                raise mod.InstallError('Fixture engine startup failure')
            self.active = True

    def wait_healthy(self, binary, cwd, expected_restarts, modes=None):
        mod.require(self.active and self.restarts == expected_restarts and
                    self.process_matches('123', binary, cwd, modes),
                    'Fixture process identity failure')

    def identity(self, pid):
        props = self.properties()
        binary = mod.executable_path(props)
        cwd = Path(props['WorkingDirectory'])
        if binary == self.layout.canonical_launcher:
            binary, cwd = self.layout.canonical_binary, self.layout.canonical_cwd
        if self.identity_mismatch:
            binary = self.layout.home / 'unrelated/bin/engine'
        return str(binary), str(self.selected_mode or cwd)

    def own_mode(self, path):
        self.mode_owner_set = True

    def secure_state(self, layout):
        pass

    def run(self, command, timeout=30):
        # Production invokes the dependency validator here. That validator has its
        # own real unit tests; this fixture stands in only for its successful call.
        if '-c' in command:
            dep = mod.read_json(Path(command[-1]))
            mod.require(dep.get('complete') and dep.get('smoke_passed'), 'Incomplete dependency smoke')
            return ''
        raise AssertionError('Unexpected fixture subprocess: ' + repr(command))

    def build(self, package, candidate):
        if self.fail_build:
            raise mod.InstallError('Fixture compiler failure')
        deps = candidate / 'dependencies/dependency-build-result.json'
        json_file(deps, {'complete': True, 'smoke_passed': True})
        records = []
        for index, row in enumerate(mod.read_json(package / 'build-support/plan.json')['units']):
            source = package / 'source' / row['source'].replace('${SOURCE}/', '')
            obj = candidate / 'build/objects' / ('unit%d.o' % index)
            put(obj, 'fixture object %d' % index)
            records.append({'source': str(source), 'source_sha256': mod.digest(source),
                            'object': str(obj), 'object_sha256': mod.digest(obj)})
        binary = candidate / 'build/runtime/bin/EYESY_SOURCE_BUILD'
        put(binary, 'fixture new binary')
        binary.chmod(0o755)
        import shutil
        shutil.copytree(str(package / 'source/app/bin/data'), str(binary.parent / 'data'))
        result = {'complete': not self.incomplete, 'compiled': records,
                  'public_dependency_rebuild_used': True, 'binary': str(binary),
                  'binary_sha256': mod.digest(binary), 'dependency_build_result_sha256': mod.digest(deps)}
        for field, name in [('source_manifest_sha256', 'source-hashes.json'),
                            ('plan_sha256', 'plan.json'), ('dependency_manifest_sha256', 'dependencies.json')]:
            result[field] = mod.digest(package / 'build-support' / name)
        recipe=mod.support_module('build')
        profile='bindings-gc-v1' if getattr(self,'temporary_swap',False) else 'original'
        effective=recipe.memory_profile(recipe.expand_plan(mod.read_json(package/'build-support/plan.json'),
                    package/'source',candidate/'build',candidate/'dependencies',self.layout.root,
                    self.layout.root/'opt/vc','g++','ar'),profile)
        expanded=candidate/'build/expanded-plan.json';json_file(expanded,effective)
        result.update(compiler_memory_profile=profile,effective_plan_sha256=mod.digest(expanded))
        for row,unit in zip(records,effective['units']):
            row.update(command=unit['args'],command_sha256=recipe.command_sha256(unit['args']))
        if getattr(self,'temporary_swap',False):
            json_file(candidate/'build-resources.json',dict(complete=True,swap_cleanup_verified=True,
                      policy=mod.support_module('build_resources').POLICY))
        json_file(candidate / 'build/build-result.json', result)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('HAUTLLE_TEST_TMP'))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.layout = mod.Layout(self.root)
        self.package = self.root / 'source-package'
        put(self.layout.modes / 'oFLua/Existing/main.lua', 'keep this mode')
        put(self.layout.root / 'sdcard/Scenes/scene.json', 'keep scenes')
        put(self.layout.root / 'sdcard/settings.json', 'keep settings')
        put(self.package / 'source/app/bin/data/lua/helper.lua', 'fixture helper')
        put(self.package / 'examples/HAUTLLE_MARKOV_LIVE_720_V1/main.lua', 'fixture starter')
        units = []
        for index in range(97):
            name = 'app/unit%d.cpp' % index if index else 'addons/ofxLua/src/bindings/linuxarm/ofBindings.cpp'
            put(self.package / 'source' / name, 'fixture source %d' % index)
            units.append({'source': '${SOURCE}/' + name,
                          'args':['g++','-O3','-pipe','-c','${SOURCE}/'+name,'-o','${BUILD}/objects/unit%d.o'%index]})
        json_file(self.package / 'build-support/plan.json', {'units': units,
                  'archive':['ar','rcs','${BUILD}/core.a'], 'link':['g++','-o','${BUILD}/runtime/bin/EYESY_SOURCE_BUILD']})
        json_file(self.package / 'build-support/source-hashes.json', {'fixture': True})
        json_file(self.package / 'build-support/dependencies.json', {})
        seal(self.package)
        self.host = FakeHost(self.layout)
        self.installer = mod.Installer(self.package, self.layout, self.host)
        self.output = io.StringIO()
        self.redirect = contextlib.redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)
        self.original_mode = mod.inventory(self.layout.modes)
        self.original_binary = mod.digest(self.host.old)
        self.original_unit = mod.digest(self.host.unit)

    def candidate(self):
        return self.layout.candidate(mod.read_json(self.layout.work / 'prepared.json')['id'])

    def canonical(self):
        self.host.old = self.layout.canonical_binary
        self.host.launch_entry = self.layout.canonical_launcher
        self.host.working = self.layout.canonical_launcher.parent
        put(self.host.old, 'original production engine')
        # The exact reviewed bytes are intentionally pinned, including LF endings.
        body = ('#!/bin/sh\nset -eu\n'
                '# Account-adapted beta entrypoint. Preserve current Lua module search behavior.\n'
                'cd /home/pi/openFrameworks/apps/myApps/eyesy\n'
                'exec /usr/bin/stdbuf -oL -eL bin/eyesy\n')
        self.host.launch_entry.parent.mkdir(parents=True, exist_ok=True)
        self.host.launch_entry.write_bytes(body.encode('utf-8'))
        self.assertEqual(mod.digest(self.host.launch_entry), mod.CANONICAL_LAUNCHER_SHA256)
        self.host.selected_mode = self.layout.modes / 'oFLua/Existing'

    def unchanged(self):
        self.assertEqual(mod.digest(self.host.old), self.original_binary)
        self.assertEqual(mod.digest(self.host.unit), self.original_unit)
        self.assertEqual((self.layout.root / 'sdcard/Scenes/scene.json').read_text(), 'keep scenes')
        self.assertEqual((self.layout.root / 'sdcard/settings.json').read_text(), 'keep settings')
        self.assertEqual((self.layout.modes / 'oFLua/Existing/main.lua').read_text(), 'keep this mode')
        self.assertEqual(self.host.enabled, 'enabled')

    def test_prepare_dry_run_performs_no_writes_or_service_control(self):
        before = mod.inventory(self.root)
        self.installer.prepare(dry=True)
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, [])

    def test_unsupported_platform_fails_before_writes(self):
        self.host.bad_platform = True
        with self.assertRaises(mod.InstallError):
            self.installer.prepare()
        self.assertFalse(self.layout.work.exists())
        self.assertEqual(self.host.calls, [])

    def test_missing_dependency_fails_before_writes(self):
        self.host.missing_dependency = True
        with self.assertRaises(mod.InstallError):
            self.installer.prepare()
        self.assertFalse(self.layout.work.exists())

    def test_changed_source_refused_before_build(self):
        put(self.package / 'source/app/unit1.cpp', 'modified')
        with self.assertRaises(mod.InstallError):
            self.installer.prepare()
        self.assertFalse(self.layout.work.exists())

    def test_failed_build_does_not_touch_service_and_can_retry_new_directory(self):
        self.host.fail_build = True
        with self.assertRaises(mod.InstallError):
            self.installer.prepare()
        failed = self.candidate()
        self.assertEqual(self.host.calls, [])
        self.host.fail_build = False
        self.installer.prepare()
        self.assertNotEqual(self.candidate(), failed)
        self.assertTrue(failed.exists())
        self.unchanged()

    def test_incomplete_build_never_staged(self):
        self.host.incomplete = True
        with self.assertRaises(mod.InstallError):
            self.installer.prepare()
        self.assertFalse((self.candidate() / 'release').exists())
        self.assertEqual(self.host.calls, [])

    def test_prepare_keeps_modes_and_service_unchanged(self):
        self.installer.prepare()
        self.assertEqual(mod.inventory(self.layout.modes), self.original_mode)
        self.assertEqual(self.host.calls, [])
        self.assertFalse(self.layout.dropin.exists())
        self.unchanged()

    def test_reduced_build_missing_or_duplicate_compile_record_is_rejected(self):
        self.installer.prepare()
        candidate = self.candidate()
        path = candidate / 'build/build-result.json'
        original = mod.read_json(path)
        self.assertEqual(len(original['compiled']), 97)
        for records in (original['compiled'][:-1],
                        original['compiled'][:-1] + [original['compiled'][0]]):
            changed = dict(original, compiled=records)
            json_file(path, changed)
            with self.assertRaisesRegex(mod.InstallError, '97 fresh engine compile records'):
                self.installer.verify_build(candidate)
        json_file(path, original)
        self.installer.verify_build(candidate)
        self.assertEqual(self.host.calls, [])
        self.unchanged()

    def test_activation_dry_run_has_no_writes(self):
        self.installer.prepare()
        before = mod.inventory(self.root)
        self.installer.activate(dry=True)
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, [])

    def test_successful_activation_and_rollback_preserve_originals(self):
        self.installer.prepare()
        self.installer.activate()
        self.assertTrue(self.host.mode_owner_set)
        self.assertEqual(mod.read_json(self.layout.journal)['phase'], 'active')
        self.assertTrue(self.layout.dropin.is_file())
        backup = Path(mod.read_json(self.layout.journal)['backup'])
        self.assertTrue((backup / 'Modes.tar').is_file())
        self.unchanged()
        self.installer.rollback()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.assertFalse(self.layout.starter.exists())
        self.assertEqual(mod.inventory(self.layout.modes), self.original_mode)
        self.unchanged()
        calls = list(self.host.calls)
        self.installer.rollback()
        self.assertEqual(self.host.calls, calls)

    def test_rollback_restores_previously_stopped_state(self):
        self.host.active = False
        self.installer.prepare(); self.installer.activate(); self.installer.rollback()
        self.assertFalse(self.host.active)
        self.unchanged()

    def test_start_failure_automatically_recovers_old_engine(self):
        self.installer.prepare()
        self.host.fail_new_start = True
        with self.assertRaisesRegex(mod.InstallError, 'restored'):
            self.installer.activate()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.assertFalse(self.layout.starter.exists())
        self.assertEqual(mod.read_json(self.layout.journal)['phase'], 'rolled_back')
        self.unchanged()

    def test_dropin_precedence_failure_recovers_without_starting_candidate(self):
        self.installer.prepare(); self.host.ignored_dropin = True
        with self.assertRaisesRegex(mod.InstallError, 'restored'):
            self.installer.activate()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.unchanged()

    def test_existing_starter_is_never_overwritten(self):
        self.installer.prepare()
        put(self.layout.starter / 'main.lua', 'user mode')
        with self.assertRaises(mod.InstallError):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])
        self.assertEqual((self.layout.starter / 'main.lua').read_text(), 'user mode')

    def test_existing_dropin_is_never_overwritten(self):
        self.installer.prepare(); put(self.layout.dropin, 'existing user configuration')
        with self.assertRaises(mod.InstallError):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])
        self.assertEqual(self.layout.dropin.read_text(), 'existing user configuration')

    def test_tampered_staged_binary_refused_before_stop(self):
        self.installer.prepare()
        put(self.candidate() / 'release/bin/EYESY_SOURCE_BUILD', 'tampered')
        with self.assertRaises(mod.InstallError):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])

    def test_missing_data_refused_before_stop(self):
        self.installer.prepare()
        (self.candidate() / 'build/runtime/bin/data/lua/helper.lua').unlink()
        with self.assertRaises(mod.InstallError):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])

    def test_changed_service_configuration_blocks_rollback_without_control_calls(self):
        self.installer.prepare(); self.installer.activate()
        put(self.host.unit, 'new administrator configuration')
        calls = list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'configuration changed'):
            self.installer.rollback()
        self.assertEqual(self.host.calls, calls)
        self.assertEqual(self.host.unit.read_text(), 'new administrator configuration')

    def test_changed_installer_dropin_blocks_rollback(self):
        self.installer.prepare(); self.installer.activate()
        with self.layout.dropin.open('a') as handle:
            handle.write('# administrator edit\n')
        calls = list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'edited'):
            self.installer.rollback()
        self.assertEqual(self.host.calls, calls)

    def test_edited_starter_is_preserved_on_rollback(self):
        self.installer.prepare(); self.installer.activate()
        put(self.layout.starter / 'main.lua', 'my edited mode')
        self.installer.rollback()
        self.assertEqual((self.layout.starter / 'main.lua').read_text(), 'my edited mode')
        self.assertIn('edited', self.output.getvalue())

    def test_interrupted_activation_can_be_recovered_from_journal(self):
        self.installer.prepare(); self.installer.activate()
        journal = mod.read_json(self.layout.journal)
        journal['phase'] = 'activating'
        mod.save_json(self.layout.journal, journal)
        self.installer.rollback()
        self.assertEqual(mod.read_json(self.layout.journal)['phase'], 'rolled_back')
        self.unchanged()

    def test_fresh_prepare_allowed_after_rollback(self):
        self.installer.prepare(); self.installer.activate(); self.installer.rollback()
        old = self.candidate()
        self.installer.prepare()
        self.assertNotEqual(self.candidate(), old)

    def test_second_prepare_cannot_replace_pending_candidate(self):
        self.installer.prepare()
        before = mod.read_json(self.layout.work / 'prepared.json')
        with self.assertRaises(mod.InstallError):
            self.installer.prepare()
        self.assertEqual(mod.read_json(self.layout.work / 'prepared.json'), before)

    def test_host_platform_guard_rejects_windows_without_commands(self):
        with mock.patch.object(mod.platform, 'system', return_value='Windows'):
            with self.assertRaises(mod.InstallError):
                mod.Host().platform_check(self.layout, build=True)

    def test_invalid_candidate_path_is_rejected(self):
        for value in ('../production', '/etc', '20200101T000000Z-00000000/../evil', None):
            with self.assertRaises(mod.InstallError):
                self.layout.candidate(value)

    def test_missing_old_engine_blocks_activation_before_service_control(self):
        self.installer.prepare(); self.host.old.unlink()
        with self.assertRaisesRegex(mod.InstallError, 'Prior engine is missing'):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])

    def test_running_engine_must_match_prior_service_route(self):
        self.installer.prepare(); self.host.identity_mismatch = True
        with self.assertRaisesRegex(mod.InstallError, 'Current process differs'):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])

    def test_failed_dependency_smoke_cannot_activate(self):
        self.installer.prepare()
        json_file(self.candidate() / 'dependencies/dependency-build-result.json',
                  {'complete': True, 'smoke_passed': False})
        with self.assertRaisesRegex(mod.InstallError, 'dependency smoke'):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])

    def test_full_backup_disk_does_not_stop_old_engine(self):
        self.installer.prepare()
        with mock.patch.object(mod.shutil, 'disk_usage', return_value=mock.Mock(free=0)):
            with self.assertRaisesRegex(mod.InstallError, 'Insufficient space'):
                self.installer.activate()
        self.assertEqual(self.host.calls, [])
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        # Retained failed backup attempts do not prevent a clean retry.
        self.installer.activate()
        self.assertEqual(mod.read_json(self.layout.journal)['phase'], 'active')

    def test_rollback_does_not_require_build_artifacts_or_compiler(self):
        self.installer.prepare(); self.installer.activate()
        candidate = self.candidate()
        os.rename(str(candidate), str(candidate.with_name(candidate.name + '-preserved')))
        self.host.missing_dependency = True
        self.installer.rollback()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.unchanged()

    def test_rollback_dry_run_checks_conflicts_without_writes(self):
        self.installer.prepare(); self.installer.activate()
        before = mod.inventory(self.root)
        calls = list(self.host.calls)
        self.installer.rollback(dry=True)
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, calls)
        put(self.host.unit, 'administrator change')
        with self.assertRaisesRegex(mod.InstallError, 'configuration changed'):
            self.installer.rollback(dry=True)
        self.assertEqual(self.host.calls, calls)

    def test_atomic_dropin_creation_refuses_existing_target(self):
        path = self.root / 'owned.conf'
        mod.exclusive_text(path, 'one complete file\n')
        with self.assertRaises(OSError):
            mod.exclusive_text(path, 'replacement\n')
        self.assertEqual(path.read_text(), 'one complete file\n')
        self.assertEqual(list(self.root.glob('owned.conf.*.tmp')), [])

    def test_health_check_rejects_restart_counter_change(self):
        host = mod.Host()
        with mock.patch.object(host, 'properties', return_value={'NRestarts': '1'}):
            with self.assertRaisesRegex(mod.InstallError, 'restarted'):
                host.wait_healthy(Path('/candidate'), Path('/candidate-cwd'), '0')

    def test_canonical_activation_and_rollback_track_real_engine_and_launcher(self):
        self.canonical()
        route = mod.launch_route(self.host.properties(), self.layout)
        self.assertEqual(Path(route['entry']), self.layout.canonical_launcher)
        self.assertEqual(Path(route['binary']), self.layout.canonical_binary)
        self.assertEqual(Path(route['runtime_cwd']), self.layout.canonical_cwd)
        self.assertEqual(Path(route['service_cwd']), self.host.working)
        self.installer.prepare(); self.installer.activate()
        journal = mod.read_json(self.layout.journal)
        self.assertEqual(journal['schema'], 3)
        self.assertEqual(journal['profile'], mod.CUSTOM)
        self.assertEqual(journal['old_route'], route)
        self.assertEqual(mod.digest(Path(journal['backup']) / 'launcher.sh'), route['entry_sha256'])
        self.installer.rollback()
        self.assertTrue(self.host.active)
        self.assertEqual(mod.launch_route(self.host.properties(), self.layout), route)
        self.assertFalse(self.layout.starter.exists())
        self.assertFalse(self.layout.dropin.exists())
        self.unchanged()

    def test_direct_engine_activation_allows_real_mode_cwd(self):
        self.host.selected_mode = self.layout.modes / 'oFLua/Existing'
        self.installer.prepare(); self.installer.activate(); self.installer.rollback()
        self.assertTrue(self.host.active)
        self.unchanged()

    def test_canonical_start_failure_recovers_real_engine_in_mode_directory(self):
        self.canonical(); self.installer.prepare(); self.host.fail_new_start = True
        with self.assertRaisesRegex(mod.InstallError, 'were restored'):
            self.installer.activate()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.assertEqual(mod.read_json(self.layout.journal)['phase'], 'rolled_back')
        self.unchanged()

    def test_canonical_rollback_restores_previously_stopped_state(self):
        self.canonical(); self.host.active = False
        self.installer.prepare(); self.installer.activate(); self.installer.rollback()
        self.assertFalse(self.host.active)
        self.unchanged()

    def test_canonical_dry_run_checks_route_without_writes(self):
        self.canonical(); self.installer.prepare()
        before = mod.inventory(self.root)
        self.installer.activate(dry=True)
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, [])

    def test_modified_canonical_launcher_refused_before_state_writes(self):
        self.canonical(); self.installer.prepare()
        put(self.host.launch_entry, '#!/bin/sh\nexec /wrong/eyesy\n')
        before = mod.inventory(self.root)
        with self.assertRaisesRegex(mod.InstallError, 'Canonical launcher differs'):
            self.installer.activate()
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, [])

    def test_same_launcher_bytes_at_unrecognized_path_are_not_followed(self):
        self.canonical()
        other = self.layout.home / 'other/run.sh'
        other.parent.mkdir(parents=True)
        other.write_bytes(self.host.launch_entry.read_bytes())
        self.host.launch_entry = other
        with self.assertRaisesRegex(mod.InstallError, 'Unrecognized startup script'):
            self.installer.prepare()
        self.assertFalse(self.layout.work.exists())

    def test_canonical_wrong_executable_is_rejected_before_stop(self):
        self.canonical(); self.installer.prepare(); self.host.identity_mismatch = True
        with self.assertRaisesRegex(mod.InstallError, 'Current process differs'):
            self.installer.activate()
        self.assertEqual(self.host.calls, [])

    def test_launcher_changed_during_backup_prevents_service_stop(self):
        self.canonical(); self.installer.prepare()
        copy = mod.shutil.copy2
        def changing_copy(source, target, **kwargs):
            result = copy(source, target, **kwargs)
            if Path(source) == self.layout.canonical_launcher:
                self.layout.canonical_launcher.write_bytes(b'#!/bin/sh\nexit 1\n')
            return result
        with mock.patch.object(mod.shutil, 'copy2', side_effect=changing_copy):
            with self.assertRaisesRegex(mod.InstallError, 'Canonical launcher differs'):
                self.installer.activate()
        self.assertEqual(self.host.calls, [])
        self.assertFalse(self.layout.journal.exists())

    def test_changed_launcher_blocks_rollback_without_overwrite(self):
        self.canonical(); self.installer.prepare(); self.installer.activate()
        put(self.host.launch_entry, '#!/bin/sh\n# administrator edit\n')
        before, calls = mod.inventory(self.root), list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'Canonical launcher differs'):
            self.installer.rollback()
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, calls)

    def test_changed_real_engine_blocks_canonical_rollback(self):
        self.canonical(); self.installer.prepare(); self.installer.activate()
        put(self.host.old, 'replacement at the same engine path')
        calls = list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'Launch route changed'):
            self.installer.rollback()
        self.assertEqual(self.host.calls, calls)

    def test_changed_launcher_backup_blocks_rollback(self):
        self.canonical(); self.installer.prepare(); self.installer.activate()
        put(Path(mod.read_json(self.layout.journal)['backup']) / 'launcher.sh', 'changed backup')
        calls = list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'Launcher backup changed'):
            self.installer.rollback()
        self.assertEqual(self.host.calls, calls)

    def test_legacy_direct_journal_still_recovers(self):
        self.installer.prepare(); self.installer.activate()
        journal = mod.read_json(self.layout.journal)
        route = journal.pop('old_route')
        journal.update(schema=1, old_binary=route['binary'], old_binary_sha256=route['binary_sha256'],
                       old_cwd=route['service_cwd'])
        mod.save_json(self.layout.journal, journal)
        self.installer.rollback()
        self.assertTrue(self.host.active)
        self.unchanged()

    def test_schema_two_canonical_journal_still_recovers(self):
        self.canonical(); self.installer.prepare(); self.installer.activate()
        journal = mod.read_json(self.layout.journal)
        journal['schema'] = 2
        journal.pop('profile'); journal.pop('peer')
        mod.save_json(self.layout.journal, journal)
        self.installer.rollback()
        self.assertTrue(self.host.active)
        self.assertFalse(self.layout.dropin.exists())
        self.unchanged()

    def test_legacy_launcher_journal_cannot_invent_missing_runtime_identity(self):
        self.canonical(); self.installer.prepare(); self.installer.activate()
        journal = mod.read_json(self.layout.journal)
        route = journal.pop('old_route')
        journal.update(schema=1, old_binary=route['entry'], old_binary_sha256=route['entry_sha256'],
                       old_cwd=route['service_cwd'])
        mod.save_json(self.layout.journal, journal)
        calls = list(self.host.calls)
        with self.assertRaisesRegex(mod.InstallError, 'lacks canonical engine identity'):
            self.installer.rollback()
        self.assertEqual(self.host.calls, calls)

    def test_reported_low_space_rejected_before_prepare_writes(self):
        self.canonical(); before = mod.inventory(self.root)
        with mock.patch.object(mod.shutil, 'disk_usage', return_value=mock.Mock(free=115425280)):
            with self.assertRaisesRegex(mod.InstallError, 'At least 1 GiB'):
                self.installer.prepare()
        self.assertEqual(mod.inventory(self.root), before)
        self.assertFalse(self.layout.work.exists())
        self.assertEqual(self.host.calls, [])

    def test_stale_service_metadata_rejected_before_prepare_and_activation_writes(self):
        self.host.needs_reload = 'yes'
        with self.assertRaisesRegex(mod.InstallError, 'need daemon-reload'):
            self.installer.prepare()
        self.assertFalse(self.layout.work.exists())
        self.host.needs_reload = 'no'; self.installer.prepare()
        self.host.needs_reload = 'yes'; before = mod.inventory(self.root)
        with self.assertRaisesRegex(mod.InstallError, 'need daemon-reload'):
            self.installer.activate()
        self.assertEqual(mod.inventory(self.root), before)
        self.assertEqual(self.host.calls, [])


class ProcessIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('HAUTLLE_TEST_TMP'))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.binary = self.root / 'app/bin/eyesy'
        self.cwd = self.root / 'app'
        self.modes = self.root / 'sdcard/Modes/oFLua'
        self.mode = self.modes / 'Existing'
        put(self.binary, 'engine bytes')
        put(self.mode / 'main.lua', 'function draw() end')
        self.host = mod.Host()

    def matches(self, binary, directory):
        with mock.patch.object(self.host, 'identity', return_value=(str(binary), str(directory))):
            return self.host.process_matches('123', self.binary, self.cwd, self.modes)

    def test_initial_and_real_single_level_mode_directories_are_allowed(self):
        self.assertTrue(self.matches(self.binary, self.cwd))
        self.assertTrue(self.matches(self.binary, self.mode))

    def test_same_named_same_byte_executable_at_other_path_is_rejected(self):
        other = self.root / 'other/bin/eyesy'
        put(other, 'engine bytes')
        self.assertFalse(self.matches(other, self.mode))

    def test_executable_inode_must_also_match(self):
        with mock.patch.object(Path, 'samefile', return_value=False):
            self.assertFalse(self.matches(self.binary, self.mode))

    def test_outside_nested_empty_and_mode_root_directories_are_rejected(self):
        for directory in (self.root / 'outside', self.mode / 'nested', self.modes / 'Empty'):
            directory.mkdir(parents=True, exist_ok=True)
        put(self.root / 'outside/main.lua', 'not a mode')
        put(self.mode / 'nested/main.lua', 'nested mode')
        for directory in (self.root / 'outside', self.mode / 'nested', self.modes / 'Empty', self.modes):
            with self.subTest(directory=directory):
                self.assertFalse(self.matches(self.binary, directory))

    def test_symlinked_main_is_rejected(self):
        original = Path.is_symlink
        with mock.patch.object(Path, 'is_symlink', autospec=True,
                               side_effect=lambda p: p == self.mode / 'main.lua' or original(p)):
            self.assertFalse(self.matches(self.binary, self.mode))

    def test_missing_executable_and_exited_process_are_rejected(self):
        self.assertFalse(self.matches(self.root / 'missing', self.mode))
        with mock.patch.object(self.host, 'identity', side_effect=ProcessLookupError):
            self.assertFalse(self.host.process_matches('123', self.binary, self.cwd, self.modes))

    def test_health_window_allows_transition_from_initial_cwd_to_mode(self):
        props = {'ActiveState': 'active', 'MainPID': '123', 'NRestarts': '0'}
        with mock.patch.object(self.host, 'properties', return_value=props), \
             mock.patch.object(self.host, 'identity', side_effect=[(self.binary, self.cwd), (self.binary, self.mode)]), \
             mock.patch.object(mod.time, 'monotonic', side_effect=itertools.count()), \
             mock.patch.object(mod.time, 'sleep'):
            self.host.wait_healthy(self.binary, self.cwd, '0', self.modes)

    def test_health_window_rejects_outside_mode_directory(self):
        props = {'ActiveState': 'active', 'MainPID': '123', 'NRestarts': '0'}
        with mock.patch.object(self.host, 'properties', return_value=props), \
             mock.patch.object(self.host, 'identity', return_value=(self.binary, self.root)):
            with self.assertRaisesRegex(mod.InstallError, 'identity differs'):
                self.host.wait_healthy(self.binary, self.cwd, '0', self.modes)


if __name__ == '__main__':
    unittest.main()
