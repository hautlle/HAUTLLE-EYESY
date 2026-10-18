#pragma once

// HAUTLLE_MURMURATION_05_FIELD
// Claude's naturalistic 2.5D flocking is intentionally preserved.
// HAUTLLE character lives in the rendering layer: distance abstraction,
// slow palette drift, signal-field marks, thresholded habitat, and restrained
// render-only contamination at the unstable end of K5.

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mfld {
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    Vec3() = default;
    Vec3(float ax, float ay, float az) : x(ax), y(ay), z(az) {}
};
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(const Vec3& a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalizeOr(const Vec3& a, const Vec3& fallback) {
    const float l2 = dot(a, a);
    if (l2 < 1.0e-10f) return fallback;
    return a * (1.0f / std::sqrt(l2));
}
} // namespace mfld

class MurmurationFieldGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

private:
    enum {
        MAX_BIRDS = 1600,
        GROUPS = 4,
        GRID_BUCKETS = 4096,
        SKY_ROWS = 24,
        TREE_COLS = 641,
        SKY_LUT = 64
    };

    struct Bird {
        mfld::Vec3 p;
        mfld::Vec3 v;
        mfld::Vec3 flockAcc;
        float roll = 0.0f;
        float flapPhase = 0.0f;
        float flapRate = 4.0f;
        float flapAmp = 0.6f;
        float flapTimer = 1.0f;
        bool gliding = false;
        float panic = 0.0f;
        float panicSide = 1.0f;
        float radius = 4.5f;
        float phase = 0.0f;
        float speedBias = 1.0f;
        int group = 0;
    };

    struct Predator {
        bool active = false;
        mfld::Vec3 p;
        mfld::Vec3 v;
        float life = 0.0f;
    };

    static int trampolinePrepare(lua_State* L);
    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineIsReady(lua_State* L);
    static int trampolineStats(lua_State* L);
    static int trampolineRelease(lua_State* L);

    void prepare();
    void step(float dt, float low, float mid, float high, float energy, bool trigFired,
              float knob1, float knob2, float knob3, float knob4, float knob5);
    void draw(float width, float height, bool persistOn, bool persistClear, int persistFade);

    void seedBirds();
    void generateCanopy(std::array<float, TREE_COLS>& heights, float w, float h,
                        float baseFrac, float edgeBoost, float crownMin, float crownMax,
                        float liftMin, float liftMax, float coniferChance);
    void buildTrees(float w, float h);
    void recolorTrees();
    void releaseGpu();
    void pushVertex(float x, float y, const ofFloatColor& c);
    void updateEvents(float dt, bool trigEdge);
    void updateRoost(float dt);
    void updatePopulation(float dt);
    void buildGrid();
    void computeNeighborForces(int index, float dtEff);
    void updateFlock(float dt);
    void updatePredator(float dt);
    void spawnPredator();
    void startWave();

    void computePalette();
    ofFloatColor skyAt(float u) const;

    int bucketFor(int ix, int iy, int iz) const;
    static ofFloatColor hsv(float h, float s, float v, float a = 1.0f);
    static ofFloatColor mixColor(const ofFloatColor& a, const ofFloatColor& b, float t);
    static float clamp01(float v);
    static float lerp(float a, float b, float t);
    static float smoothstep(float e0, float e1, float x);
    static float response(float dt, float rate);
    static float hueWrap(float h);

    uint32_t nextRand();
    float rand01();
    float randRange(float lo, float hi);

    std::array<Bird, MAX_BIRDS> birds;
    std::array<int, GRID_BUCKETS> gridHead;
    std::array<int, MAX_BIRDS> gridNext;
    int activeBirds = 900;
    float activeBirdsF = 900.0f;
    int frameParity = 0;
    bool initialized = false;
    uint32_t rngState = 0x4649454Cu; // FIEL
    float elapsed = 0.0f;

    float k1 = 0.45f, k2 = 0.35f, k3 = 0.55f, k4 = 0.58f, k5 = 0.45f;

    float lowEnv = 0.0f, midEnv = 0.0f, highEnv = 0.0f, energyEnv = 0.0f;
    float slowLow = 0.0f, midSlow = 0.0f, lowBreath = 0.0f;
    bool previousTrig = false;

    // Camera looks up over the tree line. Simulation runs in camera space:
    // +x right, +y up on screen, +z away from viewer.
    float camPitch = 0.49f;
    mfld::Vec3 worldUp;
    mfld::Vec3 horizFwd;

    mfld::Vec3 roost;
    mfld::Vec3 roostTarget;
    float roostTimer = 0.0f;
    std::array<mfld::Vec3, GROUPS> groupOffset;
    float splitPhase = 0.0f;
    float splitAmount = 0.0f;
    float flockRadius = 24.0f;
    mfld::Vec3 flockCentroid;

    Predator predator;
    float predatorCooldown = 0.0f;
    float waveCooldown = 0.0f;
    float waveTimer = 12.0f;

    uint32_t treeSeed = 0x54524545u; // TREE
    std::array<float, TREE_COLS> canopyFar;
    std::array<float, TREE_COLS> canopyNear;
    std::vector<float> treeShade;   // per-vertex: 0 = layer base, 1 = rim
    std::vector<int> treeLayer;     // per-vertex: 0 far, 1 near
    float treeColorK4 = -1.0f;
    float treeColorK5 = -1.0f;
    float treeBuiltW = 0.0f;
    float treeBuiltH = 0.0f;
    bool treesDirty = true;

    // Profiling (milliseconds), read and reset by murmfield_stats().
    float simMsAvg = 0.0f, simMsMax = 0.0f, drawMsAvg = 0.0f, drawMsMax = 0.0f;

    std::array<ofFloatColor, SKY_LUT> skyLut;
    ofFloatColor birdDark, birdLight, treeNearCol, treeFarCol, horizonCol;
    float hazeAmount = 0.3f;
    float paletteHue = 0.58f;
    float fieldSignal = 0.5f;
    float fieldHabitat = 0.5f;
    float fieldUnstable = 0.0f;
    float canopyAlpha = 0.5f;

    // Two draws per frame through the engine's normal mesh path (whole-buffer
    // upload, which this GPU driver handles well): field + birds + signal marks
    // as triangles, then the cached canopy as one triangle strip.
    ofMesh frameMesh;
    ofMesh canopyMesh;
};
