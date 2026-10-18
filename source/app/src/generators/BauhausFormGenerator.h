#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"
#include <cstdint>
#include <vector>

class BauhausFormGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    // MODE GPU RECLAIM 01
    void releaseGpuResources();

    // Safe to call during ofApp setup. The returned ofTexture object remains
    // stable for the lifetime of the generator; its storage is allocated once.
    ofTexture& getTexture();

    // SHADERHOST LAZY EXTERNAL ASSET 01
    void prepareTexture() {
        ensureAllocated();
    }

    ofTexture* getTextureIfAllocated() {
        if (!allocated || !compositionFbo.isAllocated()) return nullptr;
        return &compositionFbo.getTexture();
    }

private:
    enum ShapeType {
        SHAPE_RECT = 0,
        SHAPE_CIRCLE,
        SHAPE_RING,
        SHAPE_BAR,
        SHAPE_TRIANGLE,
        SHAPE_CROSS,
        SHAPE_ARC
    };

    struct Form {
        ShapeType type = SHAPE_RECT;

        glm::vec2 position = glm::vec2(0.5f, 0.5f);
        glm::vec2 targetPosition = glm::vec2(0.5f, 0.5f);

        glm::vec2 size = glm::vec2(0.1f, 0.1f);
        glm::vec2 targetSize = glm::vec2(0.1f, 0.1f);

        float rotation = 0.0f;
        float targetRotation = 0.0f;

        int colorRole = 1;
        int layer = 0;
        float audioPolarity = 1.0f;
    };

    static int trampolinePrepare(lua_State* L);
    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineRecompose(lua_State* L);
    static int trampolineIsReady(lua_State* L);

    void prepare();
    void step(float dt,
              float energy,
              bool trigFired,
              float knob1,
              float knob2,
              float knob3,
              float knob4,
              float knob5);
    void recompose(bool immediate);

    void ensureAllocated();
    void buildComposition(bool immediate);
    void mutateComposition();
    void assignTarget(Form& form, int index, bool preserveType);
    void updateForms(float dt);
    void renderToFbo();
    void drawForm(const Form& form, int index);

    ofColor colorForRole(int role) const;
    glm::vec2 gridPosition(bool allowOffscreen);
    glm::vec2 randomSizeForType(ShapeType type, bool dominant) const;
    float randomRotation() const;

    float random01() const;
    int randomInt(int minInclusive, int maxInclusive) const;
    static float clamp01(float value);
    static float approachAngle(float current, float target, float amount);

    static const int TEX_W = 640;
    static const int TEX_H = 360;
    static const int MAX_FORMS = 20;

    ofFbo compositionFbo;
    bool allocated = false;
    bool initialized = false;

    std::vector<Form> forms;

    mutable uint32_t rngState = 0x48415554u; // "HAUT"
    uint32_t compositionSerial = 0;

    float k1 = 0.36f;
    float k2 = 0.30f;
    float k3 = 0.18f;
    float k4 = 0.05f;
    float k5 = 0.42f;

    float smoothEnergy = 0.0f;
    float pressEnvelope = 0.0f;
    float transitionTimer = 0.0f;
    float countChangeCooldown = 0.0f;

    bool previousTrig = false;
    int activeFormCount = 8;
    int requestedFormCount = 8;
    int paletteIndex = 0;
    int majorMutationCountdown = 5;
};
