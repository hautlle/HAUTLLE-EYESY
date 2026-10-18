"""No compiler/hardware needed. Run: python3 -m unittest discover -s tests -v"""
from pathlib import Path, PurePosixPath
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('recipe', ROOT/'build-support/build.py')
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)

class RecipeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root/'source with spaces';self.source.mkdir()
        self.deps = self.root/'external inputs';self.deps.mkdir()
        self.support = self.root/'support';self.support.mkdir()
        (self.source/'unit.cpp').write_text('int main(){return 0;}\n')
        (self.deps/'lib.a').write_bytes(b'pinned test archive')
        (self.support/'source-hashes.json').write_text(json.dumps({'unit.cpp':recipe.sha(self.source/'unit.cpp')}))
        (self.support/'dependencies.json').write_text(json.dumps({'external_archives':[{'path':'lib.a','sha256':recipe.sha(self.deps/'lib.a')}]}))
        self.plan = {'units':[{'kind':'app','source':'${SOURCE}/unit.cpp','object':'${BUILD}/objects/unit.o','args':['g++','-c','${SOURCE}/unit.cpp','-o','${BUILD}/objects/unit.o']}],
            'archive':['ar','rcs','${BUILD}/objects/core.a'],
            'link':['g++','${BUILD}/objects/unit.o','${BUILD}/objects/core.a','${DEPENDENCIES}/lib.a','-o','${BUILD}/runtime/bin/engine'],
            'runtime_data':'${SOURCE}/app/bin/data'}
        (self.support/'plan.json').write_text(json.dumps(self.plan))
        self.original = recipe.HERE;recipe.HERE=self.support
        self.addCleanup(setattr,recipe,'HERE',self.original)

    def args(self,build=None):
        return ['--source-root',str(self.source),'--dependency-root',str(self.deps),'--build-dir',str(build or self.root/'new output')]

    def test_matching_inputs(self):
        self.assertEqual(recipe.check_inputs(self.source,self.deps),1)

    def test_source_drift_missing_and_unlisted_files_rejected(self):
        unit=self.source/'unit.cpp';original=unit.read_bytes()
        unit.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'hash mismatch'):recipe.check_inputs(self.source,self.deps)
        unit.unlink()
        with self.assertRaisesRegex(ValueError,'file list differs'):recipe.check_inputs(self.source,self.deps)
        unit.write_bytes(original);(self.source/'extra.cpp').write_text('extra')
        with self.assertRaisesRegex(ValueError,'file list differs'):recipe.check_inputs(self.source,self.deps)

    def test_missing_or_changed_external_archive_rejected(self):
        lib=self.deps/'lib.a';lib.write_bytes(b'different')
        with self.assertRaisesRegex(ValueError,'external archive'):recipe.check_inputs(self.source,self.deps)
        lib.unlink()
        with self.assertRaisesRegex(ValueError,'external archive'):recipe.check_inputs(self.source,self.deps)

    def test_refuses_existing_or_input_nested_output(self):
        for path in [self.source/'output',self.deps/'output',self.root]:
            with self.assertRaises(ValueError):recipe.main(self.args(path)+['--dry-run'])

    def test_dry_run_does_not_execute_or_create_build_output(self):
        with patch.object(recipe.subprocess,'check_output',side_effect=AssertionError('tool invoked')):
            with contextlib.redirect_stdout(io.StringIO()):recipe.main(self.args()+['--dry-run'])
        self.assertFalse((self.root/'new output').exists())

    def test_paths_with_spaces_remain_single_arguments(self):
        result=recipe.expand_plan(self.plan,self.source,self.root/'new output',self.deps,PurePosixPath('/'),PurePosixPath('/opt/vc'),'arm-g++','arm-ar')
        self.assertEqual(result['units'][0]['args'][2],(self.source/'unit.cpp').as_posix())
        self.assertEqual(result['units'][0]['args'][0],'arm-g++')
        self.assertEqual(result['archive'][0],'arm-ar')
        self.assertEqual(len(result['units'][0]['args']),5)

    def test_cross_sysroot_is_explicit_in_compile_and_link(self):
        plan=dict(self.plan);plan['units']=[dict(self.plan['units'][0],args=self.plan['units'][0]['args']+['-I${SYSTEM}/usr/include','-I${BROADCOM}/include'])]
        result=recipe.expand_plan(plan,self.source,self.root/'output',self.deps,PurePosixPath('/target sysroot'),PurePosixPath('/vc root'),'g++','ar')
        self.assertIn('--sysroot=/target sysroot',result['units'][0]['args'])
        self.assertIn('--sysroot=/target sysroot',result['link'])
        self.assertIn('-I/target sysroot/usr/include',result['units'][0]['args'])
        self.assertIn('-I/vc root/include',result['units'][0]['args'])

    def test_plan_output_cannot_overwrite_source_or_existing_file(self):
        with self.assertRaises(ValueError):recipe.main(self.args()+['--dry-run','--plan-output',str(self.source/'plan.json')])
        out=self.root/'plan.json';out.write_text('preserve')
        with self.assertRaises(FileExistsError):recipe.main(self.args()+['--dry-run','--plan-output',str(out)])
        self.assertEqual(out.read_text(),'preserve')

    def test_shipped_plan_is_fresh_and_complete(self):
        plan=json.loads((ROOT/'build-support/plan.json').read_text())
        self.assertEqual(len(plan['units']),97)
        self.assertEqual(len({x['object'] for x in plan['units']}),97)
        self.assertEqual({x for x in plan['archive'] if x.endswith('.o')},{x['object'] for x in plan['units'] if x['kind']=='core'})
        self.assertEqual({x for x in plan['link'] if x.endswith('.o')},{x['object'] for x in plan['units'] if x['kind']!='core'})
        self.assertIn(plan['archive'][2],plan['link'])
        text=json.dumps(plan)
        self.assertNotIn('/home/pi/',text)
        self.assertNotIn('accepted_archive',text)
        for row in plan['units']:
            self.assertTrue((ROOT/'source'/row['source'][len('${SOURCE}/'):]).is_file())

    def test_quoted_compile_time_resources_are_included_and_hashed(self):
        # Shader text can be included by C++; source packaging must not drop it
        # merely because its extension is not .cpp/.h. No compiler is required.
        source = ROOT/'source'
        hashes = json.loads((ROOT/'build-support/source-hashes.json').read_text())
        plan = json.loads((ROOT/'build-support/plan.json').read_text())
        resources = set()
        for unit in plan['units']:
            path = source/unit['source'][len('${SOURCE}/'):]
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(), re.MULTILINE):
                if Path(name).suffix.lower() in {'.h', '.hpp', '.hxx'}:
                    continue
                resource = path.parent/name
                relative = resource.relative_to(source).as_posix()
                with self.subTest(unit=unit['source'], resource=name):
                    self.assertTrue(resource.is_file(), 'Missing compile-time resource: '+relative)
                    self.assertIn(relative, hashes)
                    self.assertEqual(recipe.sha(resource), hashes[relative])
                resources.add(relative)
        self.assertTrue({'core/gl/shaders/phong.vert', 'core/gl/shaders/phong.frag'} <= resources)

if __name__=='__main__':unittest.main()
