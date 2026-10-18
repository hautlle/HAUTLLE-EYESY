"""Local source/provenance and failure-guard checks; no compiler or device use."""
from pathlib import Path
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('deps',ROOT/'build-support/rebuild_dependencies.py')
deps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deps)

class PublicDependencyTests(unittest.TestCase):
    def test_all_public_source_and_patch_hashes_match(self):
        self.assertEqual(len(deps.check_sources()['sources']),22)

    def test_patched_public_headers_match_engine_abi(self):
        for src, dst in [('kissfft/kiss_fft.h','kiss/include/kiss_fft.h'),
                         ('kissfft/tools/kiss_fftr.h','kiss/include/kiss_fftr.h'),
                         ('libtess2/Include/tesselator.h','tess2/include/tesselator.h')]:
            self.assertEqual((deps.SOURCE/src).read_bytes(),(ROOT/'source/vendor'/dst).read_bytes())
        header=(deps.SOURCE/'libtess2/Include/tesselator.h').read_text()
        self.assertIn('TARGET_OS_IPHONE || ANDROID || __ARMEL__ || EMSCRIPTEN',header)
        self.assertIn('typedef unsigned short TESSindex;',header)
        self.assertIn('const TESSindex* tessGetElements',(deps.SOURCE/'libtess2/Source/tess.c').read_text())

    def test_dry_run_has_no_process_or_output_side_effect(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest=Path(tmp)/'new output'
            with patch.object(deps.subprocess,'check_output',side_effect=AssertionError('process')):
                with patch.object(deps.subprocess,'run',side_effect=AssertionError('process')):
                    with contextlib.redirect_stdout(io.StringIO()):
                        deps.main(['--output',str(dest),'--dry-run'])
            self.assertFalse(dest.exists())

    def test_output_cannot_reuse_or_overlap_source(self):
        for dest in [ROOT,ROOT/'new-deps',ROOT.parent]:
            with self.assertRaises(ValueError):deps.main(['--output',str(dest),'--dry-run'])

    def test_plan_has_nine_fresh_units_and_deterministic_archives(self):
        plan=deps.make_plan(Path('/temporary/new output'),'gcc','ar')
        self.assertEqual(len(plan['units']),9)
        self.assertEqual(len({r['object'] for r in plan['units']}),9)
        self.assertTrue(all(r['args'][1:5]==['-std=gnu11','-march=armv6','-mfpu=vfp','-mfloat-abi=hard'] for r in plan['units']))
        self.assertTrue(all(r[1]=='rcsD' for r in plan['archives']))
        self.assertEqual(len(plan['archives'][0][3:]),2)
        self.assertEqual(len(plan['archives'][1][3:]),7)

    def result_fixture(self, root):
        plan=deps.make_plan(root,'gcc','ar')
        records=[]
        for unit in plan['units']:
            obj=Path(unit['object']);obj.parent.mkdir(parents=True,exist_ok=True);obj.write_bytes(b'fixture object')
            records.append({'source':unit['source'],'source_sha256':deps.sha(Path(unit['source'])),
                            'object':str(obj),'object_sha256':deps.sha(obj)})
        archives=[]
        for name in deps.ARCHIVES:
            p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'fixture archive')
            archives.append({'path':name,'sha256':deps.sha(p)})
        result={'schema':1,'complete':True,'smoke_passed':True,
                'recipe_sha256':deps.sha(deps.HERE/'rebuild_dependencies.py'),
                'source_manifest_sha256':deps.sha(deps.HERE/'dependency-sources.json'),
                'smoke_source_sha256':deps.sha(deps.HERE/'dependency_smoke.c'),
                'toolchain':{'compiler_version':'8.3.0','target':'arm-linux-gnueabihf','ar_version':'GNU ar 2.31.1'},
                'compiled':records,'archives':archives}
        path=root/'dependency-build-result.json';path.write_text(json.dumps(result))
        return path,result

    def test_result_detects_failed_smoke_and_recipe_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path,result=self.result_fixture(root)
            deps.validate_result(root,path)
            for field,value in [('smoke_passed',False),('recipe_sha256','0'*64),('complete',False)]:
                changed=dict(result);changed[field]=value;path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):deps.validate_result(root,path)

    def test_result_detects_object_and_archive_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path,result=self.result_fixture(root)
            obj=Path(result['compiled'][0]['object']);obj.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'object'):deps.validate_result(root,path)
            obj.write_bytes(b'fixture object')
            (root/deps.ARCHIVES[0]).write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'archive'):deps.validate_result(root,path)

    def test_result_rejects_extra_archives_and_path_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path,result=self.result_fixture(root)
            result['archives'].append(dict(result['archives'][0]));path.write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError,'archive list'):deps.validate_result(root,path)
            result['archives'].pop();result['compiled'][0]['object']=str(root.parent/'outside.o')
            path.write_text(json.dumps(result))
            with self.assertRaisesRegex(ValueError,'object'):deps.validate_result(root,path)

    def test_candidate_result_is_explicit_in_engine_preflight(self):
        engine_spec=importlib.util.spec_from_file_location('engine',ROOT/'build-support/build.py')
        engine=importlib.util.module_from_spec(engine_spec);engine_spec.loader.exec_module(engine)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path,result=self.result_fixture(root/'deps')
            with self.assertRaisesRegex(ValueError,'external archive'):
                engine.check_inputs(ROOT/'source',root/'deps')
            self.assertEqual(engine.check_inputs(ROOT/'source',root/'deps',path),650)

    def test_native_guard_rejects_non_arm_without_creating_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest=Path(tmp)/'new output'
            with patch.object(deps.platform,'system',return_value='Windows'):
                with self.assertRaisesRegex(ValueError,'Native 32-bit ARM'):
                    deps.main(['--output',str(dest)])
            self.assertFalse(dest.exists())

if __name__=='__main__':unittest.main()
