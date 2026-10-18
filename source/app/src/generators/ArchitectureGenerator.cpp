#include "ArchitectureGenerator.h"

#include "ofGraphics.h"
#include "ofLog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float PI_F = 3.14159265358979323846f;
constexpr int ARCH_MAX_ELEMENTS = 30;
}

void ArchitectureGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ArchitectureGenerator::trampolineReset, 1);
    lua_setglobal(L, "architecture_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ArchitectureGenerator::trampolineStep, 1);
    lua_setglobal(L, "architecture_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ArchitectureGenerator::trampolineDraw, 1);
    lua_setglobal(L, "architecture_draw");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ArchitectureGenerator::trampolineMeta, 1);
    lua_setglobal(L, "architecture_meta");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ArchitectureGenerator::trampolineIsReady, 1);
    lua_setglobal(L, "architecture_is_ready");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ArchitectureGenerator::trampolineReseed, 1);
    lua_setglobal(L, "architecture_reseed");
}

int ArchitectureGenerator::trampolineReset(lua_State* L) {
    ArchitectureGenerator* self = static_cast<ArchitectureGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

int ArchitectureGenerator::trampolineStep(lua_State* L) {
    ArchitectureGenerator* self = static_cast<ArchitectureGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) return 0;

    self->step(
        static_cast<float>(luaL_optnumber(L, 1, 1.0 / 60.0)),
        static_cast<float>(luaL_optnumber(L, 2, 0.0)),
        lua_toboolean(L, 3) != 0,
        static_cast<float>(luaL_optnumber(L, 4, 0.40)),
        static_cast<float>(luaL_optnumber(L, 5, 0.25)),
        static_cast<float>(luaL_optnumber(L, 6, 0.72)),
        static_cast<float>(luaL_optnumber(L, 7, 0.35)),
        static_cast<float>(luaL_optnumber(L, 8, 0.45))
    );
    return 0;
}

int ArchitectureGenerator::trampolineDraw(lua_State* L) {
    ArchitectureGenerator* self = static_cast<ArchitectureGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) return 0;

    const int w = static_cast<int>(luaL_optinteger(L, 1, 1280));
    const int h = static_cast<int>(luaL_optinteger(L, 2, 720));
    const bool persistOn = lua_toboolean(L, 3) != 0;
    const bool persistClear = lua_toboolean(L, 4) != 0;
    const int persistFade = static_cast<int>(luaL_optinteger(L, 5, 12));
    self->draw(w, h, persistOn, persistClear, persistFade);
    return 0;
}

int ArchitectureGenerator::trampolineMeta(lua_State* L) {
    ArchitectureGenerator* self = static_cast<ArchitectureGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) {
        lua_pushnumber(L, 0.0);
        return 1;
    }

    const float packed = (static_cast<float>(self->layout) * 4.0f
                        + static_cast<float>(self->state)) / 16.0f;
    lua_pushnumber(L, packed);
    return 1;
}

int ArchitectureGenerator::trampolineIsReady(lua_State* L) {
    ArchitectureGenerator* self = static_cast<ArchitectureGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self && self->ready);
    return 1;
}

int ArchitectureGenerator::trampolineReseed(lua_State* L) {
    ArchitectureGenerator* self = static_cast<ArchitectureGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) {
        self->rebuildScene();
        self->renderIfNeeded(true);
    }
    return 0;
}

float ArchitectureGenerator::clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float ArchitectureGenerator::smooth01(float value) {
    value = clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

float ArchitectureGenerator::lerp(float a, float b, float amount) {
    return a + (b - a) * amount;
}

float ArchitectureGenerator::stressAmount() const {
    // Stage 2 proved that the upper half of K3 is the useful musical range.
    // Compress the polite lower region and spread out the expressive upper
    // region so 0.55..1.0 has much more usable resolution.
    const float zone = clamp01((k3 - 0.22f) / 0.78f);
    const float shaped = smooth01(zone);
    return clamp01(0.06f * k3 + 0.94f * std::pow(shaped, 0.72f));
}

uint32_t ArchitectureGenerator::nextRand() {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x ? x : 0x41524332u;
    return rngState;
}

float ArchitectureGenerator::rand01() {
    return (nextRand() & 0x00ffffffu) / 16777215.0f;
}

float ArchitectureGenerator::randRange(float lo, float hi) {
    return lo + (hi - lo) * rand01();
}

int ArchitectureGenerator::randInt(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + static_cast<int>(nextRand() % static_cast<uint32_t>(hi - lo + 1));
}

void ArchitectureGenerator::ensureAllocated() {
    if (allocated) return;

    pixels.allocate(TEX_W, TEX_H, OF_PIXELS_RGBA);
    texture.allocate(TEX_W, TEX_H, GL_RGBA);
    texture.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
    texture.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

    allocated = texture.isAllocated();
    ready = allocated;

    if (!ready) {
        ofLogError("BRIDGE") << "ArchitectureGenerator STAGE4 texture allocation failed";
        return;
    }

    clearPixels();
    texture.loadData(pixels);
    ofLogNotice("BRIDGE")
        << "ArchitectureGenerator STAGE4 allocated one transparent CPU texture "
        << TEX_W << "x" << TEX_H;
}

void ArchitectureGenerator::clearPixels() {
    if (!allocated) return;
    std::memset(pixels.getData(), 0, static_cast<size_t>(TEX_W * TEX_H) * 4u);
}


// MODE GPU RECLAIM 01
void ArchitectureGenerator::releaseGpuResources() {
    if (texture.isAllocated()) {
        texture.clear();
        ofLogNotice("BRIDGE")
            << "MODE_RECLAIM: Architecture texture released";
    }

    allocated = false;
    ready = false;
}

void ArchitectureGenerator::reset() {
    ensureAllocated();
    if (!ready) return;

    rngState = 0x41524334u;
    sceneSerial = 0u;
    elementCount = 0;
    previousElementCount = 0;
    sceneAge = 0.0f;
    previousSceneAge = 0.0f;
    stateAge = 0.0f;
    stateHold = 4.0f;
    additionCooldown = 0.0f;
    mutationCooldown = 2.0f;
    connectionCooldown = 4.0f;
    transitionAge = 10.0f;
    transitionDuration = 3.0f;
    renderAccumulator = 0.0f;
    energyEnvelope = 0.0f;
    triggerEnvelope = 0.0f;
    previousTrig = false;
    state = STATE_ERECT;
    layout = LAYOUT_HALL;
    previousLayout = LAYOUT_HALL;
    paletteBank = 0;
    previousPaletteBank = 0;

    rebuildScene();
    renderIfNeeded(true);

    ofLogNotice("BRIDGE")
        << "architecture_reset() STAGE4: anchors/discs/planes, parallax/mutation single-texture, persist-aware, no ShaderHost";
}

void ArchitectureGenerator::archiveCurrentScene() {
    previousElementCount = elementCount;
    for (int i = 0; i < elementCount; ++i) {
        previousElements[i] = elements[i];
    }
    previousLayout = layout;
    previousPaletteBank = paletteBank;
    previousSceneAge = std::max(sceneAge, 12.0f);
}

void ArchitectureGenerator::addElement(PrimitiveType type,
                                       float x,
                                       float y,
                                       float w,
                                       float h,
                                       float depth,
                                       float thickness,
                                       float birth,
                                       float duration,
                                       int role,
                                       int variant,
                                       int rows,
                                       int cols,
                                       float opacity) {
    if (elementCount >= MAX_ELEMENTS) return;

    Element& e = elements[elementCount++];
    e.type = type;
    e.pos = glm::vec2(clamp01(x), clamp01(y));
    e.size = glm::vec2(std::max(0.008f, std::min(1.15f, w)),
                       std::max(0.008f, std::min(1.15f, h)));
    e.targetPos = e.pos;
    e.targetSize = e.size;
    e.end = e.pos;
    e.targetEnd = e.pos;
    e.depth = clamp01(depth);
    e.thickness = std::max(0.65f, std::min(5.5f, thickness));
    e.birth = std::max(0.0f, birth);
    e.duration = std::max(0.15f, duration);
    e.phase = randRange(0.0f, PI_F * 2.0f);
    e.opacity = clamp01(opacity);
    e.role = std::max(0, std::min(2, role));
    e.variant = variant;
    e.rows = std::max(0, std::min(8, rows));
    e.cols = std::max(0, std::min(10, cols));
}

void ArchitectureGenerator::addLink(float x0, float y0, float x1, float y1,
                                    float depth, float thickness, float birth,
                                    float duration, int role, int variant,
                                    float opacity) {
    if (elementCount >= MAX_ELEMENTS) return;

    Element& e = elements[elementCount++];
    e.type = PRIM_LINK;
    e.pos = glm::vec2(clamp01(x0), clamp01(y0));
    e.end = glm::vec2(clamp01(x1), clamp01(y1));
    e.targetPos = e.pos;
    e.targetEnd = e.end;
    e.size = glm::vec2(std::fabs(e.end.x - e.pos.x),
                       std::fabs(e.end.y - e.pos.y));
    e.targetSize = e.size;
    e.depth = clamp01(depth);
    e.thickness = std::max(0.55f, std::min(3.5f, thickness));
    e.birth = std::max(0.0f, birth);
    e.duration = std::max(0.18f, duration);
    e.phase = randRange(0.0f, PI_F * 2.0f);
    e.opacity = clamp01(opacity);
    e.role = std::max(0, std::min(2, role));
    e.variant = variant & 3;
    e.rows = 0;
    e.cols = 0;
}

int ArchitectureGenerator::choosePaletteBank(int previousBank) {
    // Automatic coherent palette evolution. Character biases the family,
    // but K4 is not a literal color selector anymore.
    int candidate = 0;
    const float r = rand01();

    if (k4 < 0.30f) {
        // Monumental / severe: bone-red, silver, institutional.
        const int choices[4] = {0, 3, 5, 6};
        candidate = choices[randInt(0, 3)];
    } else if (k4 > 0.72f) {
        // Skeletal / infrastructural: cyan, violet, silver, institutional.
        const int choices[4] = {2, 4, 5, 6};
        candidate = choices[randInt(0, 3)];
    } else {
        candidate = randInt(0, 6);
        if (r < 0.18f) candidate = 1;
    }

    if (candidate == previousBank) {
        candidate = (candidate + 1 + randInt(0, 4)) % 7;
    }
    return candidate;
}

ArchitectureGenerator::LayoutFamily ArchitectureGenerator::chooseLayout(
    LayoutFamily previous) {
    // K4 moves from monumental/orthogonal -> articulated -> skeletal.
    float hallW = lerp(1.55f, 0.70f, k4);
    float facadeW = 1.15f + 0.45f * (1.0f - std::fabs(k4 - 0.5f) * 2.0f);
    float towerW = lerp(0.72f, 1.70f, k4);

    if (previous == LAYOUT_HALL) hallW *= 0.55f;
    if (previous == LAYOUT_FACADE) facadeW *= 0.55f;
    if (previous == LAYOUT_TOWERS) towerW *= 0.55f;

    const float total = hallW + facadeW + towerW;
    float pick = rand01() * total;
    if ((pick -= hallW) <= 0.0f) return LAYOUT_HALL;
    if ((pick -= facadeW) <= 0.0f) return LAYOUT_FACADE;
    return LAYOUT_TOWERS;
}


void ArchitectureGenerator::buildHall() {
    const float character = k4;
    const float buildScale = 1.20f - 0.62f * k2;
    const int portals = 3 + static_cast<int>(std::floor(k1 * 5.0f));
    const float asym = character * 0.040f;

    // Dominant backdrop anchor: a dim disc or a large ring behind the corridor.
    addElement(PRIM_DISC,
               0.50f + randRange(-0.04f, 0.04f),
               randRange(0.30f, 0.42f),
               randRange(0.42f, 0.58f),
               randRange(0.42f, 0.58f),
               0.10f, 1.0f, 0.0f, 0.75f, 2, 0, 0, 0,
               lerp(0.12f, 0.08f, character));

    if (elementCount < MAX_ELEMENTS && (rand01() < 0.82f || character > 0.36f)) {
        addElement(PRIM_RING,
                   0.50f + randRange(-0.06f, 0.06f),
                   randRange(0.46f, 0.58f),
                   randRange(0.30f, 0.44f),
                   randRange(0.22f, 0.34f),
                   0.44f, lerp(1.6f, 0.95f, character),
                   0.28f, 0.95f * buildScale, 1,
                   randInt(0, 2), 0, 0,
                   lerp(0.34f, 0.22f, character));
    }

    // Side and floor/ceiling planes give the hall stronger silhouette contrast.
    addElement(PRIM_MASS, 0.12f, 0.51f, 0.20f, 0.88f,
               0.18f, 1.0f, 0.0f, 0.70f, 2, 0, 0, 0,
               lerp(0.14f, 0.070f, character));
    addElement(PRIM_MASS, 0.88f, 0.51f, 0.20f, 0.88f,
               0.18f, 1.0f, 0.0f, 0.70f, 2, 0, 0, 0,
               lerp(0.14f, 0.070f, character));
    addElement(PRIM_MASS, 0.50f, 0.96f, 1.02f, 0.14f,
               0.16f, 1.0f, 0.0f, 0.62f, 2, 0, 0, 0,
               0.10f);
    if (elementCount < MAX_ELEMENTS) {
        addElement(PRIM_MASS, 0.50f, 0.05f, 1.02f, 0.08f,
                   0.12f, 1.0f, 0.0f, 0.55f, 2, 0, 0, 0,
                   0.07f);
    }

    for (int i = 0; i < portals && elementCount < MAX_ELEMENTS; ++i) {
        const float u = portals > 1 ? i / static_cast<float>(portals - 1) : 0.0f;
        const float depth = 0.24f + 0.72f * u;
        const float w = lerp(0.24f, 0.90f, u);
        const float h = lerp(0.30f, 0.86f, u);
        const float x = 0.50f
                      + std::sin(i * 1.71f + sceneSerial * 0.27f) * asym;
        const float y = 0.485f + u * 0.040f;
        const float thick = lerp(2.6f, 1.25f, character)
                          * lerp(0.80f, 1.25f, u);

        addElement(PRIM_PORTAL, x, y, w, h, depth,
                   thick,
                   i * 0.105f * buildScale,
                   randRange(0.62f, 1.18f) * buildScale,
                   (i % 4 == 0) ? 1 : 0,
                   i & 1);
    }

    const int columnPairs = 2 + static_cast<int>(std::floor(k1 * 3.0f));
    for (int i = 0; i < columnPairs && elementCount + 1 < MAX_ELEMENTS; ++i) {
        const float u = columnPairs > 1 ? i / static_cast<float>(columnPairs - 1) : 0.0f;
        const float xOff = lerp(0.18f, 0.42f, u);
        const float height = lerp(0.44f, 0.82f, u);
        const float birth = 0.22f + i * 0.09f * buildScale;
        const float thick = lerp(2.4f, 1.15f, character);

        addElement(PRIM_COLUMN, 0.50f - xOff, 0.53f, 0.018f, height,
                   0.34f + u * 0.55f, thick, birth,
                   randRange(0.72f, 1.28f) * buildScale, 0, 0);
        addElement(PRIM_COLUMN, 0.50f + xOff, 0.53f, 0.018f, height,
                   0.34f + u * 0.55f, thick, birth + 0.035f,
                   randRange(0.72f, 1.28f) * buildScale, 0, 0);
    }

    addElement(PRIM_SLAB, 0.50f, 0.89f, 0.90f, 0.020f,
               0.96f, lerp(2.6f, 1.5f, character),
               0.48f, 0.90f * buildScale, 1, sceneSerial & 1u);

    if (character > 0.58f && elementCount < MAX_ELEMENTS) {
        addElement(PRIM_BRACE, 0.50f, 0.37f, 0.50f, 0.30f,
                   0.66f, 1.0f, 0.78f, 1.05f * buildScale,
                   1, sceneSerial & 1u);
    }
}



void ArchitectureGenerator::buildFacade() {
    const float character = k4;
    const float buildScale = 1.20f - 0.62f * k2;
    const float frameW = randRange(0.58f, lerp(0.84f, 0.76f, character));
    const float frameH = randRange(0.58f, 0.82f);
    const float asym = lerp(0.010f, 0.085f, character);
    const float cx = 0.50f + randRange(-asym, asym);
    const float cy = 0.51f;

    // Broad background plane and a disc offset behind the facade.
    addElement(PRIM_MASS, 0.50f, 0.53f, 0.96f, 0.82f,
               0.14f, 1.0f, 0.0f, 0.55f, 2, 0, 0, 0,
               lerp(0.10f, 0.065f, character));
    addElement(PRIM_DISC,
               cx + randRange(-0.18f, 0.18f),
               cy + randRange(-0.05f, 0.08f),
               randRange(0.34f, 0.50f),
               randRange(0.34f, 0.50f),
               0.20f, 1.0f, 0.0f, 0.72f, 2, 0, 0, 0,
               lerp(0.10f, 0.06f, character));
    if (elementCount < MAX_ELEMENTS && rand01() < 0.78f) {
        addElement(PRIM_RING,
                   cx + randRange(-0.08f, 0.08f),
                   cy + randRange(-0.03f, 0.04f),
                   randRange(0.18f, 0.34f),
                   randRange(0.18f, 0.30f),
                   0.54f, lerp(1.7f, 0.92f, character),
                   0.26f, 0.90f * buildScale, 1,
                   randInt(0, 2), 0, 0,
                   0.26f);
    }

    addElement(PRIM_MASS, cx, cy, frameW, frameH,
               0.34f, 1.0f, 0.0f, 0.60f, 2, 0, 0, 0,
               lerp(0.17f, 0.085f, character));

    addElement(PRIM_PORTAL, cx, cy, frameW, frameH,
               0.76f, lerp(2.8f, 1.35f, character),
               0.0f, 1.02f * buildScale, 0, 0);

    const int floors = 3 + static_cast<int>(std::floor(k1 * 5.0f));
    for (int i = 0; i < floors && elementCount < MAX_ELEMENTS; ++i) {
        const float u = (i + 1.0f) / (floors + 1.0f);
        const float y = cy - frameH * 0.5f + frameH * u;
        addElement(PRIM_SLAB, cx, y, frameW * randRange(0.82f, 0.96f), 0.014f,
                   0.36f + 0.56f * u,
                   lerp(1.75f, 0.92f, character),
                   0.16f + i * 0.075f * buildScale,
                   randRange(0.48f, 0.88f) * buildScale,
                   (i % 3 == 0) ? 1 : 0,
                   i & 1);
    }

    const int grids = character < 0.18f ? 1 : (1 + static_cast<int>(std::floor(k1 * 2.0f)));
    for (int i = 0; i < grids && elementCount < MAX_ELEMENTS; ++i) {
        const float gx = cx + randRange(-frameW * 0.20f, frameW * 0.20f);
        const float gy = cy + randRange(-frameH * 0.16f, frameH * 0.16f);
        addElement(PRIM_GRID, gx, gy,
                   frameW * randRange(0.30f, 0.58f),
                   frameH * randRange(0.28f, 0.62f),
                   randRange(0.42f, 0.78f),
                   0.80f,
                   0.58f + i * 0.15f,
                   randRange(0.78f, 1.28f) * buildScale,
                   2, 0,
                   randInt(2, 4 + static_cast<int>(k1 * 2.0f)),
                   randInt(2, 5 + static_cast<int>(k1 * 3.0f)));
    }

    if (character > 0.52f && elementCount < MAX_ELEMENTS) {
        addElement(PRIM_BRACE, cx, cy,
                   frameW * 0.88f, frameH * 0.72f,
                   0.82f, 0.95f, 0.88f,
                   1.12f * buildScale, 1,
                   sceneSerial & 1u);
    }
}



void ArchitectureGenerator::buildTowerCluster() {
    const float character = k4;
    const float buildScale = 1.20f - 0.62f * k2;
    const int towers = 2 + static_cast<int>(std::floor(k1 * 3.0f));
    const float spread = lerp(0.56f, 0.76f, character);
    const float spacing = towers > 1 ? spread / static_cast<float>(towers - 1) : 0.0f;
    const float xStart = 0.50f - spread * 0.5f;

    // Celestial / machine-like anchor behind the skyline.
    addElement(PRIM_DISC,
               randRange(0.38f, 0.66f),
               randRange(0.22f, 0.36f),
               randRange(0.34f, 0.52f),
               randRange(0.34f, 0.52f),
               0.10f, 1.0f, 0.0f, 0.72f, 2, 0, 0, 0,
               lerp(0.12f, 0.07f, character));
    if (elementCount < MAX_ELEMENTS) {
        addElement(PRIM_RING,
                   randRange(0.40f, 0.62f),
                   randRange(0.32f, 0.50f),
                   randRange(0.24f, 0.42f),
                   randRange(0.20f, 0.34f),
                   0.42f, lerp(1.8f, 1.0f, character),
                   0.18f, 0.95f * buildScale, 1,
                   randInt(0, 2), 0, 0,
                   0.28f);
    }
    addElement(PRIM_MASS, 0.50f, 0.95f, 1.05f, 0.12f,
               0.14f, 1.0f, 0.0f, 0.55f, 2, 0, 0, 0,
               0.10f);

    for (int i = 0; i < towers && elementCount < MAX_ELEMENTS; ++i) {
        const float depth = 0.30f + 0.62f * (i + 1.0f) / (towers + 1.0f);
        const float x = xStart + spacing * i + randRange(-0.025f, 0.025f) * character;
        const float width = randRange(lerp(0.15f, 0.10f, character),
                                      lerp(0.25f, 0.18f, character));
        const float height = randRange(0.42f, 0.78f);
        const float y = 0.89f - height * 0.5f;
        const float birth = i * 0.13f * buildScale;

        addElement(PRIM_MASS, x, y, width, height,
                   depth - 0.08f, 1.0f, birth, 0.64f,
                   2, 0, 0, 0,
                   lerp(0.18f, 0.065f, character));

        addElement(PRIM_PORTAL, x, y, width, height, depth,
                   lerp(2.25f, 1.05f, character), birth,
                   randRange(0.82f, 1.38f) * buildScale,
                   (i % 3 == 0) ? 1 : 0, i & 1);

        const int decks = 2 + static_cast<int>(std::floor(k1 * 3.0f));
        for (int d = 0; d < decks && elementCount < MAX_ELEMENTS; ++d) {
            const float u = (d + 1.0f) / (decks + 1.0f);
            addElement(PRIM_SLAB,
                       x,
                       y - height * 0.5f + height * u,
                       width * 0.90f,
                       0.012f,
                       depth,
                       lerp(1.35f, 0.80f, character),
                       birth + 0.28f + d * 0.070f * buildScale,
                       randRange(0.42f, 0.82f) * buildScale,
                       d % 4 == 0 ? 1 : 0,
                       (i + d) & 1);
        }

        if (character > 0.42f && i + 1 < towers && elementCount < MAX_ELEMENTS) {
            addElement(PRIM_BRACE,
                       x + spacing * 0.5f,
                       randRange(0.43f, 0.66f),
                       spacing * 0.84f,
                       randRange(0.13f, 0.30f),
                       0.68f,
                       0.90f,
                       birth + 0.74f,
                       1.00f * buildScale,
                       1,
                       (i + sceneSerial) & 1u);
        }
    }

    addElement(PRIM_SLAB, 0.50f, 0.90f, 0.82f, 0.018f,
               0.96f, lerp(2.3f, 1.2f, character),
               0.58f, 0.90f * buildScale, 1, sceneSerial & 1u);
}


void ArchitectureGenerator::rebuildScene() {
    if (elementCount > 0) archiveCurrentScene();

    elementCount = 0;
    sceneAge = 0.0f;
    stateAge = 0.0f;
    additionCooldown = 0.72f;
    mutationCooldown = randRange(2.2f, 4.8f);
    connectionCooldown = randRange(4.2f, 7.8f);
    sceneSerial++;

    layout = chooseLayout(previousLayout);
    paletteBank = choosePaletteBank(previousPaletteBank);
    state = STATE_ERECT;

    switch (layout) {
        case LAYOUT_HALL: buildHall(); break;
        case LAYOUT_FACADE: buildFacade(); break;
        case LAYOUT_TOWERS: buildTowerCluster(); break;
        default: buildHall(); break;
    }

    // Seed one or two deliberate cross-structure connections. High K3 makes
    // them more common, but they remain bounded by MAX_ELEMENTS.
    const float stress = stressAmount();
    if (stress > 0.22f && elementCount < MAX_ELEMENTS) {
        addImpossibleConnection(false);
    }
    if (stress > 0.70f && k4 > 0.42f && elementCount < MAX_ELEMENTS
        && rand01() < 0.62f) {
        addImpossibleConnection(false);
    }

    transitionAge = 0.0f;
    transitionDuration = randRange(2.8f, 5.2f) * (1.10f - 0.32f * k2);
    // Live mutation keeps scenes interesting longer, so full reseeds can breathe.
    nextSceneAt = randRange(36.0f, 66.0f) * (1.12f - 0.44f * k2);
    stateHold = randRange(3.4f, 7.2f) * (1.14f - 0.46f * k2);

    ofLogNotice("BRIDGE")
        << "Architecture STAGE4 scene=" << sceneSerial
        << " layout=" << static_cast<int>(layout)
        << " palette=" << paletteBank
        << " character=" << k4
        << " elements=" << elementCount
        << " next=" << nextSceneAt << "s";
}

void ArchitectureGenerator::addAccent() {
    if (elementCount >= MAX_ELEMENTS) return;

    const float birth = sceneAge;
    const float buildScale = 1.12f - 0.48f * k2;
    const float skeletal = smooth01((k4 - 0.42f) / 0.58f);

    if (state == STATE_SPAN) {
        addElement(PRIM_SLAB,
                   randRange(0.25f, 0.75f),
                   randRange(0.24f, 0.80f),
                   randRange(0.22f, 0.62f),
                   0.014f,
                   randRange(0.38f, 0.92f),
                   lerp(1.7f, 0.9f, k4),
                   birth,
                   randRange(0.50f, 1.00f) * buildScale,
                   1,
                   randInt(0, 1));
    } else if (state == STATE_INFILL) {
        addElement(PRIM_GRID,
                   randRange(0.28f, 0.72f),
                   randRange(0.28f, 0.72f),
                   randRange(0.15f, 0.38f),
                   randRange(0.16f, 0.42f),
                   randRange(0.36f, 0.86f),
                   0.72f,
                   birth,
                   randRange(0.74f, 1.28f) * buildScale,
                   2,
                   0,
                   randInt(2, 5),
                   randInt(2, 6));
    } else if (skeletal > 0.45f && rand01() < skeletal) {
        addElement(PRIM_BRACE,
                   randRange(0.26f, 0.74f),
                   randRange(0.28f, 0.72f),
                   randRange(0.18f, 0.48f),
                   randRange(0.15f, 0.42f),
                   randRange(0.44f, 0.90f),
                   0.85f,
                   birth,
                   randRange(0.58f, 1.15f) * buildScale,
                   1,
                   randInt(0, 1));
    } else {
        addElement(PRIM_PORTAL,
                   randRange(0.27f, 0.73f),
                   randRange(0.30f, 0.70f),
                   randRange(0.12f, 0.34f),
                   randRange(0.18f, 0.48f),
                   randRange(0.32f, 0.90f),
                   lerp(2.15f, 1.0f, k4),
                   birth,
                   randRange(0.65f, 1.25f) * buildScale,
                   rand01() < 0.35f ? 1 : 0,
                   randInt(0, 1));
    }
}

void ArchitectureGenerator::addImpossibleConnection(bool audioKick) {
    if (elementCount >= MAX_ELEMENTS || elementCount < 2) return;

    int ia = -1;
    int ib = -1;
    float bestDistance = 0.0f;

    // Fixed attempt count: no allocations and no unbounded search.
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int a = randInt(0, elementCount - 1);
        const int b = randInt(0, elementCount - 1);
        if (a == b) continue;
        if (elements[a].type == PRIM_MASS || elements[a].type == PRIM_GRID
            || elements[a].type == PRIM_LINK) continue;
        if (elements[b].type == PRIM_MASS || elements[b].type == PRIM_GRID
            || elements[b].type == PRIM_LINK) continue;

        const float dx = elements[a].pos.x - elements[b].pos.x;
        const float dy = elements[a].pos.y - elements[b].pos.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 > bestDistance && d2 > 0.035f) {
            ia = a;
            ib = b;
            bestDistance = d2;
        }
    }

    if (ia < 0 || ib < 0) return;

    glm::vec2 a = elements[ia].pos;
    glm::vec2 b = elements[ib].pos;

    // Attach columns a little above center and slabs toward their ends so the
    // connection reads like impossible infrastructure rather than a random X.
    if (elements[ia].type == PRIM_COLUMN) a.y -= elements[ia].size.y * 0.28f;
    if (elements[ib].type == PRIM_COLUMN) b.y -= elements[ib].size.y * 0.28f;
    if (elements[ia].type == PRIM_SLAB) a.x += (rand01() < 0.5f ? -1.0f : 1.0f) * elements[ia].size.x * 0.34f;
    if (elements[ib].type == PRIM_SLAB) b.x += (rand01() < 0.5f ? -1.0f : 1.0f) * elements[ib].size.x * 0.34f;

    const float stress = stressAmount();
    const float depth = clamp01((elements[ia].depth + elements[ib].depth) * 0.5f
                              + randRange(-0.08f, 0.12f));
    const float thickness = lerp(1.45f, 0.72f, k4) * lerp(0.82f, 1.12f, stress);
    const float duration = randRange(0.72f, 1.48f) * (1.10f - 0.38f * k2);
    const int variant = audioKick ? randInt(1, 3) : randInt(0, 3);

    addLink(a.x, a.y, b.x, b.y, depth, thickness,
            sceneAge, duration, 1, variant,
            audioKick ? 0.95f : 0.78f);
}

void ArchitectureGenerator::mutateOneElement(bool audioKick) {
    if (elementCount <= 0) return;

    int index = -1;
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int candidate = randInt(0, elementCount - 1);
        if (elements[candidate].type == PRIM_MASS && rand01() < 0.70f) continue;
        index = candidate;
        break;
    }
    if (index < 0) return;

    Element& e = elements[index];
    const float stress = stressAmount();
    const float kick = audioKick ? 1.35f : 1.0f;
    const float travel = (0.010f + 0.050f * stress) * kick;
    const float resize = (0.035f + 0.14f * stress) * kick;

    if (e.type == PRIM_LINK) {
        e.targetPos.x = clamp01(e.targetPos.x + randRange(-travel, travel));
        e.targetPos.y = clamp01(e.targetPos.y + randRange(-travel * 0.65f, travel * 0.65f));
        e.targetEnd.x = clamp01(e.targetEnd.x + randRange(-travel, travel));
        e.targetEnd.y = clamp01(e.targetEnd.y + randRange(-travel * 0.65f, travel * 0.65f));
        e.variant = (e.variant + 1 + randInt(0, 2)) & 3;
        return;
    }

    if (e.type == PRIM_COLUMN) {
        e.targetPos.x = clamp01(e.targetPos.x + randRange(-travel, travel));
        e.targetSize.y = std::max(0.08f, std::min(1.05f,
            e.targetSize.y * randRange(1.0f - resize, 1.0f + resize)));
    } else if (e.type == PRIM_SLAB) {
        e.targetPos.y = clamp01(e.targetPos.y + randRange(-travel * 0.65f, travel * 0.65f));
        e.targetSize.x = std::max(0.06f, std::min(1.10f,
            e.targetSize.x * randRange(1.0f - resize, 1.0f + resize)));
        if (stress > 0.58f && rand01() < 0.35f) e.variant ^= 1;
    } else {
        e.targetPos.x = clamp01(e.targetPos.x + randRange(-travel, travel));
        e.targetPos.y = clamp01(e.targetPos.y + randRange(-travel * 0.72f, travel * 0.72f));
        e.targetSize.x = std::max(0.045f, std::min(1.08f,
            e.targetSize.x * randRange(1.0f - resize, 1.0f + resize)));
        e.targetSize.y = std::max(0.045f, std::min(1.05f,
            e.targetSize.y * randRange(1.0f - resize, 1.0f + resize)));
    }
}

void ArchitectureGenerator::updateMutations(float dt, bool trigEdge) {
    const float stress = stressAmount();
    const float settleRate = 0.34f + 0.92f * k2 + 0.72f * stress;
    const float a = std::min(1.0f, dt * settleRate);

    // Smoothly chase targets in-place. Fixed array, zero allocation.
    for (int i = 0; i < elementCount; ++i) {
        Element& e = elements[i];
        e.pos += (e.targetPos - e.pos) * a;
        e.size += (e.targetSize - e.size) * a;
        e.end += (e.targetEnd - e.end) * a;
    }

    mutationCooldown = std::max(0.0f, mutationCooldown - dt);
    connectionCooldown = std::max(0.0f, connectionCooldown - dt);

    if (mutationCooldown <= 0.0f && state != STATE_HOLD) {
        mutateOneElement(false);
        mutationCooldown = randRange(2.6f, 6.2f) * (1.10f - 0.42f * k2);
    }

    if (connectionCooldown <= 0.0f) {
        if (stress > 0.36f && elementCount < MAX_ELEMENTS && rand01() < (0.24f + 0.34f * stress)) {
            addImpossibleConnection(false);
        }
        connectionCooldown = randRange(6.5f, 12.5f) * (1.08f - 0.34f * k2);
    }

    if (trigEdge) {
        if (rand01() < 0.34f + 0.20f * stress) mutateOneElement(true);
        if (stress > 0.48f && elementCount < MAX_ELEMENTS && rand01() < 0.22f + 0.20f * stress) {
            addImpossibleConnection(true);
            connectionCooldown = std::max(connectionCooldown, 2.2f);
        }
    }
}

void ArchitectureGenerator::chooseNextState() {
    const int current = static_cast<int>(state);
    int next = randInt(0, STATE_COUNT - 1);
    if (next == current) next = (next + 1) % STATE_COUNT;
    state = static_cast<BuildState>(next);
    stateAge = 0.0f;
    stateHold = randRange(2.9f, 6.6f) * (1.14f - 0.46f * k2);
}

void ArchitectureGenerator::updateState(float dt, bool trigEdge) {
    stateAge += dt;
    additionCooldown = std::max(0.0f, additionCooldown - dt);
    triggerEnvelope = std::max(0.0f, triggerEnvelope - dt * 2.8f);

    if (stateAge >= stateHold) {
        chooseNextState();
        if (state != STATE_HOLD && rand01() < 0.58f && additionCooldown <= 0.0f) {
            addAccent();
            additionCooldown = 1.10f;
        }
    }

    if (trigEdge) {
        triggerEnvelope = 1.0f;
        if (additionCooldown <= 0.0f && rand01() < 0.38f) {
            addAccent();
            additionCooldown = 0.76f;
        }
        if (rand01() < 0.24f) chooseNextState();
    }
}

float ArchitectureGenerator::progressFor(const Element& e) const {
    return smooth01((sceneAge - e.birth) / std::max(0.15f, e.duration));
}

void ArchitectureGenerator::transformedRect(const Element& e,
                                            float progress,
                                            glm::vec2& pos,
                                            glm::vec2& size) const {
    pos = e.pos;
    size = e.size;

    if (e.type == PRIM_COLUMN) {
        const float newH = e.size.y * progress;
        pos.y = e.pos.y + (e.size.y - newH) * 0.5f;
        size.y = std::max(0.004f, newH);
    } else if (e.type == PRIM_SLAB) {
        const float newW = e.size.x * progress;
        if ((e.variant & 1) == 0) {
            pos.x = e.pos.x - e.size.x * 0.5f + newW * 0.5f;
        } else {
            pos.x = e.pos.x + e.size.x * 0.5f - newW * 0.5f;
        }
        size.x = std::max(0.004f, newW);
    } else if (e.type == PRIM_PORTAL || e.type == PRIM_GRID || e.type == PRIM_MASS) {
        const float s = 0.04f + 0.96f * progress;
        size *= s;
    }
}

ofColor ArchitectureGenerator::paletteColor(int palette, int role, float depth) const {
    static const ofColor banks[7][3] = {
        { ofColor(232,228,211), ofColor(191,39,28),   ofColor(54,58,61)    },
        { ofColor(241,199,43),  ofColor(32,66,130),   ofColor(147,139,117) },
        { ofColor(57,198,211),  ofColor(236,55,42),   ofColor(38,44,65)    },
        { ofColor(217,205,171), ofColor(91,121,75),   ofColor(176,76,42)   },
        { ofColor(149,104,207), ofColor(228,235,241), ofColor(62,57,70)    },
        { ofColor(205,211,214), ofColor(117,125,128), ofColor(52,56,58)    },
        { ofColor(159,187,151), ofColor(230,145,60),  ofColor(67,70,64)    }
    };

    const int bank = std::max(0, std::min(6, palette));
    const int r = std::max(0, std::min(2, role));
    ofColor c = banks[bank][r];

    const float depthGain = lerp(0.48f, 1.10f, clamp01(depth));
    const float atmosphere = lerp(0.90f, 1.09f, k5);
    const float gain = depthGain * atmosphere;

    c.r = static_cast<unsigned char>(std::min(255.0f, c.r * gain));
    c.g = static_cast<unsigned char>(std::min(255.0f, c.g * gain));
    c.b = static_cast<unsigned char>(std::min(255.0f, c.b * gain));
    c.a = 255;
    return c;
}

void ArchitectureGenerator::blendPixel(int x, int y,
                                       const ofColor& color,
                                       float alpha) {
    if ((unsigned)x >= TEX_W || (unsigned)y >= TEX_H) return;
    alpha = clamp01(alpha);
    if (alpha <= 0.001f) return;

    unsigned char* data = pixels.getData();
    const int index = (y * TEX_W + x) * 4;
    const int srcA = static_cast<int>(alpha * 255.0f + 0.5f);

    // Store ordinary (non-premultiplied) RGBA. The strongest contributor
    // owns the pixel color; weaker contributors may only lift channels.
    // This keeps empty pixels alpha=0 so the OS Persist FBO can survive.
    if (srcA >= static_cast<int>(data[index + 3])) {
        data[index + 0] = color.r;
        data[index + 1] = color.g;
        data[index + 2] = color.b;
        data[index + 3] = static_cast<unsigned char>(srcA);
    } else {
        data[index + 0] = static_cast<unsigned char>(std::max(
            static_cast<int>(data[index + 0]),
            static_cast<int>(color.r * alpha)));
        data[index + 1] = static_cast<unsigned char>(std::max(
            static_cast<int>(data[index + 1]),
            static_cast<int>(color.g * alpha)));
        data[index + 2] = static_cast<unsigned char>(std::max(
            static_cast<int>(data[index + 2]),
            static_cast<int>(color.b * alpha)));
    }
}

void ArchitectureGenerator::drawDisc(float cx, float cy, float radius,
                                     const ofColor& color, float alpha) {
    radius = std::max(0.55f, std::min(180.0f, radius));
    const int minX = static_cast<int>(std::floor(cx - radius - 1.0f));
    const int maxX = static_cast<int>(std::ceil (cx + radius + 1.0f));
    const int minY = static_cast<int>(std::floor(cy - radius - 1.0f));
    const int maxY = static_cast<int>(std::ceil (cy + radius + 1.0f));
    const float rr = radius * radius;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = (x + 0.5f) - cx;
            const float dy = (y + 0.5f) - cy;
            const float d2 = dx * dx + dy * dy;
            if (d2 > rr) continue;
            const float edge = 1.0f - std::sqrt(d2 / std::max(rr, 0.001f));
            blendPixel(x, y, color, alpha * (0.30f + 0.70f * edge));
        }
    }
}

void ArchitectureGenerator::drawLine(float x0, float y0, float x1, float y1,
                                     float thickness, const ofColor& color,
                                     float alpha) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, std::min(520, static_cast<int>(std::ceil(length))));
    const float radius = std::max(0.55f, thickness * 0.5f);

    for (int i = 0; i <= steps; ++i) {
        const float u = i / static_cast<float>(steps);
        drawDisc(x0 + dx * u, y0 + dy * u, radius, color, alpha);
    }
}

void ArchitectureGenerator::fillRect(float x0, float y0, float x1, float y1,
                                     const ofColor& color, float alpha) {
    int ix0 = std::max(0, std::min(TEX_W - 1, static_cast<int>(std::floor(std::min(x0, x1)))));
    int ix1 = std::max(0, std::min(TEX_W - 1, static_cast<int>(std::ceil (std::max(x0, x1)))));
    int iy0 = std::max(0, std::min(TEX_H - 1, static_cast<int>(std::floor(std::min(y0, y1)))));
    int iy1 = std::max(0, std::min(TEX_H - 1, static_cast<int>(std::ceil (std::max(y0, y1)))));

    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            blendPixel(x, y, color, alpha);
        }
    }
}

void ArchitectureGenerator::drawPortal(const Element& e,
                                       const glm::vec2& pos,
                                       const glm::vec2& size,
                                       float progress,
                                       const ofColor& color,
                                       float gain) {
    (void)progress;
    const float x0 = (pos.x - size.x * 0.5f) * TEX_W;
    const float x1 = (pos.x + size.x * 0.5f) * TEX_W;
    const float y0 = (pos.y - size.y * 0.5f) * TEX_H;
    const float y1 = (pos.y + size.y * 0.5f) * TEX_H;

    const float halo = (0.45f + 1.20f * k5) * e.thickness;
    ofColor haloColor = color;
    haloColor.r = static_cast<unsigned char>(haloColor.r * 0.52f);
    haloColor.g = static_cast<unsigned char>(haloColor.g * 0.52f);
    haloColor.b = static_cast<unsigned char>(haloColor.b * 0.52f);

    drawLine(x0, y0, x1, y0, e.thickness + halo, haloColor, gain * (0.12f + 0.12f * k5));
    drawLine(x1, y0, x1, y1, e.thickness + halo, haloColor, gain * (0.12f + 0.12f * k5));
    drawLine(x1, y1, x0, y1, e.thickness + halo, haloColor, gain * (0.12f + 0.12f * k5));
    drawLine(x0, y1, x0, y0, e.thickness + halo, haloColor, gain * (0.12f + 0.12f * k5));

    drawLine(x0, y0, x1, y0, e.thickness, color, gain * 0.88f);
    drawLine(x1, y0, x1, y1, e.thickness, color, gain * 0.88f);
    drawLine(x1, y1, x0, y1, e.thickness, color, gain * 0.88f);
    drawLine(x0, y1, x0, y0, e.thickness, color, gain * 0.88f);
}

void ArchitectureGenerator::drawGrid(const Element& e,
                                     const glm::vec2& pos,
                                     const glm::vec2& size,
                                     float progress,
                                     const ofColor& color,
                                     float gain,
                                     bool archived) {
    const float x0 = (pos.x - size.x * 0.5f) * TEX_W;
    const float x1 = (pos.x + size.x * 0.5f) * TEX_W;
    const float y0 = (pos.y - size.y * 0.5f) * TEX_H;
    const float y1 = (pos.y + size.y * 0.5f) * TEX_H;

    const int rows = std::max(1, archived ? std::min(2, e.rows) : e.rows);
    const int cols = std::max(1, archived ? std::min(3, e.cols) : e.cols);
    const int visibleRows = std::max(0, std::min(rows, static_cast<int>(std::ceil(rows * progress))));
    const int visibleCols = std::max(0, std::min(cols, static_cast<int>(std::ceil(cols * progress))));

    ofColor gridColor = color;
    gridColor.r = static_cast<unsigned char>(gridColor.r * 0.72f);
    gridColor.g = static_cast<unsigned char>(gridColor.g * 0.72f);
    gridColor.b = static_cast<unsigned char>(gridColor.b * 0.72f);

    for (int r = 1; r <= visibleRows; ++r) {
        const float y = y0 + (y1 - y0) * r / (rows + 1.0f);
        drawLine(x0, y, x1, y, 0.65f + 0.30f * k5, gridColor, gain * 0.56f);
    }
    for (int c = 1; c <= visibleCols; ++c) {
        const float x = x0 + (x1 - x0) * c / (cols + 1.0f);
        drawLine(x, y0, x, y1, 0.65f + 0.30f * k5, gridColor, gain * 0.52f);
    }
}

void ArchitectureGenerator::drawBrace(const Element& e,
                                      const glm::vec2& pos,
                                      const glm::vec2& size,
                                      float progress,
                                      const ofColor& color,
                                      float gain) {
    const float x0 = (pos.x - size.x * 0.5f) * TEX_W;
    const float x1 = (pos.x + size.x * 0.5f) * TEX_W;
    const float y0 = (pos.y - size.y * 0.5f) * TEX_H;
    const float y1 = (pos.y + size.y * 0.5f) * TEX_H;

    const bool reverse = (e.variant & 1) != 0;
    const float ax = reverse ? x1 : x0;
    const float ay = y1;
    const float bx = reverse ? x0 : x1;
    const float by = y0;
    const float ex = ax + (bx - ax) * progress;
    const float ey = ay + (by - ay) * progress;

    drawLine(ax, ay, ex, ey, e.thickness + 1.1f * k5, color, gain * 0.22f);
    drawLine(ax, ay, ex, ey, e.thickness, color, gain * 0.78f);

    if (k4 > 0.72f && progress > 0.70f) {
        const float p2 = smooth01((progress - 0.70f) / 0.30f);
        const float ax2 = reverse ? x0 : x1;
        const float ay2 = y1;
        const float bx2 = reverse ? x1 : x0;
        const float by2 = y0;
        drawLine(ax2, ay2,
                 ax2 + (bx2 - ax2) * p2,
                 ay2 + (by2 - ay2) * p2,
                 e.thickness, color, gain * 0.50f);
    }
}

void ArchitectureGenerator::drawMass(const Element& e,
                                     const glm::vec2& pos,
                                     const glm::vec2& size,
                                     float progress,
                                     const ofColor& color,
                                     float gain) {
    (void)progress;
    const float x0 = (pos.x - size.x * 0.5f) * TEX_W;
    const float x1 = (pos.x + size.x * 0.5f) * TEX_W;
    const float y0 = (pos.y - size.y * 0.5f) * TEX_H;
    const float y1 = (pos.y + size.y * 0.5f) * TEX_H;

    // A near-black cover pass lets front masses actually interrupt structures
    // behind them. It is intentionally translucent, so the result remains
    // diagrammatic rather than becoming flat opaque rectangles.
    ofColor occlusion(3, 4, 5, 255);
    const float cover = clamp01(gain * (1.60f + e.depth * k5 * 1.00f));
    fillRect(x0, y0, x1, y1, occlusion, cover);

    ofColor massColor = color;
    const float shade = lerp(0.34f, 0.52f, e.depth);
    massColor.r = static_cast<unsigned char>(massColor.r * shade);
    massColor.g = static_cast<unsigned char>(massColor.g * shade);
    massColor.b = static_cast<unsigned char>(massColor.b * shade);
    fillRect(x0, y0, x1, y1, massColor, gain * 0.92f);
}


void ArchitectureGenerator::drawDiscField(const Element& e,
                                          const glm::vec2& pos,
                                          const glm::vec2& size,
                                          float progress,
                                          const ofColor& color,
                                          float gain) {
    const float cx = pos.x * TEX_W;
    const float cy = pos.y * TEX_H;
    const float radius = 0.5f * std::min(size.x * TEX_W, size.y * TEX_H)
                       * lerp(0.55f, 1.00f, progress);

    ofColor occlusion(3, 4, 5, 255);
    drawDisc(cx, cy, radius * 1.05f, occlusion,
             gain * (0.40f + 0.50f * e.depth));

    ofColor fill = color;
    const float shade = lerp(0.26f, 0.62f, e.depth);
    fill.r = static_cast<unsigned char>(fill.r * shade);
    fill.g = static_cast<unsigned char>(fill.g * shade);
    fill.b = static_cast<unsigned char>(fill.b * shade);

    drawDisc(cx, cy, radius, fill, gain * 0.82f);
}

void ArchitectureGenerator::drawRingField(const Element& e,
                                          const glm::vec2& pos,
                                          const glm::vec2& size,
                                          float progress,
                                          const ofColor& color,
                                          float gain) {
    const float cx = pos.x * TEX_W;
    const float cy = pos.y * TEX_H;
    const float radius = 0.5f * std::min(size.x * TEX_W, size.y * TEX_H)
                       * lerp(0.45f, 1.00f, progress);
    const float stress = stressAmount();
    const float thickness = std::max(0.8f, e.thickness + 0.65f * k5);
    const int segments = 44;
    float startA = -0.10f * PI_F;
    float sweepA = PI_F * (1.70f + 0.22f * k4);
    if (e.variant == 1) {
        startA = 0.18f * PI_F;
        sweepA = 1.42f * PI_F;
    } else if (e.variant == 2) {
        startA = -0.55f * PI_F;
        sweepA = 1.18f * PI_F;
    }

    ofColor halo = color;
    halo.r = static_cast<unsigned char>(std::min(255, int(halo.r * 0.55f)));
    halo.g = static_cast<unsigned char>(std::min(255, int(halo.g * 0.55f)));
    halo.b = static_cast<unsigned char>(std::min(255, int(halo.b * 0.55f)));

    for (int i = 0; i < segments; ++i) {
        const float u0 = i / static_cast<float>(segments);
        const float u1 = (i + 1) / static_cast<float>(segments);
        const float gate = 0.5f + 0.5f * std::sin(e.phase * 1.9f + i * 1.37f);
        if (stress > 0.58f && gate < (0.16f + 0.18f * (stress - 0.58f) / 0.42f)) {
            continue;
        }
        const float a0 = startA + sweepA * u0;
        const float a1 = startA + sweepA * u1;
        const float x0 = cx + std::cos(a0) * radius;
        const float y0 = cy + std::sin(a0) * radius;
        const float x1 = cx + std::cos(a1) * radius;
        const float y1 = cy + std::sin(a1) * radius;
        drawLine(x0, y0, x1, y1, thickness + 1.1f, halo, gain * 0.18f);
        drawLine(x0, y0, x1, y1, thickness, color, gain * 0.76f);
    }
}

void ArchitectureGenerator::drawLink(const Element& e,
                                     const glm::vec2& start,
                                     const glm::vec2& endPoint,
                                     float progress,
                                     const ofColor& color,
                                     float gain) {
    const float ax = start.x * TEX_W;
    const float ay = start.y * TEX_H;
    const float bx = endPoint.x * TEX_W;
    const float by = endPoint.y * TEX_H;

    float mx = (ax + bx) * 0.5f;
    float my = (ay + by) * 0.5f;
    if (e.variant == 1) {
        mx = bx;
        my = ay;
    } else if (e.variant == 2) {
        mx = ax;
        my = by;
    } else if (e.variant == 3) {
        const float stress = stressAmount();
        mx += std::sin(e.phase) * (7.0f + 18.0f * stress);
        my += std::cos(e.phase * 1.37f) * (4.0f + 10.0f * stress);
    }

    const float halo = e.thickness + 1.0f + 0.8f * k5;
    if (progress < 0.5f) {
        const float q = smooth01(progress * 2.0f);
        const float ex = ax + (mx - ax) * q;
        const float ey = ay + (my - ay) * q;
        drawLine(ax, ay, ex, ey, halo, color, gain * 0.16f);
        drawLine(ax, ay, ex, ey, e.thickness, color, gain * 0.82f);
    } else {
        drawLine(ax, ay, mx, my, halo, color, gain * 0.16f);
        drawLine(ax, ay, mx, my, e.thickness, color, gain * 0.82f);
        const float q = smooth01((progress - 0.5f) * 2.0f);
        const float ex = mx + (bx - mx) * q;
        const float ey = my + (by - my) * q;
        drawLine(mx, my, ex, ey, halo, color, gain * 0.16f);
        drawLine(mx, my, ex, ey, e.thickness, color, gain * 0.82f);
    }
}

void ArchitectureGenerator::drawElement(const Element& e,
                                        int index,
                                        float sceneTime,
                                        int palette,
                                        float gain,
                                        bool archived) {
    float progress = archived
        ? 1.0f
        : smooth01((sceneTime - e.birth) / std::max(0.15f, e.duration));
    if (progress <= 0.0f || gain <= 0.001f) return;

    glm::vec2 pos;
    glm::vec2 size;
    transformedRect(e, progress, pos, size);

    float stressScale = stressAmount() * (state == STATE_HOLD ? 0.42f : 1.0f);
    stressScale *= 0.55f + 0.45f * energyEnvelope;
    const float depthStress = lerp(0.55f, 1.0f, e.depth);
    const float wobbleX = std::sin(sceneTime * 0.59f + e.phase + index * 0.19f)
                        * stressScale * depthStress * (2.8f / TEX_W);
    const float wobbleY = std::cos(sceneTime * 0.43f + e.phase * 1.31f)
                        * stressScale * depthStress * (1.8f / TEX_H);
    pos.x += wobbleX;
    pos.y += wobbleY;

    // Slow camera/parallax drift separates the three depth planes without a
    // second texture or GPU pass. Front and back layers move oppositely.
    const float depthSigned = e.depth - 0.5f;
    const float parallax = (0.007f + 0.038f * k5)
                         * (0.32f + 0.68f * stressScale)
                         * (archived ? 0.42f : 1.0f);
    const float camX = std::sin(sceneTime * 0.115f + sceneSerial * 0.43f);
    const float camY = std::cos(sceneTime * 0.087f + sceneSerial * 0.31f);
    pos.x += depthSigned * parallax * camX;
    pos.y += depthSigned * parallax * 0.48f * camY;

    const glm::vec2 centerVec = pos - glm::vec2(0.5f, 0.5f);
    pos += centerVec * (depthSigned * 0.026f * k5 * stressScale);

    // Local structural slip in the useful upper K3 range.
    pos.x += std::sin(sceneTime * 0.17f + e.depth * 10.7f + e.phase)
           * stressScale * (0.002f + 0.009f * k5);

    ofColor color = paletteColor(palette, e.role, e.depth);
    if (!archived && triggerEnvelope > 0.0f && e.role == 1) {
        const float lift = 1.0f + triggerEnvelope * 0.20f;
        color.r = static_cast<unsigned char>(std::min(255.0f, color.r * lift));
        color.g = static_cast<unsigned char>(std::min(255.0f, color.g * lift));
        color.b = static_cast<unsigned char>(std::min(255.0f, color.b * lift));
    }

    gain *= e.opacity;

    glm::vec2 linkEnd = e.end;
    if (e.type == PRIM_LINK) {
        linkEnd.x += depthSigned * parallax * camX;
        linkEnd.y += depthSigned * parallax * 0.48f * camY;
        linkEnd.x -= std::sin(sceneTime * 0.13f + e.phase)
                   * stressScale * (0.002f + 0.008f * k5);
    }

    if (e.type == PRIM_PORTAL) {
        drawPortal(e, pos, size, progress, color, gain);
    } else if (e.type == PRIM_COLUMN) {
        const float x = pos.x * TEX_W;
        const float y0 = (pos.y - size.y * 0.5f) * TEX_H;
        const float y1 = (pos.y + size.y * 0.5f) * TEX_H;
        drawLine(x, y0, x, y1, e.thickness + 1.25f * k5, color, gain * 0.20f);
        drawLine(x, y0, x, y1, e.thickness, color, gain * 0.82f);
    } else if (e.type == PRIM_SLAB) {
        const float x0 = (pos.x - size.x * 0.5f) * TEX_W;
        const float x1 = (pos.x + size.x * 0.5f) * TEX_W;
        const float y = pos.y * TEX_H;
        drawLine(x0, y, x1, y, e.thickness + 1.35f * k5, color, gain * 0.21f);
        drawLine(x0, y, x1, y, e.thickness, color, gain * 0.84f);
    } else if (e.type == PRIM_GRID) {
        drawGrid(e, pos, size, progress, color, gain, archived);
    } else if (e.type == PRIM_BRACE) {
        drawBrace(e, pos, size, progress, color, gain);
    } else if (e.type == PRIM_MASS) {
        drawMass(e, pos, size, progress, color, gain);
    } else if (e.type == PRIM_DISC) {
        drawDiscField(e, pos, size, progress, color, gain);
    } else if (e.type == PRIM_RING) {
        drawRingField(e, pos, size, progress, color, gain);
    } else if (e.type == PRIM_LINK) {
        drawLink(e, pos, linkEnd, progress, color, gain);
    }
}

void ArchitectureGenerator::renderDepthPass(const std::array<Element, 30>& source,
                                            int count,
                                            float sceneTime,
                                            int palette,
                                            float gain,
                                            bool archived,
                                            int pass) {
    const float low = pass == 0 ? 0.0f : (pass == 1 ? 0.40f : 0.72f);
    const float high = pass == 0 ? 0.40f : (pass == 1 ? 0.72f : 1.01f);

    // Masses first within each depth plane, then structural lines.
    for (int i = 0; i < count; ++i) {
        const Element& e = source[i];
        if (e.depth < low || e.depth >= high || e.type != PRIM_MASS && e.type != PRIM_DISC && e.type != PRIM_RING) continue;
        drawElement(e, i, sceneTime, palette, gain, archived);
    }
    for (int i = 0; i < count; ++i) {
        const Element& e = source[i];
        if (e.depth < low || e.depth >= high || e.type == PRIM_MASS || e.type == PRIM_DISC || e.type == PRIM_RING) continue;
        drawElement(e, i, sceneTime, palette, gain, archived);
    }
}

void ArchitectureGenerator::renderArray(const std::array<Element, 30>& source,
                                        int count,
                                        float sceneTime,
                                        int palette,
                                        float gain,
                                        bool archived) {
    if (gain <= 0.001f || count <= 0) return;
    renderDepthPass(source, count, sceneTime, palette, gain, archived, 0);
    renderDepthPass(source, count, sceneTime, palette, gain, archived, 1);
    renderDepthPass(source, count, sceneTime, palette, gain, archived, 2);
}

void ArchitectureGenerator::renderScene() {
    if (!ready) return;
    clearPixels();

    const float transition = smooth01(
        transitionAge / std::max(0.15f, transitionDuration));
    const float oldGain = previousElementCount > 0 ? (1.0f - transition) : 0.0f;
    const float newGain = previousElementCount > 0 ? (0.22f + 0.78f * transition) : 1.0f;

    if (oldGain > 0.001f) {
        renderArray(previousElements, previousElementCount,
                    previousSceneAge + transitionAge,
                    previousPaletteBank,
                    oldGain * 0.78f,
                    true);
    }

    renderArray(elements, elementCount, sceneAge,
                paletteBank, newGain, false);

    texture.loadData(pixels);
}

void ArchitectureGenerator::renderIfNeeded(bool force) {
    if (!ready) return;

    const bool transitioning = previousElementCount > 0
                            && transitionAge < transitionDuration;
    // Crossfade temporarily doubles scene work, so lower the native update
    // rate during that short window. The display still runs independently.
    const float interval = transitioning ? (1.0f / 17.0f) : (1.0f / 21.0f);
    if (!force && renderAccumulator < interval) return;

    if (!force) renderAccumulator = std::fmod(renderAccumulator, interval);
    else renderAccumulator = 0.0f;

    renderScene();

    if (transitionAge >= transitionDuration && previousElementCount > 0) {
        previousElementCount = 0;
    }
}

void ArchitectureGenerator::step(float dt,
                                 float peak,
                                 bool trigFired,
                                 float knob1,
                                 float knob2,
                                 float knob3,
                                 float knob4,
                                 float knob5) {
    if (!ready) ensureAllocated();
    if (!ready) return;

    dt = std::max(0.0f, std::min(0.10f, dt));
    k1 = clamp01(knob1);
    k2 = clamp01(knob2);
    k3 = clamp01(knob3);
    k4 = clamp01(knob4);
    k5 = clamp01(knob5);

    peak = clamp01(peak);
    energyEnvelope += (peak - energyEnvelope) * std::min(1.0f, dt * 7.0f);

    const bool trigEdge = trigFired && !previousTrig;
    previousTrig = trigFired;

    sceneAge += dt;
    transitionAge += dt;
    renderAccumulator += dt;
    updateState(dt, trigEdge);
    updateMutations(dt, trigEdge);

    if (sceneAge >= nextSceneAt) {
        rebuildScene();
    }

    renderIfNeeded(false);
}

void ArchitectureGenerator::draw(int w, int h,
                                   bool persistOn,
                                   bool persistClear,
                                   int persistFade) {
    if (!ready || !texture.isAllocated()) return;
    w = std::max(1, w);
    h = std::max(1, h);
    persistFade = std::max(2, std::min(64, persistFade));

    ofPushStyle();
    ofEnableAlphaBlending();

    // The OS renderCanvas intentionally leaves background/fade behavior to
    // each mode. Persist OFF owns an opaque black frame; Persist ON applies
    // only a small black wash so prior FBO contents remain visible.
    if (!persistOn || persistClear) {
        ofSetColor(0, 0, 0, 255);
    } else {
        ofSetColor(0, 0, 0, persistFade);
    }
    ofDrawRectangle(0, 0, w, h);

    ofSetColor(255, 255, 255, 255);
    texture.draw(0, 0, w, h);
    ofPopStyle();
}
