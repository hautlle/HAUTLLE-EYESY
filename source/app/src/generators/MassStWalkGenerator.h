#pragma once

// HAUTLLE_MASST_WALK v4
// Massachusetts Street, Lawrence KS (7th to 10th): OSM massing +
// historically-informed facade grammar + a few hand-corrected landmarks.
// Map data (c) OpenStreetMap contributors, ODbL.

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class MassStWalkGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

private:
    enum { MAX_HITS = 8 };

    struct P3 {
        float u = 0.0f;   // metres along street from 7th
        float v = 0.0f;   // metres right of centreline when walking south
        float z = 0.0f;   // metres above street
    };

    enum Kind : uint8_t {
        STRUCT = 0,
        ROOF = 1,
        WINDOW = 2,
        STORE = 3,
        STREET = 4,
        DETAIL = 5
    };

    enum Style : uint8_t {
        DEFAULT_STYLE = 0,
        ELDRIDGE_701,
        CLASSICAL_700,
        ROMANESQUE_704,
        WOODWARD_801,
        SUNFLOWER_804,
        HOUSE_729,
        WEAVERS_901
    };

    struct Segment {
        P3 a, b;
        int building = -1;
        int window = -1;
        Kind kind = STRUCT;
        uint32_t seed = 0;
    };

    struct Building {
        uint32_t osmId = 0;
        float height = 9.0f;
        float uMin = 0.0f, uMax = 0.0f, uMid = 0.0f;
        float vMid = 0.0f;
        float frontAbsV = 0.0f;
        bool backLot = false;
        uint32_t seed = 0;
        int levels = 2;
        int segFirst = 0, segCount = 0;
        Style style = DEFAULT_STYLE;
    };

    struct PulseEvent {
        float time = -100.0f, amplitude = 0.0f, origin = 0.0f;
        bool wave = false;
        std::vector<int> selected;
    };

    void launchPulse(float strength);
    float buildingPulse(int building, float tileU) const;

    static int trampolineLoad(lua_State* L);
    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineIsReady(lua_State* L);
    static int trampolineStats(lua_State* L);

    bool load(const std::string& path);
    void addSegment(int building, Kind kind, const P3& a, const P3& b, int window = -1,
                    uint32_t seed = 0);
    void addRect(int building, int window, Kind kind, float u0, float v0, float u1, float v1,
                 float z0, float z1);
    void addArcOnFacade(int building, Kind kind, const P3& a, const P3& b,
                        float sCenter, float width, float springZ, float rise, int steps,
                        uint32_t seed);
    void addFacade(int building, const P3& a, const P3& b, int levels, bool storefront);
    void addFacadeCharacter(int building, const P3& a, const P3& b, bool storefront,
                            int levels);
    void buildStreetDetails();
    Style classifyStyle(uint32_t osmId, float uMid, float vMid) const;

    void step(float dt, float low, float mid, float high, float energy, bool trigFired,
              float k1, float k2, float k3, float k4, float k5);
    void draw(float width, float height, bool persistOn, bool persistClear, int persistFade);

    static float clamp01(float v);
    static float lerp(float a, float b, float t);
    static float response(float dt, float rate);
    static uint32_t hash32(uint32_t x);
    static float hash01(uint32_t x);
    static ofFloatColor hsv(float h, float s, float v, float a = 1.0f);

    std::vector<Building> buildings;
    std::vector<Segment> segments;
    std::vector<Segment> streetSegments;
    std::vector<float> crossings;
    int windowCount = 0;
    float streetLength = 600.0f;
    bool loaded = false;
    std::string loadedPath;

    // Centreline glide state
    float camU = 0.0f;
    float elapsed = 0.0f;

    // Knobs: Depth / Structure Pulse / Occupancy / Signal Age / Field Error
    float k1 = 0.4f, k2 = 0.5f, k3 = 0.5f, k4 = 0.33f, k5 = 0.1f;

    // Audio
    float lowEnv = 0.0f, midEnv = 0.0f, highEnv = 0.0f, energyEnv = 0.0f;
    float slowLow = 0.0f, midSlow = 0.0f;
    bool previousTrig = false;
    float hitCooldown = 0.0f;
    bool hitArmed = true;
    float lowPeak = 0.0f;
    float prevLowEnv = 0.0f;
    std::array<PulseEvent, MAX_HITS> hits;
    uint32_t groupRotation = 0;
    int groupsSinceWave = 0;
    float lastWaveTime = 0.0f;
    int hitNext = 0;
    float litDrift = 0.0f;
    uint32_t frameCounter = 0;
    uint32_t rng = 0x4D415353u;   // MASS

    // Glitch tear band (screen space)
    float tearY0 = 0.0f, tearY1 = 0.0f, tearShift = 0.0f, tearLife = 0.0f;

    ofMesh lineMesh;
    float simMsAvg = 0.0f, simMsMax = 0.0f, drawMsAvg = 0.0f, drawMsMax = 0.0f;
    int lastSegmentsDrawn = 0;
};
