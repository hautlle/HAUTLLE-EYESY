"""Cross-file integration checks for the captured live-mode dependency closure.

Static checks only: these do not compile the engine or certify rendering.
"""
# Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see LICENSE.txt.
from pathlib import Path
from collections import Counter
import hashlib
import importlib.util
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'source'

def read(relative):
    return json.loads((ROOT / relative).read_text(encoding='utf-8'))

class LiveRosterTests(unittest.TestCase):
    def setUp(self):
        self.roster = read('provenance/live-native-inventory.json')
        self.plan = read('build-support/plan.json')
        self.keep = set(self.roster['keep'])
        self.header = (SOURCE/'app/src/ofApp.h').read_text()
        self.app = (SOURCE/'app/src/ofApp.cpp').read_text()

    def test_live_api_consumers_have_compiled_registered_implementations(self):
        members = dict(re.findall(r'^\s*(\w+Generator)\s+(\w+);', self.header, re.M))
        registrations = re.findall(r'nativeGenerators\.push_back\(&([A-Za-z0-9_]+)\);', self.app)
        units = [Path(u['source']).stem for u in self.plan['units']
                 if '/app/src/generators/' in u['source']]
        self.assertEqual(set(members), self.keep)
        self.assertEqual(Counter(registrations), Counter(members.values()))
        self.assertEqual(Counter(units), Counter(self.keep))
        self.assertEqual(len(self.roster['mode_map']), 16)
        for mode in self.roster['mode_map']:
            for cls, apis in mode['generators'].items():
                with self.subTest(mode=mode['name'], generator=cls):
                    self.assertIn(cls, members)
                    implementation = (SOURCE/'app/src/generators'/(cls+'.cpp')).read_text()
                    for api in apis:
                        self.assertRegex(implementation, '"'+re.escape(api)+'"')

    def test_removed_classes_have_no_members_bindings_or_build_objects(self):
        headers = re.findall(r'#include "generators/([^\"]+)\.h"', self.header)
        self.assertEqual(set(headers), self.keep)
        for cls in self.roster['remove_candidates']:
            self.assertNotIn(cls, self.header)
            self.assertNotIn(cls, json.dumps(self.plan))
            for suffix in ('.cpp','.h'):
                self.assertFalse((SOURCE/'app/src/generators'/(cls+suffix)).exists())
        self.assertFalse((SOURCE/'app/src/generators/AsciiQuestTuning.h').exists())
        self.assertNotIn('u_corridorTex', self.app)
        self.assertNotIn('u_morphCloudTex', self.app)

    def test_provider_edges_and_feedback_remain(self):
        for edge in self.roster['texture_bindings']:
            if edge['decision'] == 'KEEP':
                self.assertIn('"'+edge['uniform']+'"', self.app)
                self.assertIn(edge['provider'], self.keep)
        for member, uniform in [('bauhausFormGenerator','u_bauhausTex'),('meltLockGenerator','u_meltlockTex')]:
            self.assertRegex(self.app, r'bindExternalTextureProvider\(\s*"'+uniform+r'",\s*\[this\]\(\)\s*\{\s*'+member+r'\.prepareTexture\(\);')
            self.assertIn('return '+member+'.getTextureIfAllocated();',self.app)
        shader = (SOURCE/'app/src/generators/ShaderHostGenerator.cpp').read_text()
        for api in ('shaderhost_feedback_enable','shaderhost_feedback_clear','shaderhost_feedback_is_active'):
            self.assertIn('"'+api+'"',shader)
        self.assertIn('u_prevFrame',shader)

    def test_retirement_order_and_retained_releases(self):
        reload = self.app[self.app.index('void ofApp::reloadScript()'):]
        release = re.findall(r'(\w+)\.releaseGpuResources\(\);',reload)
        self.assertEqual(release, ['shaderHostGenerator','meltLockGenerator','vectorImpactGenerator',
                                  'markovMoshGenerator','bauhausFormGenerator','architectureGenerator'])
        ordered = ['lua.scriptExit();'] + [n+'.releaseGpuResources();' for n in release] + [
            'lua.clear();','glFinish();','lua.init();','registerAllGenerators(lua, nativeGenerators);','lua.doScript(']
        positions = [reload.index(s) for s in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_source_manifest_and_quoted_local_includes_are_complete(self):
        hashes = read('build-support/source-hashes.json')
        actual = {p.relative_to(SOURCE).as_posix():p for p in SOURCE.rglob('*') if p.is_file()}
        self.assertEqual(set(hashes),set(actual))
        self.assertEqual(len(actual),650)
        for name,path in actual.items():
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),hashes[name],name)
        # All app includes ending in Generator.h must still have a shipped header.
        for path in (SOURCE/'app/src').rglob('*'):
            if path.suffix in ('.cpp','.h'):
                for include in re.findall(r'^\s*#include\s+"([^\"]+Generator\.h)"',path.read_text(),re.M):
                    options = [path.parent/include,SOURCE/'app/src'/include,SOURCE/'app/src/generators'/include,SOURCE/'app/src/bridge'/include]
                    self.assertTrue(any(p.is_file() for p in options),(str(path),include))

    def test_control_fixture_remains_identical_to_prior_cpu_qualification(self):
        spec = importlib.util.spec_from_file_location('roster_control_fixture', ROOT/'tests/run_controls_tests.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        expected = read('provenance/controls-cpu.json')['generated_fixture_sha256']
        self.assertEqual(hashlib.sha256(module.fixture().encode()).hexdigest(),expected)

if __name__ == '__main__':
    unittest.main()
