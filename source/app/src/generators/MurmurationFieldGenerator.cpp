#include "MurmurationFieldGenerator.h"

#include "ofGraphics.h"
#include "ofLog.h"

#include <algorithm>
#include <cmath>

using mfld::Vec3;

namespace {
constexpr float PI_F = 3.14159265358979323846f;
constexpr float TWO_PI_F = 6.28318530717958647692f;

// Simulation tuning (camera-space units; a bird body is ~2 units long).
constexpr float GRID_CELL = 10.0f;         // must be >= max neighbour radius
constexpr float TOPO_NEIGHBORS = 7.0f;     // starlings track ~7 neighbours
constexpr int   MAX_NEIGHBOR_CHECKS = 64;
constexpr float SEP_RADIUS = 3.0f;
constexpr float BODY_LEN = 1.40f;
// Camera distance to the flock. 1.0 = original (~190 units away); smaller
// values bring the whole flock closer, so birds and flock both read larger.
constexpr float FLOCK_DIST = 0.62f;
constexpr float WING_SPAN = 0.36f;         // x BODY_LEN, keeps triangles obtuse
constexpr float WING_SWEEP = -0.08f;       // tip sits slightly behind mid-body
constexpr float FOV_Y = 0.873f;            // 50 degrees
constexpr float MIN_HINGE_PX = 2.2f;
constexpr float MIN_WING_PX = 0.85f;

inline float fieldHash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (x & 0x00ffffffu) / 16777215.0f;
}
} // namespace

// ---------------------------------------------------------------- Lua bridge

void MurmurationFieldGenerator::registerFunctions(lua_State* L) {
    struct Entry { const char* name; lua_CFunction fn; };
    const Entry entries[] = {
        {"murmfield_prepare", &MurmurationFieldGenerator::trampolinePrepare},
        {"murmfield_reset", &MurmurationFieldGenerator::trampolineReset},
        {"murmfield_step", &MurmurationFieldGenerator::trampolineStep},
        {"murmfield_draw", &MurmurationFieldGenerator::trampolineDraw},
        {"murmfield_is_ready", &MurmurationFieldGenerator::trampolineIsReady},
        {"murmfield_stats", &MurmurationFieldGenerator::trampolineStats},
        {"murmfield_release", &MurmurationFieldGenerator::trampolineRelease},
    };
    for (const Entry& e : entries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, e.fn, 1);
        lua_setglobal(L, e.name);
    }
}

#define MSK_SELF \
    MurmurationFieldGenerator* self = static_cast<MurmurationFieldGenerator*>( \
        lua_touserdata(L, lua_upvalueindex(1)))

int MurmurationFieldGenerator::trampolinePrepare(lua_State* L) {
    MSK_SELF;
    if (self) self->prepare();
    return 0;
}

int MurmurationFieldGenerator::trampolineReset(lua_State* L) {
    MSK_SELF;
    if (self) self->reset();
    return 0;
}

int MurmurationFieldGenerator::trampolineStep(lua_State* L) {
    MSK_SELF;
    if (!self) return 0;
    self->step(static_cast<float>(luaL_optnumber(L, 1, 1.0 / 60.0)),
               static_cast<float>(luaL_optnumber(L, 2, 0.0)),
               static_cast<float>(luaL_optnumber(L, 3, 0.0)),
               static_cast<float>(luaL_optnumber(L, 4, 0.0)),
               static_cast<float>(luaL_optnumber(L, 5, 0.0)),
               lua_toboolean(L, 6) != 0,
               static_cast<float>(luaL_optnumber(L, 7, 0.45)),
               static_cast<float>(luaL_optnumber(L, 8, 0.35)),
               static_cast<float>(luaL_optnumber(L, 9, 0.55)),
               static_cast<float>(luaL_optnumber(L, 10, 0.58)),
               static_cast<float>(luaL_optnumber(L, 11, 0.45)));
    return 0;
}

int MurmurationFieldGenerator::trampolineDraw(lua_State* L) {
    MSK_SELF;
    if (!self) return 0;
    self->draw(static_cast<float>(luaL_optnumber(L, 1, 1280.0)),
               static_cast<float>(luaL_optnumber(L, 2, 720.0)),
               lua_toboolean(L, 3) != 0,
               lua_toboolean(L, 4) != 0,
               static_cast<int>(luaL_optinteger(L, 5, 12)));
    return 0;
}

int MurmurationFieldGenerator::trampolineIsReady(lua_State* L) {
    MSK_SELF;
    lua_pushboolean(L, self && self->initialized);
    return 1;
}

// Returns: birds, predatorActive, simAvgMs, simMaxMs, drawAvgMs, drawMaxMs.
// Max values reset on every call, so call it on a fixed interval.
int MurmurationFieldGenerator::trampolineStats(lua_State* L) {
    MSK_SELF;
    if (!self) return 0;
    lua_pushinteger(L, self->activeBirds);
    lua_pushboolean(L, self->predator.active);
    lua_pushnumber(L, self->simMsAvg);
    lua_pushnumber(L, self->simMsMax);
    lua_pushnumber(L, self->drawMsAvg);
    lua_pushnumber(L, self->drawMsMax);
    self->simMsMax = 0.0f;
    self->drawMsMax = 0.0f;
    return 6;
}

// Frees the VBO ring. Called from the mode's exit() so nothing stays on the
// GPU after switching modes; buffers are re-created lazily on the next draw.
int MurmurationFieldGenerator::trampolineRelease(lua_State* L) {
    MSK_SELF;
    if (self) self->releaseGpu();
    return 0;
}

#undef MSK_SELF

// ---------------------------------------------------------------- helpers

float MurmurationFieldGenerator::clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float MurmurationFieldGenerator::lerp(float a, float b, float t) { return a + (b - a) * t; }
float MurmurationFieldGenerator::smoothstep(float e0, float e1, float x) {
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
float MurmurationFieldGenerator::response(float dt, float rate) {
    return 1.0f - std::exp(-std::max(0.0f, dt) * std::max(0.0f, rate));
}
float MurmurationFieldGenerator::hueWrap(float h) {
    h = std::fmod(h, 1.0f);
    return h < 0.0f ? h + 1.0f : h;
}

uint32_t MurmurationFieldGenerator::nextRand() {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x ? x : 0x4649454Cu;
    return rngState;
}
float MurmurationFieldGenerator::rand01() { return (nextRand() & 0x00ffffffu) / 16777215.0f; }
float MurmurationFieldGenerator::randRange(float lo, float hi) { return lo + (hi - lo) * rand01(); }

ofFloatColor MurmurationFieldGenerator::hsv(float h, float s, float v, float a) {
    h = hueWrap(h);
    s = clamp01(s);
    v = clamp01(v);
    const float hh = h * 6.0f;
    const int sector = static_cast<int>(std::floor(hh)) % 6;
    const float f = hh - std::floor(hh);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    switch (sector) {
        case 0: return ofFloatColor(v, t, p, a);
        case 1: return ofFloatColor(q, v, p, a);
        case 2: return ofFloatColor(p, v, t, a);
        case 3: return ofFloatColor(p, q, v, a);
        case 4: return ofFloatColor(t, p, v, a);
        default: return ofFloatColor(v, p, q, a);
    }
}

ofFloatColor MurmurationFieldGenerator::mixColor(const ofFloatColor& a, const ofFloatColor& b, float t) {
    return ofFloatColor(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t));
}

int MurmurationFieldGenerator::bucketFor(int ix, int iy, int iz) const {
    const uint32_t h = (static_cast<uint32_t>(ix) * 73856093u)
                     ^ (static_cast<uint32_t>(iy) * 19349663u)
                     ^ (static_cast<uint32_t>(iz) * 83492791u);
    return static_cast<int>(h & (GRID_BUCKETS - 1));
}

// ---------------------------------------------------------------- lifecycle

void MurmurationFieldGenerator::prepare() {
    if (!initialized) reset();
}

void MurmurationFieldGenerator::reset() {
    rngState = 0x4649454Cu ^ static_cast<uint32_t>(ofGetElapsedTimeMillis() & 0xffffu);
    if (!rngState) rngState = 0x4649454Cu;
    elapsed = 0.0f;
    lowEnv = midEnv = highEnv = energyEnv = 0.0f;
    slowLow = midSlow = lowBreath = 0.0f;
    previousTrig = false;

    worldUp = Vec3(0.0f, std::cos(camPitch), std::sin(camPitch));
    horizFwd = Vec3(0.0f, -std::sin(camPitch), std::cos(camPitch));

    roost = roostTarget = Vec3(0.0f, 14.0f * FLOCK_DIST, 190.0f * FLOCK_DIST);
    roostTimer = 6.0f;
    splitPhase = randRange(0.0f, TWO_PI_F);
    for (int g = 0; g < GROUPS; ++g) {
        const float a = g * (TWO_PI_F / GROUPS) + randRange(-0.4f, 0.4f);
        const float r = randRange(40.0f, 54.0f);
        groupOffset[g] = Vec3(std::cos(a) * r, 0.0f, 0.0f) + horizFwd * (std::sin(a) * r * 0.6f)
                       + worldUp * randRange(-6.0f, 6.0f);
    }

    predator = Predator();
    predatorCooldown = 3.0f;
    waveCooldown = 4.0f;
    waveTimer = randRange(8.0f, 14.0f);

    activeBirdsF = lerp(350.0f, static_cast<float>(MAX_BIRDS), k3);
    activeBirds = static_cast<int>(activeBirdsF);

    seedBirds();
    treeSeed = nextRand();
    treesDirty = true;
    initialized = true;
    ofLogNotice("BRIDGE") << "murmfield_reset() MURMURATION FIELD v5.0: " << activeBirds
                          << " birds, Claude flock dynamics + HAUTLLE render field";
}

void MurmurationFieldGenerator::seedBirds() {
    const float heading = randRange(0.0f, TWO_PI_F);
    const Vec3 dir = Vec3(std::cos(heading), 0.0f, 0.0f) + horizFwd * std::sin(heading);
    for (int i = 0; i < MAX_BIRDS; ++i) {
        Bird& b = birds[i];
        const float a = randRange(0.0f, TWO_PI_F);
        const float r = 34.0f * std::sqrt(rand01());
        b.p = roost + Vec3(std::cos(a) * r, 0.0f, 0.0f) + horizFwd * (std::sin(a) * r)
            + worldUp * randRange(-5.0f, 5.0f);
        b.v = (dir + Vec3(randRange(-0.1f, 0.1f), randRange(-0.05f, 0.05f), randRange(-0.1f, 0.1f))) * 16.0f;
        b.flockAcc = Vec3();
        b.roll = 0.0f;
        b.flapPhase = randRange(0.0f, TWO_PI_F);
        b.flapRate = randRange(3.2f, 5.0f);
        b.flapAmp = 0.6f;
        b.flapTimer = randRange(0.3f, 2.0f);
        b.gliding = rand01() < 0.3f;
        b.panic = 0.0f;
        b.panicSide = 1.0f;
        b.radius = 4.0f;
        b.phase = randRange(0.0f, TWO_PI_F);
        b.speedBias = randRange(0.92f, 1.08f);
        b.group = i % GROUPS;
    }
}

// Forest canopy seen from above/beyond: one continuous top edge made of many
// small rounded crowns (each a cluster of dome lumps) plus occasional conifer
// spires. Heights are pixels above the bottom edge.
void MurmurationFieldGenerator::generateCanopy(std::array<float, TREE_COLS>& heights, float w, float h,
                                             float baseFrac, float edgeBoost, float crownMin,
                                             float crownMax, float liftMin, float liftMax,
                                             float coniferChance) {
    const float colW = w / static_cast<float>(TREE_COLS - 1);
    const float p1 = randRange(0.0f, TWO_PI_F);
    const float p2 = randRange(0.0f, TWO_PI_F);
    const float p3 = randRange(0.0f, TWO_PI_F);

    std::array<float, TREE_COLS> base;
    for (int c = 0; c < TREE_COLS; ++c) {
        const float u = c / static_cast<float>(TREE_COLS - 1);
        const float edge = std::fabs(u - 0.5f) * 2.0f;
        const float rolling = 0.012f * std::sin(u * TWO_PI_F * 1.3f + p1)
                            + 0.008f * std::sin(u * TWO_PI_F * 3.1f + p2)
                            + 0.004f * std::sin(u * TWO_PI_F * 7.7f + p3);
        base[c] = h * (baseFrac + rolling) * (1.0f + edgeBoost * edge * edge);
        heights[c] = base[c] - h * 0.006f;   // shallow valley floor between crowns
    }

    auto baseAt = [&](float x) {
        const int c = std::max(0, std::min(static_cast<int>(TREE_COLS) - 1,
                                           static_cast<int>(x / colW + 0.5f)));
        return base[c];
    };

    float x = -randRange(0.0f, crownMax * h);
    while (x < w + crownMax * h) {
        if (rand01() < coniferChance) {
            const float cw = h * randRange(crownMin * 0.55f, crownMin * 0.95f);
            const float spire = cw * randRange(1.7f, 2.8f);
            const float b = baseAt(x);
            const int c0 = std::max(0, static_cast<int>((x - cw * 0.5f) / colW));
            const int c1 = std::min(static_cast<int>(TREE_COLS) - 1, static_cast<int>((x + cw * 0.5f) / colW) + 1);
            for (int c = c0; c <= c1; ++c) {
                const float u = std::fabs(c * colW - x) / (cw * 0.5f);
                if (u >= 1.0f) continue;
                const float tiers = u * 4.0f - std::floor(u * 4.0f);   // tiered spruce edge
                const float v = b - h * 0.01f + spire * (1.0f - u) * (0.86f + 0.14f * tiers);
                heights[c] = std::max(heights[c], v);
            }
            x += cw * randRange(0.5f, 0.9f);
        } else {
            const float cw = h * randRange(crownMin, crownMax);
            const float b = baseAt(x);
            const int lumps = 5 + static_cast<int>(rand01() * 4.0f);
            const float crownLift = cw * randRange(liftMin, liftMax);
            for (int l = 0; l < lumps; ++l) {
                const float ox = x + randRange(-0.38f, 0.38f) * cw;
                const float r = cw * randRange(0.13f, 0.27f);
                // Lumps near the crown centre sit highest: a rounded, cauliflower top.
                const float off = std::fabs(ox - x) / (cw * 0.5f);
                const float top = b + crownLift * (1.0f - 0.65f * off * off) * randRange(0.80f, 1.05f);
                const int c0 = std::max(0, static_cast<int>((ox - r) / colW));
                const int c1 = std::min(static_cast<int>(TREE_COLS) - 1, static_cast<int>((ox + r) / colW) + 1);
                for (int c = c0; c <= c1; ++c) {
                    const float d = c * colW - ox;
                    if (std::fabs(d) >= r) continue;
                    const float v = top - r + std::sqrt(r * r - d * d);
                    heights[c] = std::max(heights[c], v);
                }
            }
            x += cw * randRange(0.45f, 0.75f);
        }
    }
    // Fine leaf texture on the edge.
    for (int c = 0; c < TREE_COLS; ++c) heights[c] += randRange(-0.6f, 0.6f);
}

void MurmurationFieldGenerator::buildTrees(float w, float h) {
    // Trees use their own seed so a resize rebuilds the same skyline.
    const uint32_t savedRng = rngState;
    rngState = treeSeed ? treeSeed : 0x54524545u;
    generateCanopy(canopyFar, w, h, 0.150f, 0.15f, 0.030f, 0.060f, 0.25f, 0.50f, 0.18f);
    generateCanopy(canopyNear, w, h, 0.100f, 0.30f, 0.055f, 0.115f, 0.28f, 0.60f, 0.12f);
    rngState = savedRng;

    // Both layers as one triangle strip (2 vertices per column), joined by a
    // degenerate pair so the canopy is a single cheap draw.
    canopyMesh.clear();
    canopyMesh.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    treeShade.clear();
    treeLayer.clear();
    const float colW = w / static_cast<float>(TREE_COLS - 1);
    auto add = [&](float x, float y, float shade, int layer) {
        canopyMesh.addVertex(glm::vec3(x, y, 0.0f));
        treeShade.push_back(shade);
        treeLayer.push_back(layer);
    };
    for (int layer = 0; layer < 2; ++layer) {
        const std::array<float, TREE_COLS>& hts = layer == 0 ? canopyFar : canopyNear;
        if (layer == 1) add(0.0f, h, 0.0f, 1);   // degenerate join (repeats next vertex)
        for (int c = 0; c < TREE_COLS; ++c) {
            const float x = c * colW;
            add(x, h, 0.0f, layer);
            add(x, h - hts[c], 1.0f, layer);
        }
        if (layer == 0) add(w, h - hts[TREE_COLS - 1], 1.0f, 0);   // degenerate join
    }
    canopyMesh.getColors().assign(canopyMesh.getVertices().size(), ofFloatColor(0.0f, 0.0f, 0.0f, 1.0f));
    treeBuiltW = w;
    treeBuiltH = h;
    treesDirty = false;
    treeColorK4 = treeColorK5 = -1.0f;
}

void MurmurationFieldGenerator::recolorTrees() {
    const ofFloatColor farTop = mixColor(treeFarCol, horizonCol, 0.12f);
    const ofFloatColor farBottom = mixColor(treeFarCol, treeNearCol, 0.35f);
    const ofFloatColor nearTop = mixColor(treeNearCol, horizonCol, 0.07f);
    const ofFloatColor nearBottom(treeNearCol.r * 0.55f, treeNearCol.g * 0.55f, treeNearCol.b * 0.55f, 1.0f);
    auto& cols = canopyMesh.getColors();
    cols.resize(treeShade.size());
    for (size_t i = 0; i < treeShade.size(); ++i) {
        const bool nearLayer = treeLayer[i] == 1;
        ofFloatColor c = treeShade[i] > 0.5f ? (nearLayer ? nearTop : farTop)
                                             : (nearLayer ? nearBottom : farBottom);
        c.a = canopyAlpha * (nearLayer ? 1.0f : 0.68f);
        cols[i] = c;
    }
    treeColorK4 = k4;
    treeColorK5 = k5;
}

// Nothing persistent lives on the GPU (the engine re-uploads mesh data each
// draw); this just drops CPU-side buffers when the mode exits.
void MurmurationFieldGenerator::releaseGpu() {
    frameMesh.clear();
}

void MurmurationFieldGenerator::pushVertex(float x, float y, const ofFloatColor& c) {
    frameMesh.getVertices().emplace_back(x, y, 0.0f);
    frameMesh.getColors().push_back(c);
}

// ---------------------------------------------------------------- simulation

void MurmurationFieldGenerator::step(float dt, float low, float mid, float high, float energy,
                                   bool trigFired, float knob1, float knob2, float knob3,
                                   float knob4, float knob5) {
    prepare();
    if (!initialized) return;
    const auto t0 = std::chrono::steady_clock::now();
    dt = std::max(0.0f, std::min(0.05f, dt));
    elapsed += dt;

    k1 = clamp01(knob1);
    k2 = clamp01(knob2);
    k3 = clamp01(knob3);
    k4 = clamp01(knob4);
    k5 = clamp01(knob5);

    low = clamp01(low);
    mid = clamp01(mid);
    high = clamp01(high);
    energy = clamp01(energy);
    lowEnv += (low - lowEnv) * response(dt, low > lowEnv ? 11.0f : 3.2f);
    midEnv += (mid - midEnv) * response(dt, mid > midEnv ? 8.0f : 3.0f);
    highEnv += (high - highEnv) * response(dt, high > highEnv ? 14.0f : 5.0f);
    energyEnv += (energy - energyEnv) * response(dt, energy > energyEnv ? 9.0f : 2.4f);
    slowLow += (lowEnv - slowLow) * response(dt, 1.15f);
    midSlow += (midEnv - midSlow) * response(dt, 0.55f);   // mood, not jolts
    lowBreath += (lowEnv - lowBreath) * response(dt, 0.8f);

    const bool trigEdge = trigFired && !previousTrig;
    previousTrig = trigFired;

    updateEvents(dt, trigEdge);
    updateRoost(dt);
    updatePopulation(dt);
    buildGrid();
    updateFlock(dt);
    updatePredator(dt);
    frameParity ^= 1;

    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    simMsAvg += (ms - simMsAvg) * 0.05f;
    simMsMax = std::max(simMsMax, ms);
}

void MurmurationFieldGenerator::updateEvents(float dt, bool trigEdge) {
    predatorCooldown = std::max(0.0f, predatorCooldown - dt);
    waveCooldown = std::max(0.0f, waveCooldown - dt);

    const float lowNovelty = std::max(0.0f, lowEnv - slowLow);
    const bool bassHit = lowNovelty > (0.085f + 0.04f * energyEnv);

    // Predator dives are the big gesture: strictly rate-limited.
    if ((trigEdge || bassHit) && predatorCooldown <= 0.0f && !predator.active) {
        spawnPredator();
        predatorCooldown = lerp(10.0f, 4.0f, k1) + randRange(0.0f, 2.0f);
        waveCooldown = std::max(waveCooldown, 3.5f);
    }

    // Spontaneous turning waves keep the flock alive in quiet passages.
    // Mids make them more frequent, but never closer than the cooldown.
    waveTimer -= dt * (0.6f + 1.2f * midSlow + 0.4f * k1);
    if (waveTimer <= 0.0f && waveCooldown <= 0.0f) {
        startWave();
        waveTimer = randRange(10.0f, 22.0f);
        waveCooldown = 4.0f;
    }
}

void MurmurationFieldGenerator::updateRoost(float dt) {
    roostTimer -= dt;
    if (roostTimer <= 0.0f) {
        roostTarget = Vec3(randRange(-38.0f, 38.0f) * FLOCK_DIST,
                           randRange(4.0f, 24.0f) * FLOCK_DIST,
                           randRange(175.0f, 225.0f) * FLOCK_DIST);
        roostTimer = randRange(7.0f, 15.0f) / (0.6f + 0.8f * k1);
    }
    const float follow = response(dt, 0.10f + 0.14f * k1);
    roost += (roostTarget - roost) * follow;

    splitPhase += dt * (0.09f + 0.06f * k1);
    const float breath = 0.5f + 0.5f * std::sin(splitPhase);
    splitAmount = std::pow(k2, 1.4f) * (0.30f + 0.70f * breath);

    const float popFrac = clamp01((activeBirdsF - 350.0f) / (MAX_BIRDS - 350.0f));
    flockRadius = lerp(24.0f, 46.0f, std::sqrt(popFrac)) * (1.0f + 0.10f * lowBreath);
}

void MurmurationFieldGenerator::updatePopulation(float dt) {
    const float target = lerp(350.0f, static_cast<float>(MAX_BIRDS), k3);
    const float maxStep = 500.0f * dt;
    activeBirdsF += std::max(-maxStep, std::min(maxStep, target - activeBirdsF));
    const int next = std::max(16, std::min(static_cast<int>(MAX_BIRDS), static_cast<int>(activeBirdsF)));

    // Newly activated birds join next to an existing bird of their own group.
    for (int i = activeBirds; i < next; ++i) {
        Bird& b = birds[i];
        int j = static_cast<int>(rand01() * (activeBirds - 1));
        j = (j / GROUPS) * GROUPS + b.group;
        if (j >= activeBirds) j = std::max(0, j - GROUPS);
        const Bird& src = birds[j];
        b.p = src.p + Vec3(randRange(-3.5f, 3.5f), randRange(-1.5f, 1.5f), randRange(-3.5f, 3.5f));
        b.v = src.v;
        b.flockAcc = Vec3();
        b.roll = src.roll;
        b.panic = 0.0f;
        b.radius = 4.0f;
    }
    activeBirds = next;
}

void MurmurationFieldGenerator::buildGrid() {
    gridHead.fill(-1);
    for (int i = 0; i < activeBirds; ++i) {
        const Vec3& p = birds[i].p;
        const int b = bucketFor(static_cast<int>(std::floor(p.x / GRID_CELL)),
                                static_cast<int>(std::floor(p.y / GRID_CELL)),
                                static_cast<int>(std::floor(p.z / GRID_CELL)));
        gridNext[i] = gridHead[b];
        gridHead[b] = i;
    }
}

void MurmurationFieldGenerator::computeNeighborForces(int index, float dtEff) {
    Bird& b = birds[index];
    const float r2 = b.radius * b.radius;
    const float crossGroup = lerp(1.0f, 0.12f, k2);
    const float wAlign = 3.0f;
    const float wCohere = lerp(0.55f, 0.30f, k2);
    const float wSeparate = 40.0f;

    const int ix = static_cast<int>(std::floor(b.p.x / GRID_CELL));
    const int iy = static_cast<int>(std::floor(b.p.y / GRID_CELL));
    const int iz = static_cast<int>(std::floor(b.p.z / GRID_CELL));

    int visited[27];
    int visitedCount = 0;
    int checks = 0;
    int count = 0;
    float weightSum = 0.0f;
    Vec3 sumV, sumP, sep;
    float posWeight = 0.0f;
    float maxPanic = 0.0f;
    float maxPanicSide = b.panicSide;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int bucket = bucketFor(ix + dx, iy + dy, iz + dz);
                bool seen = false;
                for (int k = 0; k < visitedCount; ++k) {
                    if (visited[k] == bucket) { seen = true; break; }
                }
                if (seen) continue;
                visited[visitedCount++] = bucket;

                for (int j = gridHead[bucket]; j >= 0 && checks < MAX_NEIGHBOR_CHECKS; j = gridNext[j]) {
                    if (j == index) continue;
                    ++checks;
                    const Bird& n = birds[j];
                    const Vec3 d = n.p - b.p;
                    const float d2 = mfld::dot(d, d);
                    if (d2 > r2 || d2 < 1.0e-6f) continue;
                    ++count;
                    const float w = (n.group == b.group) ? 1.0f : crossGroup;
                    sumV += n.v * w;
                    weightSum += w;
                    sumP += n.p * w;
                    posWeight += w;
                    if (d2 < SEP_RADIUS * SEP_RADIUS) {
                        const float dd = std::sqrt(d2);
                        sep -= d * ((1.0f - dd / SEP_RADIUS) / dd);
                    }
                    if (n.panic > maxPanic) {
                        maxPanic = n.panic;
                        maxPanicSide = n.panicSide;
                    }
                }
            }
        }
    }

    Vec3 acc;
    if (count > 0) {
        const Vec3 avgV = sumV * (1.0f / weightSum);
        const Vec3 centroid = sumP * (1.0f / posWeight);
        acc += (avgV - b.v) * wAlign;
        acc += (centroid - b.p) * wCohere;
    }
    acc += sep * wSeparate;
    b.flockAcc = acc;

    // Adaptive (topological) radius: sparse areas reach further, dense areas less.
    b.radius += dtEff * 1.6f * (TOPO_NEIGHBORS - static_cast<float>(count));
    b.radius = std::max(2.5f, std::min(GRID_CELL, b.radius));

    // Panic (escape-wave) propagation: each hop is slightly weaker and slightly
    // delayed, so a wave visibly travels across the flock.
    const float targetPanic = maxPanic * 0.86f;
    if (targetPanic > b.panic + 0.02f) {
        b.panic += (targetPanic - b.panic) * response(dtEff, 8.0f);
        b.panicSide = maxPanicSide;
    }
}

void MurmurationFieldGenerator::updateFlock(float dt) {
    const float cruise = lerp(13.0f, 26.0f, k1) * (1.0f + 0.15f * midSlow);
    const float maxAcc = lerp(20.0f, 42.0f, k1);
    const float noiseW = 1.2f + 3.0f * midSlow;
    const float fleeRadius = 16.0f;
    const float tanHalf = std::tan(FOV_Y * 0.5f);
    const float limX = 0.78f * tanHalf * (16.0f / 9.0f);
    const float limTop = 0.82f * tanHalf;
    const float limBottom = -0.42f * tanHalf;

    // Per-frame constants (avoid per-bird exp()).
    const float panicDecay = std::exp(-dt * 1.3f);
    const float flapResp = response(dt, 5.0f);
    const float flapSpeed = dt * TWO_PI_F * (0.85f + 0.3f * k1);

    Vec3 centroidSum;
    for (int i = 0; i < activeBirds; ++i) {
        Bird& b = birds[i];
        if ((i & 1) == frameParity) computeNeighborForces(i, dt * 2.0f);

        const float speed = mfld::length(b.v);
        const Vec3 fwd = mfld::normalizeOr(b.v, horizFwd);
        const Vec3 up0 = mfld::normalizeOr(worldUp - fwd * mfld::dot(fwd, worldUp), worldUp);
        const Vec3 right = mfld::cross(fwd, up0);

        Vec3 acc = b.flockAcc;

        // Group roost: horizontal pull that ramps up past the flock radius.
        // Birds at the edge turn back first, which folds the whole flock.
        const Vec3 target = roost + groupOffset[b.group] * splitAmount;
        const Vec3 toTarget = target - b.p;
        const float vertical = std::max(-25.0f, std::min(25.0f, mfld::dot(toTarget, worldUp)));
        const Vec3 horizontal = toTarget - worldUp * mfld::dot(toTarget, worldUp);
        const float hDist = mfld::length(horizontal);
        const float pull = 0.6f + 10.0f * smoothstep(flockRadius * 0.4f, flockRadius * 1.3f, hDist);
        acc += mfld::normalizeOr(horizontal, Vec3()) * pull;

        // Flight level: damp climbs/dives and spring back toward roost altitude.
        // This keeps the flock sheet-like, so it thickens/thins as it turns.
        acc += worldUp * (vertical * 0.35f - mfld::dot(b.v, worldUp) * 1.6f);

        // Keep the flock framed above the tree line.
        const float sx = b.p.x / b.p.z;
        const float sy = b.p.y / b.p.z;
        if (sx > limX) acc.x -= (sx - limX) * b.p.z * 4.0f;
        if (sx < -limX) acc.x += (-limX - sx) * b.p.z * 4.0f;
        if (sy > limTop) acc.y -= (sy - limTop) * b.p.z * 4.0f;
        if (sy < limBottom) acc.y += (limBottom - sy) * b.p.z * 4.0f;
        const float zNear = 115.0f * FLOCK_DIST;
        const float zFar = 290.0f * FLOCK_DIST;
        if (b.p.z < zNear) acc.z += (zNear - b.p.z) * 0.8f;
        if (b.p.z > zFar) acc.z -= (b.p.z - zFar) * 0.8f;

        // Cruise speed regulation.
        const float targetSpeed = cruise * b.speedBias * (1.0f + 0.30f * b.panic);
        acc += fwd * ((targetSpeed - speed) * 1.8f);

        // Gentle individual wander.
        acc += Vec3(std::sin(elapsed * 0.9f + b.phase * 3.1f),
                    std::sin(elapsed * 0.7f + b.phase * 1.7f) * 0.4f,
                    std::cos(elapsed * 0.8f + b.phase * 2.3f)) * noiseW;

        // Predator: flee and seed panic.
        if (predator.active) {
            const Vec3 away = b.p - predator.p;
            const float d = mfld::length(away);
            if (d < fleeRadius && d > 0.001f) {
                const float w = 1.0f - d / fleeRadius;
                const Vec3 dir = away * (1.0f / d);
                acc += dir * (55.0f * w);
                b.panic = std::max(b.panic, 0.6f + 0.4f * w);
                b.panicSide = mfld::dot(dir, right) >= 0.0f ? 1.0f : -1.0f;
            }
        }

        // Panic: hard lateral turn in the stored direction.
        acc += right * (b.panicSide * 20.0f * b.panic);

        const float accMag = mfld::length(acc);
        const float accCap = maxAcc * (1.0f + 0.6f * b.panic);
        if (accMag > accCap) acc = acc * (accCap / accMag);

        b.v += acc * dt;
        const float newSpeed = mfld::length(b.v);
        const float lo = cruise * 0.55f;
        const float hi = cruise * 1.55f;
        if (newSpeed < lo && newSpeed > 1.0e-4f) b.v = b.v * (lo / newSpeed);
        else if (newSpeed > hi) b.v = b.v * (hi / newSpeed);
        else if (newSpeed <= 1.0e-4f) b.v = horizFwd * lo;
        b.p += b.v * dt;

        if (b.p.z < 40.0f * FLOCK_DIST) { b.p.z = 40.0f * FLOCK_DIST; b.v.z = std::fabs(b.v.z); }
        if (b.p.z > 420.0f * FLOCK_DIST) { b.p.z = 420.0f * FLOCK_DIST; b.v.z = -std::fabs(b.v.z); }

        // Bank into the turn; panic adds a hard roll, which flips which
        // wing surface faces the viewer and produces the dark ripple.
        const float aLat = mfld::dot(acc, right);
        float rollTarget = std::atan(aLat / 14.0f)
                         + b.panicSide * b.panic * 0.9f
                         + highEnv * 0.12f * std::sin(elapsed * 11.0f + b.phase * 7.0f);
        rollTarget = std::max(-1.35f, std::min(1.35f, rollTarget));
        b.roll += (rollTarget - b.roll) * std::min(1.0f, dt * (6.0f + 6.0f * b.panic));

        // Flap / glide bouts.
        b.flapTimer -= dt;
        if (b.flapTimer <= 0.0f) {
            b.gliding = !b.gliding && b.panic < 0.3f;
            b.flapTimer = b.gliding ? randRange(0.4f, 1.4f) : randRange(0.6f, 2.2f);
        }
        const float ampTarget = b.gliding ? 0.10f : (0.62f + 0.25f * b.panic);
        b.flapAmp += (ampTarget - b.flapAmp) * flapResp;
        b.flapPhase += flapSpeed * b.flapRate * (1.0f + 0.6f * b.panic);
        if (b.flapPhase > TWO_PI_F) b.flapPhase -= TWO_PI_F;

        b.panic *= panicDecay;
        centroidSum += b.p;
    }
    if (activeBirds > 0) flockCentroid = centroidSum * (1.0f / activeBirds);
}

void MurmurationFieldGenerator::spawnPredator() {
    const float a = randRange(0.0f, TWO_PI_F);
    const Vec3 h = Vec3(std::cos(a), 0.0f, 0.0f) + horizFwd * std::sin(a);
    const Vec3 start = flockCentroid + h * (flockRadius * 0.6f + 18.0f) + worldUp * 10.0f;
    const Vec3 aim = flockCentroid + Vec3(randRange(-6.0f, 6.0f), randRange(-3.0f, 3.0f), randRange(-6.0f, 6.0f));
    predator.active = true;
    predator.p = start;
    predator.v = (aim - start) * (1.0f / 0.9f);   // reaches the flock edge ~0.3 s after the hit
    predator.life = 2.2f;
}

void MurmurationFieldGenerator::startWave() {
    if (activeBirds < 2) return;
    // Seed from a bird near the flock's edge so the wave sweeps across it.
    int best = 0;
    float bestD = -1.0f;
    for (int t = 0; t < 12; ++t) {
        const int i = static_cast<int>(rand01() * (activeBirds - 1));
        const float d = mfld::length(birds[i].p - flockCentroid);
        if (d > bestD) { bestD = d; best = i; }
    }
    birds[best].panic = 0.8f;
    birds[best].panicSide = rand01() < 0.5f ? -1.0f : 1.0f;
}

void MurmurationFieldGenerator::updatePredator(float dt) {
    if (!predator.active) return;
    predator.p += predator.v * dt;
    predator.life -= dt;
    if (predator.life <= 0.0f) predator.active = false;
}

// ---------------------------------------------------------------- drawing

void MurmurationFieldGenerator::computePalette() {
    // K5 is FIELD, not "time of day".  The middle of the knob allows the
    // current habitat to resolve; either side pushes the observation system
    // away from literal landscape imagery.
    const float field = k5;
    fieldSignal = 1.0f - smoothstep(0.18f, 0.48f, field);
    fieldHabitat = smoothstep(0.18f, 0.58f, field);
    fieldUnstable = smoothstep(0.78f, 1.0f, field);

    // K4 sets the palette centre, but it never quite locks.  Two very slow,
    // incommensurate oscillators give the ~45 s chromatic breathing the old
    // HAUTLLE modes had without making audio steer the colour directly.
    const float drift = 0.034f * std::sin(elapsed * (TWO_PI_F / 45.0f))
                      + 0.011f * std::sin(elapsed * (TWO_PI_F / 71.0f) + 1.73f);
    paletteHue = hueWrap(k4 + drift);

    const ofFloatColor sigZen = hsv(paletteHue + 0.02f, 0.52f, 0.105f);
    const ofFloatColor sigMid = hsv(paletteHue - 0.07f, 0.58f, 0.075f);
    const ofFloatColor sigHor = hsv(paletteHue + 0.12f, 0.42f, 0.145f);

    const ofFloatColor habZen = hsv(paletteHue,         0.38f, 0.55f);
    const ofFloatColor habMid = hsv(paletteHue - 0.03f, 0.31f, 0.62f);
    const ofFloatColor habHor = hsv(paletteHue - 0.075f,0.25f, 0.72f);

    const ofFloatColor badZen = hsv(paletteHue + 0.18f, 0.70f, 0.105f);
    const ofFloatColor badMid = hsv(paletteHue - 0.16f, 0.64f, 0.165f);
    const ofFloatColor badHor = hsv(paletteHue + 0.31f, 0.56f, 0.205f);

    for (int i = 0; i < SKY_LUT; ++i) {
        const float u = i / static_cast<float>(SKY_LUT - 1);
        const float c = std::pow(u, 1.45f);
        const ofFloatColor signal = c < 0.5f
            ? mixColor(sigZen, sigMid, c * 2.0f)
            : mixColor(sigMid, sigHor, (c - 0.5f) * 2.0f);
        const ofFloatColor habitat = c < 0.5f
            ? mixColor(habZen, habMid, c * 2.0f)
            : mixColor(habMid, habHor, (c - 0.5f) * 2.0f);
        const ofFloatColor unstable = c < 0.5f
            ? mixColor(badZen, badMid, c * 2.0f)
            : mixColor(badMid, badHor, (c - 0.5f) * 2.0f);

        ofFloatColor col = mixColor(signal, habitat, fieldHabitat);
        col = mixColor(col, unstable, fieldUnstable * 0.72f);

        // Very low-amplitude horizontal band contamination at the unstable end.
        const float band = 0.5f + 0.5f * std::sin(u * 37.0f + elapsed * 0.18f);
        const ofFloatColor bandCol = hsv(paletteHue + 0.22f, 0.62f, 0.24f);
        col = mixColor(col, bandCol, fieldUnstable * band * 0.075f);
        skyLut[i] = col;
    }
    horizonCol = skyLut[SKY_LUT - 1];

    // Signal end: birds are bright graphic marks. Habitat: dark tops / light
    // undersides read naturally. Unstable end: contrast comes back with a
    // slightly synthetic colour split.
    float darkV = lerp(0.70f, 0.095f, fieldHabitat);
    darkV = lerp(darkV, 0.34f, fieldUnstable);
    float lightV = lerp(0.92f, 0.72f, fieldHabitat);
    lightV = lerp(lightV, 0.80f, fieldUnstable);
    birdDark = hsv(paletteHue + 0.47f, lerp(0.64f, 0.48f, fieldHabitat), darkV);
    birdLight = hsv(paletteHue + 0.57f, lerp(0.72f, 0.54f, fieldHabitat), lightV);

    // Keep the canopy inside the palette rather than permanently "tree green."
    treeNearCol = hsv(paletteHue + 0.29f, 0.44f, lerp(0.10f, 0.055f, fieldHabitat));
    treeFarCol = mixColor(hsv(paletteHue + 0.24f, 0.35f, 0.13f), horizonCol, 0.34f);

    // Forest only fully resolves through the middle of FIELD.  At the signal
    // end it is almost a threshold line; at the unstable end it dissolves again.
    canopyAlpha = (0.020f + 0.74f * fieldHabitat) * (1.0f - 0.60f * fieldUnstable);
    hazeAmount = lerp(0.055f, 0.36f, fieldHabitat);
    hazeAmount = lerp(hazeAmount, 0.13f, fieldUnstable);
}

ofFloatColor MurmurationFieldGenerator::skyAt(float u) const {
    const int i = std::max(0, std::min(static_cast<int>(SKY_LUT) - 1,
                                       static_cast<int>(u * (SKY_LUT - 1) + 0.5f)));
    return skyLut[i];
}

void MurmurationFieldGenerator::draw(float width, float height, bool persistOn, bool persistClear,
                                     int persistFade) {
    prepare();
    if (!initialized) return;
    const auto t0 = std::chrono::steady_clock::now();
    const float w = std::max(1.0f, width);
    const float h = std::max(1.0f, height);
    const float focal = (h * 0.5f) / std::tan(FOV_Y * 0.5f);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    computePalette();
    if (treesDirty || treeBuiltW != w || treeBuiltH != h) buildTrees(w, h);
    if (std::fabs(treeColorK4 - k4) > 1.0e-4f || std::fabs(treeColorK5 - k5) > 1.0e-4f) recolorTrees();

    frameMesh.setMode(OF_PRIMITIVE_TRIANGLES);
    frameMesh.getVertices().clear();
    frameMesh.getColors().clear();

    // --- field background (translucent wash when Persist is on)
    const float skyAlpha = (persistOn && !persistClear)
        ? std::max(2, std::min(255, persistFade)) / 255.0f
        : 1.0f;
    for (int r = 0; r < SKY_ROWS; ++r) {
        const float u0 = r / static_cast<float>(SKY_ROWS);
        const float u1 = (r + 1) / static_cast<float>(SKY_ROWS);
        ofFloatColor c0 = skyAt(u0);
        ofFloatColor c1 = skyAt(u1);
        c0.a = c1.a = skyAlpha;
        const float y0 = u0 * h;
        const float y1 = u1 * h;
        pushVertex(0.0f, y0, c0); pushVertex(w, y0, c0); pushVertex(w, y1, c1);
        pushVertex(0.0f, y0, c0); pushVertex(w, y1, c1); pushVertex(0.0f, y1, c1);
    }

    // --- signal field
    // Fixed hash positions keep this layer spatially coherent.  It never touches
    // the flock RNG, so rendering cannot accidentally alter the movement model.
    const int markCount = static_cast<int>(lerp(118.0f, 36.0f, fieldHabitat)
                                           + 54.0f * fieldUnstable);
    for (int i = 0; i < markCount; ++i) {
        const uint32_t seed = 0x9e3779b9u * static_cast<uint32_t>(i + 1);
        const float hx = fieldHash01(seed ^ 0x68bc21ebu);
        const float hy = fieldHash01(seed ^ 0x02e5be93u);
        const float hs = fieldHash01(seed ^ 0xa511e9b3u);
        const float hp = fieldHash01(seed ^ 0x63d83595u);
        const float hh = fieldHash01(seed ^ 0x91e10da5u);

        float x = hx * w;
        float y = (0.035f + hy * 0.82f) * h;
        x += std::sin(elapsed * (0.035f + hp * 0.045f) + hp * TWO_PI_F) * w * 0.008f * fieldSignal;
        x += std::sin(elapsed * 0.73f + hp * 19.0f) * w * 0.010f * fieldUnstable;
        y += std::cos(elapsed * 0.047f + hp * 13.0f) * h * 0.004f * fieldSignal;

        const float sz = (0.55f + hs * 1.85f) * lerp(1.0f, 0.58f, fieldHabitat);
        float a = (0.10f + 0.30f * fieldSignal + 0.14f * fieldUnstable)
                * (0.55f + 0.45f * hp);
        if (fieldHabitat > 0.8f && fieldUnstable < 0.05f) a *= 0.62f;

        const float hueOffset = (i % 3 == 0) ? 0.17f : ((i % 3 == 1) ? 0.54f : -0.08f);
        ofFloatColor c = hsv(paletteHue + hueOffset,
                             0.52f + 0.33f * fieldSignal,
                             0.45f + 0.42f * hh,
                             clamp01(a));

        if ((i & 3) == 0) {
            // Tiny diamond: reads as a glyph / sensor return rather than a star.
            pushVertex(x, y - sz, c); pushVertex(x + sz * 0.7f, y, c); pushVertex(x, y + sz, c);
            pushVertex(x, y - sz, c); pushVertex(x, y + sz, c); pushVertex(x - sz * 0.7f, y, c);
        } else {
            // Directionless triangular fleck.
            const float skew = (hp - 0.5f) * sz * 0.8f;
            pushVertex(x - sz, y + sz * 0.35f, c);
            pushVertex(x + sz, y + skew, c);
            pushVertex(x - sz * 0.25f, y - sz * 0.7f, c);
        }
    }

    // --- birds
    for (int i = 0; i < activeBirds; ++i) {
        const Bird& b = birds[i];
        if (b.p.z < 5.0f) continue;

        const Vec3 fwd = mfld::normalizeOr(b.v, horizFwd);
        const Vec3 up0 = mfld::normalizeOr(worldUp - fwd * mfld::dot(fwd, worldUp), worldUp);
        const Vec3 right0 = mfld::cross(fwd, up0);
        const float cr = std::cos(b.roll);
        const float sr = std::sin(b.roll);
        const Vec3 upB = up0 * cr + right0 * sr;
        const Vec3 rightB = right0 * cr - up0 * sr;

        const Vec3 nose = b.p + fwd * (BODY_LEN * 0.5f);
        const Vec3 tail = b.p - fwd * (BODY_LEN * 0.5f);

        auto project = [&](const Vec3& q, float& sx, float& sy) {
            const float z = std::max(1.0f, q.z);
            sx = cx + focal * q.x / z;
            sy = cy - focal * q.y / z;
        };

        float nx, ny, tx, ty;
        project(nose, nx, ny);
        project(tail, tx, ty);
        if ((nx < -20.0f && tx < -20.0f) || (nx > w + 20.0f && tx > w + 20.0f) ||
            (ny < -20.0f && ty < -20.0f) || (ny > h + 20.0f && ty > h + 20.0f)) continue;

        float hx = tx - nx;
        float hy = ty - ny;
        float hl = std::sqrt(hx * hx + hy * hy);
        if (hl < MIN_HINGE_PX) {
            const float mx = (nx + tx) * 0.5f;
            const float my = (ny + ty) * 0.5f;
            if (hl < 1.0e-4f) { hx = 1.0f; hy = 0.0f; hl = 1.0f; }
            const float s = (MIN_HINGE_PX * 0.5f) / hl;
            nx = mx - hx * s; ny = my - hy * s;
            tx = mx + hx * s; ty = my + hy * s;
            hx = tx - nx; hy = ty - ny; hl = MIN_HINGE_PX;
        }

        const float apparentPx = focal * BODY_LEN / std::max(1.0f, b.p.z);
        const float fog = clamp01((b.p.z - 130.0f * FLOCK_DIST) / (240.0f * FLOCK_DIST)) * hazeAmount;
        const ofFloatColor fogCol = skyAt(clamp01((ny + ty) * 0.5f / h));

        // Far birds are marks first, organisms second.  They only resolve into
        // articulated wing geometry by physically approaching the camera.
        if (apparentPx < 7.25f) {
            const float mx = (nx + tx) * 0.5f;
            const float my = (ny + ty) * 0.5f;
            const float ux = (nx - tx) / hl;
            const float uy = (ny - ty) / hl;
            const float pxv = -uy;
            const float pyv = ux;
            const float len = std::max(1.7f, std::min(4.2f, apparentPx * 0.52f));
            const float wid = std::max(0.65f, std::min(1.55f, apparentPx * 0.18f));

            ofFloatColor c = mixColor(birdDark, birdLight,
                                      0.20f + 0.30f * (0.5f + 0.5f * std::sin(b.flapPhase)));
            c = mixColor(c, fogCol, fog);
            c.a = lerp(0.82f, 1.0f, fieldHabitat);

            const float fx = mx + ux * len * 0.60f;
            const float fy = my + uy * len * 0.60f;
            const float bx = mx - ux * len * 0.45f;
            const float by = my - uy * len * 0.45f;
            pushVertex(fx, fy, c);
            pushVertex(bx + pxv * wid, by + pyv * wid, c);
            pushVertex(bx - pxv * wid, by - pyv * wid, c);

            if (fieldUnstable > 0.01f && (i % 11) == 0) {
                const float pulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(elapsed * 1.7f + b.phase * 7.0f));
                const float off = fieldUnstable * pulse * 5.0f;
                ofFloatColor g = hsv(paletteHue + ((i & 1) ? 0.19f : -0.21f), 0.78f, 0.74f,
                                     0.08f + 0.16f * fieldUnstable);
                pushVertex(fx + pxv * off, fy + pyv * off, g);
                pushVertex(bx + pxv * (wid + off), by + pyv * (wid + off), g);
                pushVertex(bx + pxv * (off - wid), by + pyv * (off - wid), g);
            }
            continue;
        }

        const Vec3 toCam = mfld::normalizeOr(b.p * -1.0f, Vec3(0.0f, 0.0f, -1.0f));
        const float dihedral = 0.12f + b.flapAmp * std::sin(b.flapPhase);
        const float ca = std::cos(dihedral);
        const float sa = std::sin(dihedral);
        const float resolve = smoothstep(7.25f, 11.0f, apparentPx);
        const float wingScale = lerp(0.58f, 1.0f, resolve);

        for (int side = -1; side <= 1; side += 2) {
            const float ss = static_cast<float>(side);
            const Vec3 out = rightB * (ss * ca) + upB * sa;
            const Vec3 tip = b.p + fwd * (BODY_LEN * WING_SWEEP)
                           + out * (BODY_LEN * WING_SPAN * wingScale);
            const Vec3 topNormal = upB * ca - rightB * (ss * sa);

            float wx, wy;
            project(tip, wx, wy);

            // Keep a sliver of wing visible when it is seen edge-on.
            const float minWing = lerp(0.52f, MIN_WING_PX, resolve);
            const float dist = (hx * (wy - ny) - hy * (wx - nx)) / hl;
            if (std::fabs(dist) < minWing) {
                const float sign = dist >= 0.0f ? 1.0f : -1.0f;
                const float push = (minWing * sign - dist) / hl;
                wx += -hy * push;
                wy += hx * push;
            }

            const float facing = mfld::dot(topNormal, toCam);
            const float light = 0.72f + 0.28f * std::fabs(facing);
            ofFloatColor surface = facing > 0.0f ? birdDark : birdLight;
            ofFloatColor ambiguous = mixColor(birdDark, birdLight,
                                              0.26f + 0.16f * std::fabs(std::sin(b.flapPhase)));
            ofFloatColor c = mixColor(ambiguous, surface, resolve);
            c.r *= light; c.g *= light; c.b *= light;
            c = mixColor(c, fogCol, fog);
            c.a = 1.0f;

            pushVertex(nx, ny, c);
            pushVertex(tx, ty, c);
            pushVertex(wx, wy, c);

            // At the far end of FIELD the flock is still moving naturally;
            // only the observation tears.  Sparse chromatic echoes follow hard
            // banks, with no force fed back into the birds.
            if (fieldUnstable > 0.01f && (i % 9) == 0 && std::fabs(b.roll) > 0.38f) {
                const float pulse = 0.25f + 0.75f * (0.5f + 0.5f * std::sin(elapsed * 1.35f + b.phase * 5.3f));
                const float sign = side < 0 ? -1.0f : 1.0f;
                const float off = sign * fieldUnstable * pulse * (2.0f + 2.5f * std::fabs(b.roll));
                const float ox = (-hy / hl) * off;
                const float oy = ( hx / hl) * off;
                ofFloatColor g = hsv(paletteHue + (side < 0 ? 0.18f : -0.20f), 0.78f, 0.78f,
                                     0.055f + 0.15f * fieldUnstable);
                pushVertex(nx + ox, ny + oy, g);
                pushVertex(tx + ox, ty + oy, g);
                pushVertex(wx + ox, wy + oy, g);
            }
        }
    }

    ofPushStyle();
    ofEnableAlphaBlending();
    ofDisableDepthTest();
    ofFill();
    ofSetColor(255);
    frameMesh.draw();
    canopyMesh.draw();
    ofPopStyle();

    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    drawMsAvg += (ms - drawMsAvg) * 0.05f;
    drawMsMax = std::max(drawMsMax, ms);
}
