#pragma once
#include "lua.hpp"
#include "NativeGenerator.h"
#include <vector>

void registerAllGenerators(lua_State* L, const std::vector<NativeGenerator*>& generators);
