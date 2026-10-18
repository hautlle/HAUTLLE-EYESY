"""Build candidate KissFFT/libtess2 archives from pinned public source; no install/network."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import subprocess
import time

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / 'dependency-sources'
ARCHIVES = ['kiss/lib/linuxarmv6l/libkiss.a', 'tess2/lib/linuxarmv6l/libtess2.a']
TESS_UNITS = ['dict', 'priorityq', 'geom', 'mesh', 'sweep', 'bucketalloc', 'tess']

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def inside(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False

def check_sources():
    manifest = read(HERE/'dependency-sources.json')
    actual = {p.relative_to(SOURCE).as_posix() for p in SOURCE.rglob('*') if p.is_file()}
    if actual != set(manifest['sources']):
        raise ValueError('Dependency source file list differs')
    for name, row in manifest['sources'].items():
        path = SOURCE/name
        if not inside(path.resolve(), SOURCE.resolve()) or sha(path) != row['sha256']:
            raise ValueError('Dependency source hash/path mismatch: '+name)
    for name, row in manifest['reference_files'].items():
        path = HERE/'dependency-provenance'/name
        if not inside(path.resolve(), (HERE/'dependency-provenance').resolve()) or sha(path) != row['sha256']:
            raise ValueError('Dependency provenance hash/path mismatch: '+name)
    return manifest

def make_plan(output, cc, ar):
    common = ['-std=gnu11', '-march=armv6', '-mfpu=vfp', '-mfloat-abi=hard', '-fPIC']
    units = []
    for name in ['kiss_fft.c', 'tools/kiss_fftr.c']:
        source = SOURCE/'kissfft'/name
        obj = output/'objects/kiss'/(source.stem+'.o')
        units.append({'source':str(source), 'object':str(obj),
                      'args':[cc]+common+['-O0', '-I'+str(SOURCE/'kissfft'), '-c', str(source), '-o', str(obj)]})
    for name in TESS_UNITS:
        source = SOURCE/'libtess2/Source'/(name+'.c')
        obj = output/'objects/tess2'/(name+'.o')
        units.append({'source':str(source), 'object':str(obj),
                      'args':[cc]+common+['-O2','-DNDEBUG','-Wall','-I'+str(SOURCE/'libtess2/Include'),
                              '-I'+str(SOURCE/'libtess2/Source'),'-c',str(source),'-o',str(obj)]})
    archives = [[ar,'rcsD',str(output/ARCHIVES[0])]+[r['object'] for r in units[:2]],
                [ar,'rcsD',str(output/ARCHIVES[1])]+[r['object'] for r in units[2:]]]
    smoke = [cc]+common+['-I'+str(SOURCE/'kissfft'),'-I'+str(SOURCE/'kissfft/tools'),
              '-I'+str(SOURCE/'libtess2/Include'),str(HERE/'dependency_smoke.c'),
              str(output/ARCHIVES[0]),str(output/ARCHIVES[1]),'-lm','-o',str(output/'dependency-smoke')]
    return {'units':units,'archives':archives,'smoke_compile':smoke,'smoke_run':[str(output/'dependency-smoke')]}

def validate_result(dependency_root, result_path):
    """Verify this recipe's successful native result; never silently relax legacy pins."""
    check_sources()
    result = read(result_path)
    root = dependency_root.resolve()
    if result_path.resolve() != root/'dependency-build-result.json':
        raise ValueError('Dependency result must be at dependency-root/dependency-build-result.json')
    if result.get('schema') != 1 or result.get('complete') is not True or result.get('smoke_passed') is not True:
        raise ValueError('Incomplete or failed public dependency build/smoke')
    if result.get('recipe_sha256') != sha(HERE/'rebuild_dependencies.py') or result.get('source_manifest_sha256') != sha(HERE/'dependency-sources.json') or result.get('smoke_source_sha256') != sha(HERE/'dependency_smoke.c'):
        raise ValueError('Dependency recipe/source/smoke identity mismatch')
    if result.get('toolchain',{}).get('compiler_version') != '8.3.0' or 'gnueabihf' not in result.get('toolchain',{}).get('target','') or '2.31.1' not in result.get('toolchain',{}).get('ar_version',''):
        raise ValueError('Dependency toolchain identity differs')
    expected_sources = {r['source'] for r in make_plan(root,'gcc','ar')['units']}
    compiled = result.get('compiled',[])
    if len(compiled) != 9 or {r['source'] for r in compiled} != expected_sources:
        raise ValueError('Dependency compile record differs')
    for r in compiled:
        if sha(Path(r['source'])) != r['source_sha256']:
            raise ValueError('Dependency compile source drift')
        obj = Path(r['object']).resolve()
        if not inside(obj,root/'objects') or sha(obj) != r['object_sha256']:
            raise ValueError('Dependency object hash/path mismatch')
    rows = result.get('archives',[])
    if len(rows) != 2 or {r['path'] for r in rows} != set(ARCHIVES):
        raise ValueError('Dependency archive list differs')
    for row in rows:
        path = root/row['path']
        if not inside(path.resolve(),root) or sha(path) != row['sha256']:
            raise ValueError('Dependency archive hash/path mismatch')
    return result

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True,help='New dependency output, never reused or deleted')
    parser.add_argument('--cc',default='gcc')
    parser.add_argument('--ar',default='ar')
    parser.add_argument('--dry-run',action='store_true')
    args = parser.parse_args(argv)
    output = args.output.resolve()
    package = HERE.parent.resolve()
    if output.exists() or inside(output,package) or inside(package,output):
        raise ValueError('Choose a new output directory outside the source package')
    manifest = check_sources()
    plan = make_plan(output,args.cc,args.ar)
    if args.dry_run:
        print(json.dumps({'source_files_verified':len(manifest['sources']),'fresh_dependency_units':len(plan['units']),
                          'plan':plan,'toolchain_checked':False,'compiled':False},indent=2))
        return
    if platform.system() != 'Linux' or platform.machine() not in ('armv6l','armv7l','armv8l'):
        raise ValueError('Native 32-bit ARM Linux required so the CPU smoke actually runs; cross-build not qualified')
    version = subprocess.check_output([args.cc,'-dumpfullversion','-dumpversion'],text=True).strip()
    target = subprocess.check_output([args.cc,'-dumpmachine'],text=True).strip()
    ar_version = subprocess.check_output([args.ar,'--version'],text=True).splitlines()[0]
    if version != '8.3.0' or 'arm' not in target or 'gnueabihf' not in target or '2.31.1' not in ar_version:
        raise ValueError('Use the recorded GCC 8.3.0 / arm gnueabihf / binutils 2.31.1 toolchain')
    output.mkdir(parents=True)
    (output/'logs').mkdir()
    result = {'schema':1,'complete':False,'smoke_passed':False,'runtime_qualified':False,
              'historical_archive_bytes_reproduced':False,'started':time.time(),
              'recipe_sha256':sha(HERE/'rebuild_dependencies.py'),
              'source_manifest_sha256':sha(HERE/'dependency-sources.json'),
              'smoke_source_sha256':sha(HERE/'dependency_smoke.c'),
              'toolchain':{'compiler_version':version,'target':target,'ar_version':ar_version},
              'compiled':[],'archives':[]}
    def save():
        (output/'dependency-build-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def run(command,name,timeout=120):
        with (output/'logs'/(name+'.log')).open('x',encoding='utf-8') as log:
            subprocess.run(command,cwd=output,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)
    try:
        (output/'dependency-plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
        for i, unit in enumerate(plan['units']):
            obj=Path(unit['object']);obj.parent.mkdir(parents=True,exist_ok=True)
            run(unit['args'],'%02d-%s'%(i,obj.stem))
            result['compiled'].append({'source':unit['source'],'source_sha256':sha(Path(unit['source'])),
                                       'object':str(obj),'object_sha256':sha(obj)})
            save()
        for i, command in enumerate(plan['archives']):
            Path(command[2]).parent.mkdir(parents=True,exist_ok=True)
            run(command,'archive-%d'%i)
            result['archives'].append({'path':ARCHIVES[i],'sha256':sha(Path(command[2]))})
        run(plan['smoke_compile'],'smoke-compile')
        run(plan['smoke_run'],'smoke-run',timeout=10)
        check_sources()
        result.update(smoke_passed=True,complete=True)
    except BaseException as error:
        result['error']=type(error).__name__+': '+str(error)
        raise
    finally:
        result['finished']=time.time();save()
    print(json.dumps(result,indent=2))

if __name__ == '__main__':
    main()
