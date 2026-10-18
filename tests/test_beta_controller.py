"""Focused disposable PD fixtures; never calls a service or device."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

SUPPORT=Path(__file__).resolve().parents[1]/'build-support'
sys.path.insert(0,str(SUPPORT))
spec=importlib.util.spec_from_file_location('beta_controller_tested',SUPPORT/'beta_controller.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)

class Operations:
    def __init__(self): self.restarts=0;self.fail=False
    def healthy(self):
        if self.fail and self.restarts==1: raise c.InstallError('fixture startup failure')
    def restart(self): self.restarts+=1
    def video_identity(self): return {'eyesy-oflua.service':'123 active','eyesy-python.service':'0 inactive'}

class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.target=self.root/'eyesy.pd';self.backup=self.root/'eyesy-stock.pd'
        self.old=b'outside unchanged\n'+c.START+b'\n'.join(old.encode().rstrip(b'\n') for old,new in c.PROFILE['changes'])+b'\n#X connect 42 0 36 0;\n#X connect 55 0 60 0;\n'+c.END+b'\noutside unchanged\n'
        self.new=self.old
        for old,new in c.PROFILE['changes']: self.new=self.new.replace(old.encode(),new.encode())
        profile=dict(c.PROFILE,before_sha256=c.digest(self.old),after_sha256=c.digest(self.new))
        patch=mock.patch.object(c,'PROFILE',profile);patch.start();self.addCleanup(patch.stop)
        self.target.write_bytes(self.old);self.ops=Operations()
    def test_exact_transform_preserves_other_routing(self):
        self.assertEqual(c.patched(self.old),self.new)
        for keep in (b'#X connect 42 0 36 0;',b'#X connect 55 0 60 0;',b'outside unchanged'):
            self.assertEqual(self.new.count(keep),self.old.count(keep))
    def test_unrecognized_preimage_refused_before_backup_or_restart(self):
        self.target.write_bytes(self.old+b'edit')
        with self.assertRaisesRegex(c.InstallError,'exact supported'): c.change('apply',self.target,self.backup,self.ops)
        self.assertFalse(self.backup.exists());self.assertEqual(self.ops.restarts,0)
    def test_apply_and_separate_rollback(self):
        c.change('apply',self.target,self.backup,self.ops)
        self.assertEqual(self.target.read_bytes(),self.new);self.assertEqual(self.backup.read_bytes(),self.old)
        c.change('rollback',self.target,self.backup,self.ops)
        self.assertEqual(self.target.read_bytes(),self.old);self.assertEqual(self.ops.restarts,2)
    def test_already_selected_profile_does_not_restart(self):
        self.target.write_bytes(self.new)
        c.change('apply',self.target,self.backup,self.ops)
        self.assertFalse(self.backup.exists());self.assertEqual(self.ops.restarts,0)
    def test_changed_backup_or_installed_file_refuses_rollback(self):
        c.change('apply',self.target,self.backup,self.ops);self.backup.write_bytes(b'changed')
        with self.assertRaisesRegex(c.InstallError,'backup changed'): c.change('rollback',self.target,self.backup,self.ops)
        self.assertEqual(self.target.read_bytes(),self.new);self.assertEqual(self.ops.restarts,1)
        self.target.write_bytes(self.new+b'edit')
        with self.assertRaisesRegex(c.InstallError,'refuses changed'): c.change('rollback',self.target,self.backup,self.ops)
    def test_restart_failure_restores_old_bytes(self):
        self.ops.fail=True
        with self.assertRaisesRegex(c.InstallError,'fixture startup'): c.change('apply',self.target,self.backup,self.ops)
        self.assertEqual(self.target.read_bytes(),self.old);self.assertEqual(self.ops.restarts,2)
    def test_failed_atomic_replace_leaves_original_untouched(self):
        with mock.patch.object(c,'replace',side_effect=OSError('fixture write failure')):
            with self.assertRaisesRegex(OSError,'write failure'): c.change('apply',self.target,self.backup,self.ops)
        self.assertEqual(self.target.read_bytes(),self.old);self.assertEqual(self.ops.restarts,0)
    def test_dry_run_controller_action_routes_only_to_check(self):
        import installer
        module=mock.Mock();module.main.return_value=0
        with mock.patch.object(installer,'support_module',return_value=module):
            self.assertEqual(installer.main(['controller-apply','--dry-run']),0)
        module.main.assert_called_once_with('check')

if __name__=='__main__': unittest.main()
