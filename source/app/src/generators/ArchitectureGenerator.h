#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"

#include <array>
#include <cstdint>

class ArchitectureGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    // MODE GPU RECLAIM 01
    void releaseGpuResources();

private:
    enum PrimitiveType {
        PRIM_PORTAL = 0,
        PRIM_COLUMN = 1,
        PRIM_SLAB   = 2,
        PRIM_GRID   = 3,
        PRIM_BRACE  = 4,
        PRIM_MASS   = 5,
        PRIM_DISC   = 6,
        PRIM_RING   = 7,
        PRIM_LINK   = 8
    };

    enum LayoutFamily {
        LAYOUT_HALL = 0,
        LAYOUT_FACADE = 1,
        LAYOUT_TOWERS = 2,
        LAYOUT_COUNT = 3
    };

    enum BuildState {
        STATE_ERECT = 0,
        STATE_SPAN = 1,
        STATE_INFILL = 2,
        STATE_HOLD = 3,
        STATE_COUNT = 4
    };

    struct Element {
        PrimitiveType type = PRIM_PORTAL;
        glm::vec2 pos = glm::vec2(0.5f, 0.5f);
        glm::vec2 size = glm::vec2(0.25f, 0.25f);
        glm::vec2 targetPos = glm::vec2(0.5f, 0.5f);
        glm::vec2 targetSize = glm::vec2(0.25f, 0.25f);
        glm::vec2 end = glm::vec2(0.5f, 0.5f);
        glm::vec2 targetEnd = glm::vec2(0.5f, 0.5f);
        float depth = 0.5f;
        float thickness = 1.5f;
        float birth = 0.0f;
        float duration = 1.0f;
        float phase = 0.0f;
        float opacity = 1.0f;
        int role = 0;
        int variant = 0;
        int rows = 0;
        int cols = 0;
    };

    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineMeta(lua_State* L);
    static int trampolineIsReady(lua_State* L);
    static int trampolineReseed(lua_State* L);

    void step(float dt,
              float peak,
              bool trigFired,
              float knob1,
              float knob2,
              float knob3,
              float knob4,
              float knob5);
    void draw(int w, int h, bool persistOn, bool persistClear, int persistFade);

    void ensureAllocated();
    void clearPixels();
    void renderIfNeeded(bool force);
    void renderScene();

    void rebuildScene();
    void archiveCurrentScene();
    void buildHall();
    void buildFacade();
    void buildTowerCluster();
    void addElement(PrimitiveType type,
                    float x,
                    float y,
                    float w,
                    float h,
                    float depth,
                    float thickness,
                    float birth,
                    float duration,
                    int role,
                    int variant = 0,
                    int rows = 0,
                    int cols = 0,
                    float opacity = 1.0f);
    void addAccent();
    void addImpossibleConnection(bool audioKick);
    void mutateOneElement(bool audioKick);
    void updateMutations(float dt, bool trigEdge);
    void addLink(float x0, float y0, float x1, float y1,
                 float depth, float thickness, float birth,
                 float duration, int role, int variant, float opacity = 1.0f);

    void updateState(float dt, bool trigEdge);
    void chooseNextState();
    int choosePaletteBank(int previousBank);
    LayoutFamily chooseLayout(LayoutFamily previousLayout);

    float progressFor(const Element& e) const;
    void transformedRect(const Element& e,
                         float progress,
                         glm::vec2& pos,
                         glm::vec2& size) const;

    void renderArray(const std::array<Element, 30>& source,
                     int count,
                     float sceneTime,
                     int palette,
                     float gain,
                     bool archived);
    void renderDepthPass(const std::array<Element, 30>& source,
                         int count,
                         float sceneTime,
                         int palette,
                         float gain,
                         bool archived,
                         int pass);

    void drawElement(const Element& e,
                     int index,
                     float sceneTime,
                     int palette,
                     float gain,
                     bool archived);
    void drawPortal(const Element& e,
                    const glm::vec2& pos,
                    const glm::vec2& size,
                    float progress,
                    const ofColor& color,
                    float gain);
    void drawGrid(const Element& e,
                  const glm::vec2& pos,
                  const glm::vec2& size,
                  float progress,
                  const ofColor& color,
                  float gain,
                  bool archived);
    void drawBrace(const Element& e,
                   const glm::vec2& pos,
                   const glm::vec2& size,
                   float progress,
                   const ofColor& color,
                   float gain);
    void drawMass(const Element& e,
                  const glm::vec2& pos,
                  const glm::vec2& size,
                  float progress,
                  const ofColor& color,
                  float gain);
    void drawDiscField(const Element& e,
                       const glm::vec2& pos,
                       const glm::vec2& size,
                       float progress,
                       const ofColor& color,
                       float gain);
    void drawRingField(const Element& e,
                       const glm::vec2& pos,
                       const glm::vec2& size,
                       float progress,
                       const ofColor& color,
                       float gain);
    void drawLink(const Element& e,
                  const glm::vec2& start,
                  const glm::vec2& end,
                  float progress,
                  const ofColor& color,
                  float gain);

    void drawLine(float x0, float y0, float x1, float y1,
                  float thickness, const ofColor& color, float alpha = 1.0f);
    void drawDisc(float cx, float cy, float radius,
                  const ofColor& color, float alpha = 1.0f);
    void fillRect(float x0, float y0, float x1, float y1,
                  const ofColor& color, float alpha = 1.0f);
    void blendPixel(int x, int y, const ofColor& color, float alpha);

    ofColor paletteColor(int palette, int role, float depth) const;

    uint32_t nextRand();
    float rand01();
    float randRange(float lo, float hi);
    int randInt(int lo, int hi);

    static float clamp01(float value);
    static float smooth01(float value);
    static float lerp(float a, float b, float amount);
    float stressAmount() const;

    enum {
        TEX_W = 320,
        TEX_H = 180,
        MAX_ELEMENTS = 30
    };

    ofPixels pixels;
    ofTexture texture;
    bool allocated = false;
    bool ready = false;

    std::array<Element, MAX_ELEMENTS> elements;
    int elementCount = 0;

    std::array<Element, MAX_ELEMENTS> previousElements;
    int previousElementCount = 0;

    uint32_t rngState = 0x41524332u; // "ARC2"
    uint32_t sceneSerial = 0u;

    LayoutFamily layout = LAYOUT_HALL;
    LayoutFamily previousLayout = LAYOUT_HALL;
    BuildState state = STATE_ERECT;

    int paletteBank = 0;
    int previousPaletteBank = 0;

    float sceneAge = 0.0f;
    float previousSceneAge = 0.0f;
    float nextSceneAt = 24.0f;
    float stateAge = 0.0f;
    float stateHold = 4.0f;
    float additionCooldown = 0.0f;
    float mutationCooldown = 0.0f;
    float connectionCooldown = 0.0f;

    float transitionAge = 10.0f;
    float transitionDuration = 3.0f;

    float renderAccumulator = 0.0f;
    float energyEnvelope = 0.0f;
    float triggerEnvelope = 0.0f;
    bool previousTrig = false;

    float k1 = 0.40f; // density
    float k2 = 0.25f; // build rate / turnover
    float k3 = 0.72f; // structural stress; upper range is the expressive zone
    float k4 = 0.35f; // architectural character
    float k5 = 0.45f; // depth / atmosphere
};
