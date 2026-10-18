"""Command/provenance checks only; no compiler, swap or device calls."""
import copy
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('memory_recipe', ROOT/'build-support/build.py')
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)


class MemoryProfileTests(unittest.TestCase):
    def setUp(self):
        self.plan = json.loads((ROOT/'build-support/plan.json').read_text())
        self.index = next(i for i,u in enumerate(self.plan['units']) if u['source'].endswith('/ofBindings.cpp'))

    def test_default_preserves_every_command_without_aliasing(self):
        actual = recipe.memory_profile(self.plan, 'original')
        self.assertEqual(actual, self.plan)
        actual['units'][0]['args'].append('mutation')
        self.assertNotEqual(actual, self.plan)

    def test_only_validated_binding_flags_change(self):
        before = copy.deepcopy(self.plan)
        actual = recipe.memory_profile(self.plan, 'bindings-gc-v1')
        expected = copy.deepcopy(before)
        args = expected['units'][self.index]['args']
        args.remove('-pipe')
        args[1:1] = ['--param','ggc-min-expand=10','--param','ggc-min-heapsize=4096']
        self.assertEqual(actual, expected)
        self.assertEqual(self.plan, before)
        self.assertIn('-O3', args)
        self.assertIn('-ftree-vectorize', args)

    def test_missing_duplicate_or_changed_binding_recipe_rejected(self):
        bad = copy.deepcopy(self.plan);bad['units'].pop(self.index)
        with self.assertRaisesRegex(ValueError,'exactly one'):recipe.memory_profile(bad,'bindings-gc-v1')
        bad = copy.deepcopy(self.plan);bad['units'].append(bad['units'][self.index])
        with self.assertRaisesRegex(ValueError,'exactly one'):recipe.memory_profile(bad,'bindings-gc-v1')
        for change in ('optimization','pipe','collector'):
            bad=copy.deepcopy(self.plan);args=bad['units'][self.index]['args']
            if change=='optimization':args[args.index('-O3')]='-O1'
            elif change=='pipe':args.remove('-pipe')
            else:args.append('--param=ggc-min-expand=20')
            with self.assertRaisesRegex(ValueError,'validated original'):recipe.memory_profile(bad,'bindings-gc-v1')
        with self.assertRaisesRegex(ValueError,'Unknown'):recipe.memory_profile(self.plan,'unknown')

    def test_command_digest_preserves_argument_boundaries_and_order(self):
        command=['g++','-I/path with spaces','-O3']
        expected=hashlib.sha256(json.dumps(command,separators=(',',':'),ensure_ascii=True).encode()).hexdigest()
        self.assertEqual(recipe.command_sha256(command),expected)
        self.assertNotEqual(recipe.command_sha256(command),recipe.command_sha256(list(reversed(command))))
        self.assertNotEqual(recipe.command_sha256(['a b','c']),recipe.command_sha256(['a','b c']))

    def test_recorded_commands_are_the_commands_executed(self):
        # Exercise all 97 runner records, archive/link and data staging with fake
        # compiler output. This is command-provenance validation, not compilation.
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);executed=[]
            def version(command,**kwargs):
                return '8.3.0' if '-dumpfullversion' in command else ('arm-linux-gnueabihf' if '-dumpmachine' in command else 'GNU ar 2.31.1')
            def run(command,**kwargs):
                executed.append(list(command))
                output=Path(command[command.index('-o')+1] if '-o' in command else command[2])
                output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(b'fixture')
            with mock.patch.object(recipe,'check_inputs',return_value=650), \
                 mock.patch.object(recipe.subprocess,'check_output',side_effect=version), \
                 mock.patch.object(recipe.subprocess,'run',side_effect=run),contextlib.redirect_stdout(io.StringIO()):
                recipe.main(['--build-dir',str(base/'build'),'--dependency-root',str(base/'deps'),
                             '--compiler-memory-profile','bindings-gc-v1'])
            result=json.loads((base/'build/build-result.json').read_text())
            effective=json.loads((base/'build/expanded-plan.json').read_text())
            self.assertTrue(result['complete'])
            self.assertFalse(result['runtime_qualified'])
            self.assertEqual(result['compiler_memory_profile'],'bindings-gc-v1')
            self.assertEqual(len(result['compiled']),97)
            self.assertEqual([row['command'] for row in result['compiled']],executed[:97])
            self.assertEqual(executed,[u['args'] for u in effective['units']]+[effective['archive'],effective['link']])
            self.assertEqual(result['effective_plan_sha256'],recipe.sha(base/'build/expanded-plan.json'))
            self.assertEqual(result['plan_sha256'],recipe.sha(ROOT/'build-support/plan.json'))
            for row in result['compiled']:
                self.assertEqual(row['command_sha256'],recipe.command_sha256(row['command']))

    def test_failed_command_is_not_recorded_as_a_successful_compile(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp)
            with mock.patch.object(recipe,'check_inputs',return_value=650), \
                 mock.patch.object(recipe.subprocess,'check_output',side_effect=['8.3.0','arm-linux-gnueabihf','GNU ar 2.31.1']), \
                 mock.patch.object(recipe.subprocess,'run',side_effect=subprocess.CalledProcessError(2,['g++'])), \
                 contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(subprocess.CalledProcessError):
                    recipe.main(['--build-dir',str(base/'build'),'--dependency-root',str(base/'deps')])
            result=json.loads((base/'build/build-result.json').read_text())
            self.assertFalse(result['complete'])
            self.assertEqual(result['compiler_memory_profile'],'original')
            self.assertEqual(result['compiled'],[])
            self.assertEqual(result['effective_plan_sha256'],recipe.sha(base/'build/expanded-plan.json'))


if __name__=='__main__':unittest.main()
