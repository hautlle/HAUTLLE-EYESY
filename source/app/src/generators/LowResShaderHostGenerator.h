#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"
#include <memory>

// LOW RES SHADER HOST V1
// Dedicated low-resolution GLSL host.
// It does NOT use ShaderHostGenerator's private feedback FBOs.
class LowResShaderHostGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    bool load(const std::string& fragSrc);
    void step(float t, float e, float b, bool trg,
              float a1, float a2, float a3, float a4, float a5);
    void draw();
    void setScale(float scale);

    bool isLoaded() const { return shader != nullptr; }

private:
    static int trampolineReset(lua_State* L);
    static int trampolineScale(lua_State* L);
    static int trampolineLoad(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineIsLoaded(lua_State* L);

    bool ensureFbo(int targetW, int targetH);

    std::unique_ptr<ofShader> shader;
    ofFbo renderFbo;

    float renderScale = 0.5f;
    int renderW = 0;
    int renderH = 0;

    float time = 0.0f;
    float energy = 0.0f;
    float brightness = 1.0f;
    bool trigFired = false;
    float k1 = 0.0f;
    float k2 = 0.0f;
    float k3 = 0.0f;
    float k4 = 0.0f;
    float k5 = 0.0f;
};
