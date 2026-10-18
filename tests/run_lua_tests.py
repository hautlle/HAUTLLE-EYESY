"""Execute helper/example tests using an installed LuaJIT 5.1 library; no GPU."""
from pathlib import Path
import argparse
import ctypes as c
import ctypes.util
import json

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lua-library',default=ctypes.util.find_library('luajit-5.1'))
args=parser.parse_args()
if not args.lua_library:parser.error('Supply --lua-library with the installed LuaJIT 5.1 shared library path')
lua=c.CDLL(args.lua_library)
lua.luaL_newstate.restype=c.c_void_p
lua.luaL_openlibs.argtypes=[c.c_void_p]
lua.luaL_loadstring.argtypes=[c.c_void_p,c.c_char_p]
lua.lua_pcall.argtypes=[c.c_void_p,c.c_int,c.c_int,c.c_int]
lua.lua_tolstring.argtypes=[c.c_void_p,c.c_int,c.c_void_p];lua.lua_tolstring.restype=c.c_char_p
lua.lua_close.argtypes=[c.c_void_p]
variables={'TEST_ROOT':ROOT/'tests/lua','HELPER_PATH':ROOT/'source/app/bin/data/lua/hautlle/v1/frozen_layer_pair.lua',
           'DATA_ROOT':ROOT/'source/app/bin/data','EXAMPLE_PATH':ROOT/'examples/FROZEN_LAYER_PAIR_MINIMAL/main.lua'}
def execute(script):
    vm=lua.luaL_newstate()
    if not vm:raise RuntimeError('Lua VM allocation failed')
    try:
        lua.luaL_openlibs(vm)
        source='\n'.join(k+'='+json.dumps(v.as_posix()) for k,v in variables.items())+'\n'+script
        status=lua.luaL_loadstring(vm,source.encode())
        if not status:status=lua.lua_pcall(vm,0,1,0)
        raw=lua.lua_tolstring(vm,-1,None)
        if status:raise RuntimeError(raw.decode() if raw else 'Lua error')
        return json.loads(raw.decode())
    finally:lua.lua_close(vm)
result={name:execute((ROOT/'tests/lua'/name).read_text()) for name in ['lifecycle.lua','example.lua']}
result['scope']='Actual helper/example under OF stubs; no hardware or pixel validation.'
print(json.dumps(result,indent=2))
