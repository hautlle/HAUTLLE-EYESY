#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"
#include <array>
#include <cstdint>
#include <vector>

class MarkovMoshGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    // MODE GPU RECLAIM 02
    void releaseGpuResources();

    void step(float time,
              float energy,
              float low,
              float mid,
              float high,
              bool trig,
              float k1,
              float k2,
              float k3,
              float k4,
              float k5);

    ofTexture& getTexture() { return texture; }

    // Packed for ShaderHost's u_brightness:
    // floor(meta * 8.0) = state index, fract(meta * 8.0) = transition morph.
    float getPackedMeta() const;

private:
    enum State {
        DRIFT_X = 0,
        DRIFT_Y = 1,
        SHEAR   = 2,
        TEAR    = 3,
        FOLD    = 4,
        LOCK    = 5,
        STATE_COUNT = 6
    };

    struct Line {
        ofVec2f a;
        ofVec2f b;
        ofVec2f startA;
        ofVec2f startB;
        ofFloatColor color;
        float thickness = 2.0f;
        float delay = 0.0f;
        float duration = 0.8f;
        float phase = 0.0f;
        float flex = 0.0f;
        bool paired = false;
        bool dashed = false;
    };

    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineMeta(lua_State* L);

    static constexpr int TEX_W = 320;
    static constexpr int TEX_H = 180;
    static constexpr int MAX_LINES = 14;

    void ensureAllocated();
    void clearPixels();
    void spawnChain(bool force);
    void chooseNextState(bool audioKick);
    void updateStateMachine(float dt);
    void updateDirectionBias(float dt);
    void renderLines();

    float rand01();
    float randRange(float a, float b);
    int randInt(int lo, int hi);
    uint32_t nextRand();

    ofFloatColor paletteColor(int palette, int role, float variation) const;
    void putPixel(int x, int y, const ofFloatColor& c, float alpha = 1.0f);
    void drawDisc(float cx, float cy, float radius, const ofFloatColor& c, float alpha);
    void drawSegment(const ofVec2f& a,
                     const ofVec2f& b,
                     float thickness,
                     const ofFloatColor& c,
                     float alpha,
                     bool dashed);

    ofPixels pixels;
    ofTexture texture;
    bool allocated = false;

    std::vector<Line> lines;

    uint32_t rngState = 69069u;

    float now = 0.0f;
    float lastTime = 0.0f;
    float chainAge = 0.0f;
    float nextChainAt = 5.0f;
    float stateAge = 0.0f;
    float stateHold = 4.0f;
    float transitionAge = 1.0f;
    float transitionDuration = 0.6f;
    float directionTimer = 0.0f;
    float triggerImpulse = 0.0f;

    State state = DRIFT_X;
    State previousState = DRIFT_X;

    int signX = 1;
    int signY = 1;
    float biasX = 0.25f;
    float biasY = 0.25f;

    float energy = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    bool trig = false;

    float k1 = 0.5f;
    float k2 = 0.4f;
    float k3 = 0.45f;
    float k4 = 0.25f;
    float k5 = 0.7f;
};
