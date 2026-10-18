"""Installer consent, workspace, recovery and provenance; fake service/build only."""
import copy
import unittest
from unittest import mock
import test_installer as base

mod=base.mod


class InstallerResourceTests(unittest.TestCase):
    def setUp(self):
        base.InstallerTests.setUp(self)
        self.layout=mod.Layout(self.root,workspace='sdcard')
        self.host=base.FakeHost(self.layout)
        self.installer=mod.Installer(self.package,self.layout,self.host,temporary_swap=True)

    candidate=base.InstallerTests.candidate

    def test_explicit_swap_check_is_read_only(self):
        before=mod.inventory(self.root)
        self.installer.prepare(dry=True)
        self.assertEqual(mod.inventory(self.root),before)
        self.assertEqual(self.host.calls,[])
        self.assertEqual(self.layout.work,self.root/'sdcard/hautlle-eyesy')

    def test_swap_requires_data_workspace(self):
        layout=mod.Layout(self.root)
        host=base.FakeHost(layout)
        installer=mod.Installer(self.package,layout,host,temporary_swap=True)
        with self.assertRaisesRegex(mod.InstallError,'requires --workspace sdcard'): installer.prepare()
        self.assertFalse(layout.work.exists())
        self.assertEqual(host.calls,[])

    def test_prepared_profile_and_workspace_follow_consent(self):
        self.installer.prepare()
        candidate,state=self.installer.prepared()
        self.assertEqual(state['workspace'],'sdcard')
        self.assertTrue(state['temporary_swap'])
        self.assertEqual(mod.read_json(candidate/'build/build-result.json')['compiler_memory_profile'],'bindings-gc-v1')
        self.assertEqual(self.host.calls,[])

    def test_incomplete_swap_cleanup_prevents_staging_or_activation(self):
        self.installer.prepare();candidate=self.candidate()
        status=mod.read_json(candidate/'build-resources.json');status['swap_cleanup_verified']=False
        base.json_file(candidate/'build-resources.json',status)
        with self.assertRaisesRegex(mod.InstallError,'cleanup is incomplete'): self.installer.prepared()
        self.assertEqual(self.host.calls,[])

    def test_changed_or_missing_compile_provenance_is_refused(self):
        self.installer.prepare();candidate=self.candidate();path=candidate/'build/build-result.json'
        original=mod.read_json(path)
        for change in ('profile','effective','command','digest'):
            bad=copy.deepcopy(original)
            if change=='profile': bad.pop('compiler_memory_profile')
            elif change=='effective': bad['effective_plan_sha256']='0'*64
            elif change=='command': bad['compiled'][0]['command'].append('-O1')
            else: bad['compiled'][0]['command_sha256']='0'*64
            base.json_file(path,bad)
            with self.assertRaises(mod.InstallError): self.installer.prepared()
        self.assertEqual(self.host.calls,[])

    def test_cleanup_dry_run_creates_no_lock_or_other_file(self):
        identifier='20261018T120000Z-12345678';candidate=self.layout.candidate(identifier)
        base.json_file(self.layout.work/'prepared.json',dict(id=identifier,workspace='sdcard',temporary_swap=True,state='failed'))
        base.json_file(candidate/'build-resources.json',dict(fixture=True))
        before=mod.inventory(self.root)
        self.installer.cleanup_build(dry=True)
        self.assertEqual(mod.inventory(self.root),before)
        self.assertFalse((self.layout.work/'installer.lock').exists())
        self.assertEqual(self.host.calls,[])

    def test_interrupted_build_requires_cleanup_before_retry(self):
        identifier='20261018T120000Z-12345678';candidate=self.layout.candidate(identifier)
        base.json_file(self.layout.work/'prepared.json',dict(id=identifier,workspace='sdcard',temporary_swap=True,state='failed'))
        base.json_file(candidate/'build-resources.json',dict(swap_cleanup_verified=False))
        with self.assertRaisesRegex(mod.InstallError,'cleanup is pending'): self.installer.prepare()
        self.assertEqual(self.host.calls,[])

    def test_host_routes_opt_in_build_through_supervisor(self):
        host=mod.Host(self.layout);host.temporary_swap=True
        session=mock.Mock()
        resources=mock.MagicMock();resources.BuildResources.return_value.session.return_value.__enter__.return_value=session
        with mock.patch.object(mod,'support_module',return_value=resources),mock.patch.object(mod.subprocess,'run') as direct:
            host.build(self.package,self.root/'candidate')
        self.assertEqual(direct.call_count,0)
        calls=session.run.call_args_list
        self.assertEqual(len(calls),2)
        self.assertEqual(calls[0].args[1:],('dependencies',300))
        self.assertEqual(calls[1].args[1:],('engine',7200))
        self.assertEqual(calls[1].args[0][-2:],['--compiler-memory-profile','bindings-gc-v1'])


if __name__=='__main__':unittest.main()
