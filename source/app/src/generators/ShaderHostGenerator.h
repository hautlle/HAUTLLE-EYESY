#pragma once
#include <functional>

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"
#include <memory>
#include <string>
#include <vector>
#include <utility>

class ShaderHostGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    bool load(const std::string& fragSrc);

    void step(float t,
              float e,
              float b,
              float trg,
              float knob1,
              float knob2,
              float knob3,
              float knob4,
              float knob5);

    void draw();

    bool isLoaded() const {
        return shader != nullptr;
    }

    // --- TEXTURE BIND DIAG ---
    void bindExternalTexture(const std::string& uniformName, ofTexture* tex);

    // SHADERHOST LAZY EXTERNAL ASSET 01
    // prepare() may allocate, but is called only during shader load.
    // getIfReady() MUST NOT allocate; it is safe to call during draw.
    void bindExternalTextureProvider(
        const std::string& uniformName,
        std::function<void()> prepare,
        std::function<ofTexture*()> getIfReady);
    // --- end TEXTURE BIND DIAG ---

    // --- SHADERHOST FEEDBACK V2 (asset/feedback split) ---
    // Mode opt-in only. Disabled by default. Does not allocate anything and
    // does not touch the draw() path unless a mode explicitly calls
    // shaderhost_feedback_enable(true) from Lua. This is the ONLY thing
    // Lua/mode code controls.
    void setFeedbackEnabled(bool enabled);
    bool isFeedbackEnabled() const {
        return feedbackEnabled;
    }

    // Compatibility gate only; the old Shift+Persist binding is retired.
    // Separate from mode opt-in. Toggling the gate on a mode
    // that has not opted in must never enable the feedback path.
    void setFeedbackGateEnabled(bool enabled);
    bool isFeedbackGateEnabled() const {
        return feedbackGateEnabled;
    }

    // True only when the mode has opted in AND the hardware gate is open.
    // This is what draw() actually uses to pick a path.
    bool isFeedbackActive() const {
        return feedbackEnabled && feedbackGateEnabled;
    }

    void clearFeedback();
    void releaseGpuResources();
    // --- end SHADERHOST FEEDBACK V2 ---

private:
    static int trampolineReset(lua_State* L);
    static int trampolineLoad(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineIsLoaded(lua_State* L);

    // --- SHADERHOST FEEDBACK V2 ---
    static int trampolineFeedbackEnable(lua_State* L);
    static int trampolineFeedbackClear(lua_State* L);
    static int trampolineFeedbackIsEnabled(lua_State* L);
    static int trampolineFeedbackIsActive(lua_State* L);

    void allocateFeedbackIfNeeded();
    void drawDirect(int w, int h);

    // Asset vs feedback split helpers.
    // A texture (asset or u_prevFrame) should only ever be bound if the
    // currently loaded shader actually declares that uniform. This is what
    // keeps external asset textures (e.g. u_corridorTex) from leaking into
    // shaders that never asked for them, and vice versa.
    bool shaderDeclaresUniform(const std::string& uniformName) const;
    void bindDeclaredExternalTextures(int& texUnit);
    // --- end SHADERHOST FEEDBACK V2 ---

    std::unique_ptr<ofShader> shader;

    // --- TEXTURE BIND DIAG ---
    std::vector<std::pair<std::string, ofTexture*>> externalTextures;

    // SHADERHOST LAZY EXTERNAL ASSET 01
    struct ExternalTextureProvider {
        std::string uniformName;
        std::function<void()> prepare;
        std::function<ofTexture*()> getIfReady;
    };

    std::vector<ExternalTextureProvider> externalTextureProviders;

    void prepareDeclaredExternalTextureProviders();
    // --- end TEXTURE BIND DIAG ---

    // --- SHADERHOST FEEDBACK V2 ---
    // Fixed, deliberately small feedback resolution. Independent of the
    // active render viewport (which may be 1280x720) so feedback never
    // competes for GPU memory the way the failed v1 attempt did.
    static const int kFeedbackW = 640;
    static const int kFeedbackH = 360;

    ofFbo feedbackFbos[2];
    int feedbackReadIndex = 0;
    int feedbackWriteIndex = 1;
    bool feedbackAllocated = false;
    bool feedbackAllocationAttempted = false;
    bool feedbackReady = false;
    bool feedbackEnabled = false;          // mode opt-in, off by default (Lua-controlled)
    bool feedbackGateEnabled = true;       // compatibility gate, open by default; no button binding
    bool feedbackClearRequested = true;
    // --- end SHADERHOST FEEDBACK V2 ---

    float time = 0.0f;
    float energy = 0.0f;
    float brightness = 1.0f;
    float triggerLevel = 0.0f;

    float k1 = 0.0f;
    float k2 = 0.0f;
    float k3 = 0.0f;
    float k4 = 0.0f;
    float k5 = 0.0f;
};
