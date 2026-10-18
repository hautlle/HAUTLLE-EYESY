#pragma once
#include "lua.hpp"

class NativeGenerator {
public:
    virtual ~NativeGenerator() {}
    virtual void registerFunctions(lua_State* L) = 0;

    // Optional. Not called automatically by the engine anywhere.
    // Exists so a generator has a conventional method to bind a
    // Lua-callable reset wrapper to, for a mode to call explicitly.
    virtual void reset() {}
};
