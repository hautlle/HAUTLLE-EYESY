#include "NativeBridge.h"
#include "ofLog.h"

void registerAllGenerators(lua_State* L, const std::vector<NativeGenerator*>& generators) {
    for (auto* gen : generators) {
        gen->registerFunctions(L);
    }
    ofLogNotice("BRIDGE") << "registered " << generators.size() << " native generator(s)";
}
