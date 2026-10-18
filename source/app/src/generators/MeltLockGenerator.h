#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"

#include <cstdint>
#include <vector>

class MeltLockGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    // MELTLOCK CENTRAL RECLAIM 01
    void releaseGpuResources();

    // Stable GL_TEXTURE_2D object used by ShaderHost as u_meltlockTex.
    ofTexture& getTexture();

    // SHADERHOST LAZY EXTERNAL ASSET 01
    void prepareTexture() {
        ensureAllocated();
    }

    ofTexture* getTextureIfAllocated() {
        if (!allocated || !shaderReady || !blurPongFbo.isAllocated()) return nullptr;
        return &blurPongFbo.getTexture();
    }

private:
    struct Blob {
        glm::vec2 position = glm::vec2(0.5f, 0.5f);
        glm::vec2 velocity = glm::vec2(0.0f, -0.05f);

        float radius = 0.10f;
        float targetRadius = 0.10f;
        float radiusScale = 1.0f;
        float phase = 0.0f;
        float phaseSpeed = 1.0f;
        float drift = 0.5f;
        float polarity = 1.0f;
        float squash = 0.0f;
    };

    static int trampolinePrepare(lua_State* L);
    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
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

    void ensureAllocated();

    bool buildBlurShader();
    void initializeBlobs(bool completelyNew);
    void initializeBlob(Blob& blob, int index, bool fromBoundary);
    void updateBlobCount();
    void updateBlobs(float dt);
    void applyPairForces(float dt);
    void renderField();
    void renderBlurPass(ofFbo& source,
                        ofFbo& destination,
                        float dx,
                        float dy);
    void drawBlob(const Blob& blob, int index);

    float random01() const;
    float randomRange(float low, float high) const;
    int randomInt(int minInclusive, int maxInclusive) const;
    static float clamp01(float value);
    static float lerp(float a, float b, float t);

    static const int TEX_W = 320;
    static const int TEX_H = 180;
    static const int MIN_BLOBS = 4;
    static const int MAX_BLOBS = 16;

    ofFbo sourceFbo;
    ofFbo blurPingFbo;
    ofFbo blurPongFbo;
    ofShader blurShader;

    bool allocated = false;
    bool allocationFailed = false;
    bool shaderReady = false;
    bool initialized = false;

    std::vector<Blob> blobs;

    mutable uint32_t rngState = 0x4d454c54u; // "MELT"
    uint32_t resetSerial = 0;

    float k1 = 0.44f;
    float k2 = 0.24f;
    float k3 = 0.05f;
    float k4 = 0.08f;
    float k5 = 0.56f;

    float smoothEnergy = 0.0f;
    float pressureEnvelope = 0.0f;
    float fractureEnvelope = 0.0f;

    bool previousTrig = false;
    int activeBlobCount = 9;
    int requestedBlobCount = 9;
};
