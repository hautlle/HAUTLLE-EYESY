"""Disposable transaction fixtures; no device, service, or network calls."""
import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SUPPORT = Path(__file__).resolve().parents[1] / 'build-support'
sys.path.insert(0, str(SUPPORT))
spec = importlib.util.spec_from_file_location('beta_web_tested', SUPPORT / 'beta_web.py')
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)


class Operations:
    def __init__(self, root):
        self.root = root
        self.restarts = 0
        self.fail = False
        self.external = None
        self.peer_change = False

    def restart(self):
        self.restarts += 1

    def healthy(self, files, final):
        if self.restarts == 1 and self.external:
            self.external.write_bytes(b'independent edit')
            raise w.InstallError('external fixture failure')
        if self.restarts == 1 and self.fail:
            raise w.InstallError('fixture startup failure')
        if w.read_files(self.root) != files:
            raise AssertionError('health checked against wrong file state')

    def peers(self):
        return {'engine-pd': 'changed' if self.peer_change and self.restarts == 1 else 'unchanged'}


class WebTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'os'
        self.backup = Path(self.tmp.name) / 'backup'
        self.old, self.new, rows = {}, {}, []
        for name in ('web/cpapp.py', 'web/static/app.js', 'web/static/app.html'):
            old, new = ('stock ' + name + '\n').encode(), ('final ' + name + '\n').encode()
            self.old[name], self.new[name] = old, new
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(old)
            rows.append(dict(path=name, before_sha256=w.digest(old), after_sha256=w.digest(new),
                             hunks=[dict(old_start=1, new_start=1, before=old.decode(), after=new.decode())]))
        profile = mock.patch.object(w, 'PROFILE', dict(files=rows))
        profile.start()
        self.addCleanup(profile.stop)
        self.ops = Operations(self.root)

    def change(self, action='apply'):
        return w.change(action, self.root, self.backup, self.ops)

    def test_apply_rollback_and_backups(self):
        self.change()
        self.assertEqual(w.read_files(self.root), self.new)
        self.assertEqual(w.read_files(self.backup), self.old)
        self.change('rollback')
        self.assertEqual(w.read_files(self.root), self.old)
        self.assertEqual(self.ops.restarts, 2)

    def test_exact_transform_both_directions(self):
        for row in w.PROFILE['files']:
            name = row['path']
            self.assertEqual(w.transform(self.old[name], row), self.new[name])
            self.assertEqual(w.transform(self.new[name], row, reverse=True), self.old[name])

    def test_unknown_and_mixed_refused_without_backup(self):
        target = self.root / 'web/cpapp.py'
        for data in (b'unknown', self.new['web/cpapp.py']):
            target.write_bytes(data)
            with self.assertRaisesRegex(w.InstallError, 'Unknown or mixed'):
                self.change()
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.ops.restarts, 0)

    def test_idempotent_and_existing_final_without_backup(self):
        self.change('rollback')
        for name, data in self.new.items():
            (self.root / name).write_bytes(data)
        self.change()
        self.assertEqual(self.ops.restarts, 0)
        self.assertFalse(self.backup.exists())

    def test_changed_backup_stops_apply(self):
        dest = self.backup / 'web/cpapp.py'
        dest.parent.mkdir(parents=True)
        dest.write_bytes(b'changed')
        with self.assertRaisesRegex(w.InstallError, 'backup changed'):
            self.change()
        self.assertEqual(w.read_files(self.root), self.old)

    def test_changed_backup_stops_rollback(self):
        self.change()
        (self.backup / 'web/static/app.js').write_bytes(b'changed')
        with self.assertRaises(w.InstallError):
            self.change('rollback')
        self.assertEqual(w.read_files(self.root), self.new)
        self.assertEqual(self.ops.restarts, 1)

    def test_staging_failure_leaves_live_files_unchanged(self):
        original = w.stage
        def failing(path, data, info):
            if path == self.root / 'web/static/app.js':
                raise OSError('stage failure')
            return original(path, data, info)
        with mock.patch.object(w, 'stage', side_effect=failing):
            with self.assertRaisesRegex(OSError, 'stage failure'):
                self.change()
        self.assertEqual(w.read_files(self.root), self.old)
        self.assertFalse(list(self.root.rglob('*.tmp')))
        self.assertEqual(self.ops.restarts, 0)

    def test_second_replace_failure_recovers_partial_write(self):
        original = w.os.replace
        count = [0]
        def failing(src, dest):
            count[0] += 1
            if count[0] == 2:
                raise OSError('replace failure')
            return original(src, dest)
        with mock.patch.object(w.os, 'replace', side_effect=failing):
            with self.assertRaisesRegex(OSError, 'replace failure'):
                self.change()
        self.assertEqual(w.read_files(self.root), self.old)
        self.assertEqual(self.ops.restarts, 1)
        self.assertFalse(list(self.root.rglob('*.tmp')))

    def test_failed_health_restores_all_bytes(self):
        self.ops.fail = True
        with self.assertRaisesRegex(w.InstallError, 'fixture startup'):
            self.change()
        self.assertEqual(w.read_files(self.root), self.old)
        self.assertEqual(self.ops.restarts, 2)

    def test_external_edit_not_overwritten_on_failure(self):
        self.ops.external = self.root / 'web/static/app.js'
        with self.assertRaisesRegex(w.InstallError, 'changed externally'):
            self.change()
        self.assertEqual(self.ops.external.read_bytes(), b'independent edit')
        self.assertEqual(w.read_files(self.backup), self.old)

    def test_peer_change_triggers_web_recovery(self):
        self.ops.peer_change = True
        with self.assertRaisesRegex(w.InstallError, 'Engine/PD'):
            self.change()
        self.assertEqual(w.read_files(self.root), self.old)

    def test_patch_context_and_output_hash_are_checked(self):
        row = copy.deepcopy(w.PROFILE['files'][0])
        row['hunks'][0]['before'] = 'incorrect context\n'
        with self.assertRaisesRegex(w.InstallError, 'context'):
            w.transform(self.old[row['path']], row)
        row = copy.deepcopy(w.PROFILE['files'][0])
        row['after_sha256'] = '0' * 64
        with self.assertRaisesRegex(w.InstallError, 'output'):
            w.transform(self.old[row['path']], row)

    def test_hardlinked_target_refused(self):
        target = self.root / 'web/cpapp.py'
        w.os.link(str(target), str(self.root / 'alias'))
        with self.assertRaisesRegex(w.InstallError, 'one link'):
            self.change()

    def test_dry_run_only_checks_and_options_are_scoped(self):
        import installer
        module = mock.Mock()
        module.main.return_value = 0
        with mock.patch.object(installer, 'support_module', return_value=module):
            self.assertEqual(installer.main(['web-apply', '--dry-run']), 0)
            module.main.assert_called_once_with('check')
            module.reset_mock()
            self.assertEqual(installer.main(['web-apply', '--workspace', 'sdcard']), 1)
            self.assertEqual(installer.main(['web-apply', '--profile', 'custom-pi']), 1)
            module.main.assert_not_called()


if __name__ == '__main__':
    unittest.main()
