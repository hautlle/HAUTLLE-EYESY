"""Separate, opt-in controller routing for the exact inspected beta PD source."""
# Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see ../LICENSE.txt.
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import uuid
from installer import InstallError, require, no_links, locked, verify_package

HERE=Path(__file__).resolve().parent
PROFILE=json.loads((HERE/'beta-controller.json').read_text())
TARGET=Path('/home/music/EYESY_OS/pd/eyesy.pd')
BACKUP=Path('/var/lib/hautlle-eyesy/controller/eyesy-stock.pd')
PD='eyesy-pd.service'
START=b'#N canvas 83 50 1168 585 shift-params 0;\n'
END=b'#X restore 715 123 pd shift-params;'

def digest(data): return hashlib.sha256(data).hexdigest()

def patched(data):
    require(digest(data)==PROFILE['before_sha256'],'Controller source is not the exact supported stock beta preimage.')
    require(data.count(START)==1 and data.count(END)==1,'Controller canvas identity differs.')
    first=data.index(START);last=data.index(END,first)
    body=data[first:last]
    for old,new in PROFILE['changes']:
        old,new=old.encode(),new.encode()
        require(body.count(old)==1,'Controller patch context differs.')
        body=body.replace(old,new,1)
    result=data[:first]+body+data[last:]
    require(digest(result)==PROFILE['after_sha256'],'Controller output identity differs.')
    return result

def regular(path):
    no_links(path)
    require(path.is_file() and path.stat().st_nlink==1,'Expected an unlinked regular controller file: '+str(path))

def replace(path,data):
    regular(path);info=path.stat()
    tmp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    try:
        with tmp.open('xb') as f: f.write(data);f.flush();os.fsync(f.fileno())
        os.chmod(str(tmp),info.st_mode & 0o777)
        if os.name=='posix': os.chown(str(tmp),info.st_uid,info.st_gid)
        os.replace(str(tmp),str(path))
    finally:
        if tmp.exists(): tmp.unlink()

class Operations:
    def command(self,*args):
        return subprocess.check_output(list(args),universal_newlines=True,stderr=subprocess.STDOUT,timeout=30).strip()
    def video_identity(self):
        result={}
        for service in ('eyesy-oflua.service','eyesy-python.service'):
            result[service]=self.command('systemctl','show',service,'-p','MainPID','-p','ActiveState','-p','ExecMainStartTimestampMonotonic')
        return result
    def restart(self): self.command('systemctl','restart',PD)
    def healthy(self):
        expected=Path('/usr/bin/pd').resolve(strict=True)
        deadline=time.monotonic()+6
        while time.monotonic()<deadline:
            if self.command('systemctl','is-active',PD)=='active':
                pid=self.command('systemctl','show',PD,'-p','MainPID','--value')
                if pid.isdigit() and int(pid)>1:
                    try:
                        children=Path('/proc/'+pid+'/task/'+pid+'/children').read_text().split()
                        if any(Path('/proc/'+p+'/exe').resolve(strict=True)==expected for p in [pid]+children): return
                    except (FileNotFoundError,ProcessLookupError): pass
            time.sleep(0.25)
        raise InstallError('PD did not start the resolved /usr/bin/pd executable.')

def change(action,target=TARGET,backup=BACKUP,ops=None):
    ops=ops or Operations()
    regular(target);old=target.read_bytes();identity=digest(old)
    if action=='apply' and identity==PROFILE['after_sha256']:
        return 'Recorder controller already installed; unchanged. Existing owner backup may be needed for rollback.'
    if action=='rollback' and identity==PROFILE['before_sha256']:
        return 'Stock beta controller already installed; unchanged.'
    if action=='apply':
        new=patched(old)
        if backup.exists():
            regular(backup);require(digest(backup.read_bytes())==PROFILE['before_sha256'],'Controller backup changed.')
        else:
            no_links(backup)
            with backup.open('xb') as f: f.write(old);f.flush();os.fsync(f.fileno())
            backup.chmod(0o600)
    else:
        require(action=='rollback' and identity==PROFILE['after_sha256'],'Controller rollback refuses changed source.')
        regular(backup);new=backup.read_bytes()
        require(digest(new)==PROFILE['before_sha256'],'Controller backup changed.')
    ops.healthy();video=ops.video_identity()
    require(target.read_bytes()==old,'Controller changed during preparation.')
    try:
        replace(target,new);ops.restart();ops.healthy()
        require(target.read_bytes()==new,'Controller changed during restart.')
        require(ops.video_identity()==video,'Video service identity changed; controller recovery required.')
    except BaseException:
        # Restore only our exact replacement. Never overwrite an independent edit.
        current=target.read_bytes()
        require(current in (old,new),'Controller changed externally; backup retained for manual recovery.')
        if current==new: replace(target,old);ops.restart()
        raise
    return ('Recorder controller installed; this fix was physically confirmed by the user on the spare card.' if action=='apply' else
            'Stock beta controller restored. Native engine rollback is separate.')

def main(action):
    require(platform.system()=='Linux','Beta controller actions require the reviewed Linux device.')
    verify_package(HERE.parent)
    regular(TARGET)
    identity=digest(TARGET.read_bytes())
    require(identity in (PROFILE['before_sha256'],PROFILE['after_sha256']),'Unrecognized controller source; left unchanged.')
    if action=='check':
        print('Controller profile: '+('stock-beta' if identity==PROFILE['before_sha256'] else 'beta-recorder'))
        print('Recorder fix physically confirmed by the user on the spare card. Shift+knobs replaces beta shifted settings editing.')
        return 0
    require(os.geteuid()==0,'Use sudo for explicit controller apply/rollback.')
    no_links(BACKUP.parent)
    for parent in [BACKUP.parent]+list(BACKUP.parent.parents):
        if parent.exists():
            st=parent.stat();require(st.st_uid==0 and st.st_mode & 0o022==0,'Controller backup path is not root-owned/private.')
    with locked(BACKUP.parent): print(change(action))
    return 0
