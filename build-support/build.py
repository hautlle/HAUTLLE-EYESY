"""Build the pinned legacy target from fresh outputs; never installs or runs it."""
from pathlib import Path
import argparse
import copy
import hashlib
import importlib.util
import json
import subprocess
import time

HERE = Path(__file__).resolve().parent
MEMORY_PROFILES = ('original', 'bindings-gc-v1')


def memory_profile(plan, profile):
    """Only the validated SWIG binding command changes; no resource/system action."""
    if profile not in MEMORY_PROFILES:
        raise ValueError('Unknown compiler memory profile: ' + str(profile))
    result = copy.deepcopy(plan)
    if profile == 'original':
        return result
    matches = [u for u in result['units']
               if u['source'].endswith('/addons/ofxLua/src/bindings/linuxarm/ofBindings.cpp')]
    if len(matches) != 1:
        raise ValueError('Memory profile requires exactly one linuxarm ofBindings.cpp unit')
    args = matches[0]['args']
    if [a for a in args if a.startswith('-O')] != ['-O3'] or args.count('-pipe') != 1 or any('ggc-' in a for a in args):
        raise ValueError('Binding command differs from the validated original O3 recipe')
    args.remove('-pipe')
    args[1:1] = ['--param', 'ggc-min-expand=10', '--param', 'ggc-min-heapsize=4096']
    return result


def command_sha256(command):
    return hashlib.sha256(json.dumps(command, separators=(',', ':'), ensure_ascii=True).encode('utf-8')).hexdigest()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def inside(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def dependency_recipe():
    spec = importlib.util.spec_from_file_location('public_dependencies', HERE/'rebuild_dependencies.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_inputs(source, dependencies, dependency_result=None):
    hashes = read(HERE / 'source-hashes.json')
    actual = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    if actual != set(hashes):
        raise ValueError('Source file list differs: missing=%r extra=%r' % (sorted(set(hashes)-actual), sorted(actual-set(hashes))))
    for relative, expected in hashes.items():
        path = source / relative
        if '..' in Path(relative).parts or not inside(path.resolve(), source):
            raise ValueError('Unsafe source manifest path: ' + relative)
        if sha(path) != expected:
            raise ValueError('Source hash mismatch: ' + relative)
    if dependency_result is not None:
        dependency_recipe().validate_result(dependencies, dependency_result)
    else:
        for entry in read(HERE / 'dependencies.json')['external_archives']:
            path = dependencies / entry['path']
            if not path.is_file() or sha(path) != entry['sha256']:
                raise ValueError('Missing or mismatched external archive: ' + str(path))
    return len(hashes)

def expand_plan(plan, source, build, dependencies, system, broadcom, cxx, ar):
    roots = {'${SOURCE}':source.as_posix(), '${BUILD}':build.as_posix(),
             '${DEPENDENCIES}':dependencies.as_posix(), '${SYSTEM}':system.as_posix().rstrip('/'),
             '${BROADCOM}':broadcom.as_posix()}
    def expand(value):
        if isinstance(value, list): return [expand(x) for x in value]
        if isinstance(value, dict): return {k:expand(v) for k,v in value.items()}
        if isinstance(value, str):
            for token, root in roots.items(): value = value.replace(token, root)
            if '${' in value: raise ValueError('Unexpanded plan token: ' + value)
        return value
    expanded = expand(plan)
    for unit in expanded['units']: unit['args'][0] = cxx
    expanded['archive'][0] = ar
    expanded['link'][0] = cxx
    if system.as_posix() != '/':
        for unit in expanded['units']: unit['args'].insert(1, '--sysroot=' + system.as_posix())
        expanded['link'].insert(1, '--sysroot=' + system.as_posix())
    return expanded

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=HERE.parent / 'source')
    parser.add_argument('--build-dir', type=Path, required=True, help='New output directory; never deleted or reused')
    parser.add_argument('--dependency-root', type=Path, required=True, help='Directory containing pinned kiss/ and tess2/ archives')
    parser.add_argument('--dependency-build-result', type=Path,
                        help='Explicit candidate public-source dependency build result; default still requires original archive hashes')
    parser.add_argument('--system-root', type=Path, default=Path('/'), help='Installed target system or matching cross sysroot')
    parser.add_argument('--broadcom-root', type=Path, help='Defaults to SYSTEM/opt/vc')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--ar', default='ar')
    parser.add_argument('--compiler-memory-profile', choices=MEMORY_PROFILES, default='original',
                        help='Optional binding-only GC tuning; does not enable swap or change process limits')
    parser.add_argument('--dry-run', action='store_true', help='Verify inputs and expand commands; do not invoke toolchain')
    parser.add_argument('--plan-output', type=Path, help='Optional expanded plan file, kept outside source')
    args = parser.parse_args(argv)
    source, build, dependencies, system = (p.resolve() for p in [args.source_root,args.build_dir,args.dependency_root,args.system_root])
    broadcom = (args.broadcom_root or system / 'opt/vc').resolve()
    if build == source or inside(build, source) or build == dependencies or inside(build, dependencies):
        raise ValueError('Build output must be outside source and dependency inputs')
    if build.exists(): raise ValueError('Build output already exists; choose a new directory: ' + str(build))
    dependency_result = args.dependency_build_result.resolve() if args.dependency_build_result else None
    count = check_inputs(source, dependencies, dependency_result)
    plan = memory_profile(expand_plan(read(HERE / 'plan.json'), source, build, dependencies, system, broadcom, args.cxx, args.ar), args.compiler_memory_profile)
    if args.plan_output:
        output = args.plan_output.resolve()
        if inside(output, source) or inside(output, dependencies) or inside(output, build):
            raise ValueError('Plan output must be separate from source/dependencies/build output')
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('x', encoding='utf-8') as f: json.dump(plan, f, indent=2)
    if args.dry_run:
        print(json.dumps({'source_files_verified':count,'fresh_units':len(plan['units']),'dry_run':True,'toolchain_or_system_headers_checked':False,
                          'compiler_memory_profile':args.compiler_memory_profile}))
        return

    compiler_version = subprocess.check_output([args.cxx,'-dumpfullversion','-dumpversion'],text=True).strip()
    target = subprocess.check_output([args.cxx,'-dumpmachine'],text=True).strip()
    archive_version = subprocess.check_output([args.ar,'--version'],text=True).splitlines()[0]
    if compiler_version != '8.3.0' or 'arm' not in target or 'gnueabihf' not in target or '2.31.1' not in archive_version:
        raise ValueError('Toolchain differs from qualified target: '+repr((compiler_version,target,archive_version)))
    build.mkdir(parents=True)
    (build/'logs').mkdir()
    binary = Path(plan['link'][plan['link'].index('-o')+1])
    binary.parent.mkdir(parents=True)
    result = {'complete':False,'started':time.time(),'compiler':compiler_version,'target':target,'ar':archive_version,
              'source_manifest_sha256':sha(HERE/'source-hashes.json'),'dependency_manifest_sha256':sha(HERE/'dependencies.json'),
              'plan_sha256':sha(HERE/'plan.json'),'compiled':[],'runtime_qualified':False,
              'compiler_memory_profile':args.compiler_memory_profile}
    result['public_dependency_rebuild_used'] = dependency_result is not None
    if dependency_result is not None:
        result['dependency_build_result_sha256'] = sha(dependency_result)
        result['dependency_archives'] = read(dependency_result)['archives']
    def save():
        (build/'build-result.json').write_text(json.dumps(result,indent=2)+'\n')
    def run(command,label):
        print(label,flush=True)
        with (build/'logs'/(label+'.log')).open('x') as log:
            subprocess.run(command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1800)
    try:
        (build/'expanded-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
        result['effective_plan_sha256'] = sha(build/'expanded-plan.json')
        save()
        for index,unit in enumerate(plan['units']):
            obj = Path(unit['object']);obj.parent.mkdir(parents=True,exist_ok=True)
            if obj.exists(): raise ValueError('Object unexpectedly exists: '+str(obj))
            run(unit['args'],'%03d-%s-%s' % (index,unit['kind'],obj.stem))
            result['compiled'].append({'source':unit['source'],'source_sha256':sha(Path(unit['source'])),'object':str(obj),'object_sha256':sha(obj),
                                       'command':list(unit['args']),'command_sha256':command_sha256(unit['args'])})
            save()
        archive = Path(plan['archive'][2]);archive.parent.mkdir(parents=True,exist_ok=True)
        run(plan['archive'],'archive-core')
        run(plan['link'],'link-engine')
        # Stage only explicitly hashed source data. No copy from a live release.
        data = Path(plan['runtime_data'])
        for item in data.rglob('*'):
            if item.is_file():
                target_path = binary.parent/'data'/item.relative_to(data)
                target_path.parent.mkdir(parents=True,exist_ok=True)
                target_path.write_bytes(item.read_bytes())
        check_inputs(source, dependencies, dependency_result)
        result.update(complete=True,binary=str(binary),binary_sha256=sha(binary),core_sha256=sha(archive))
    except BaseException as error:
        result['error'] = type(error).__name__+': '+str(error)
        raise
    finally:
        result['finished'] = time.time();save()
    print(json.dumps({k:v for k,v in result.items() if k!='compiled'},indent=2))

if __name__ == '__main__':
    main()
