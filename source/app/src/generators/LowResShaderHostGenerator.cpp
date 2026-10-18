#include "LowResShaderHostGenerator.h"

#include <algorithm>
#include <cmath>

// LOW RES SHADER HOST V1
// Conservative GLES2-compatible vertex shader, matching ShaderHostGenerator.
static const char* LOWRES_VERTEX_SRC = R"(
attribute vec4 position;
attribute vec2 texcoord;
uniform mat4 modelViewProjectionMatrix;
varying vec2 v_texcoord;
void main() {
    v_texcoord = texcoord;
    gl_Position = modelViewProjectionMatrix * position;
}
)";

void LowResShaderHostGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LowResShaderHostGenerator::trampolineReset, 1);
    lua_setglobal(L, "shaderlow_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LowResShaderHostGenerator::trampolineScale, 1);
    lua_setglobal(L, "shaderlow_scale");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LowResShaderHostGenerator::trampolineLoad, 1);
    lua_setglobal(L, "shaderlow_load");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LowResShaderHostGenerator::trampolineStep, 1);
    lua_setglobal(L, "shaderlow_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LowResShaderHostGenerator::trampolineDraw, 1);
    lua_setglobal(L, "shaderlow_draw");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LowResShaderHostGenerator::trampolineIsLoaded, 1);
    lua_setglobal(L, "shaderlow_is_loaded");
}

int LowResShaderHostGenerator::trampolineReset(lua_State* L) {
    LowResShaderHostGenerator* self = static_cast<LowResShaderHostGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

int LowResShaderHostGenerator::trampolineScale(lua_State* L) {
    LowResShaderHostGenerator* self = static_cast<LowResShaderHostGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->setScale((float)lua_tonumber(L, 1));
    return 0;
}

int LowResShaderHostGenerator::trampolineLoad(lua_State* L) {
    LowResShaderHostGenerator* self = static_cast<LowResShaderHostGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));

    const char* src = lua_tostring(L, 1);
    bool ok = self && src && self->load(std::string(src));
    lua_pushboolean(L, ok);
    return 1;
}

int LowResShaderHostGenerator::trampolineStep(lua_State* L) {
    LowResShaderHostGenerator* self = static_cast<LowResShaderHostGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));

    if (!self) return 0;

    float t  = (float)lua_tonumber(L, 1);
    float e  = (float)lua_tonumber(L, 2);
    float b  = (float)lua_tonumber(L, 3);
    bool trg = lua_toboolean(L, 4) != 0;
    float a1 = (float)lua_tonumber(L, 5);
    float a2 = (float)lua_tonumber(L, 6);
    float a3 = (float)lua_tonumber(L, 7);
    float a4 = (float)lua_tonumber(L, 8);
    float a5 = (float)lua_tonumber(L, 9);

    self->step(t, e, b, trg, a1, a2, a3, a4, a5);
    return 0;
}

int LowResShaderHostGenerator::trampolineDraw(lua_State* L) {
    LowResShaderHostGenerator* self = static_cast<LowResShaderHostGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->draw();
    return 0;
}

int LowResShaderHostGenerator::trampolineIsLoaded(lua_State* L) {
    LowResShaderHostGenerator* self = static_cast<LowResShaderHostGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self ? self->isLoaded() : false);
    return 1;
}

void LowResShaderHostGenerator::reset() {
    time = 0.0f;
    energy = 0.0f;
    brightness = 1.0f;
    trigFired = false;
    k1 = k2 = k3 = k4 = k5 = 0.0f;

    // Explicit per-mode default. Lua can override after reset().
    // IMPORTANT: do not invalidate renderW/renderH here.  The FBO may already
    // be the correct size from a previous visit to a low-res shader mode, and
    // forcing the cached dimensions to zero caused ensureFbo() to call
    // allocate() again on an already-allocated ofFbo.  On VideoCore IV that
    // can create relocatable-memory churn and catastrophic re-entry failures.
    // If the scale really changes, ensureFbo() compares wanted dimensions and
    // explicitly releases the old FBO before allocating a replacement.
    renderScale = 0.5f;

    ofLogNotice("BRIDGE") << "shaderlow_reset() called; default scale 0.5; FBO cache preserved";
}

void LowResShaderHostGenerator::setScale(float scale) {
    if (!std::isfinite(scale)) scale = 0.5f;
    scale = std::max(0.25f, std::min(1.0f, scale));

    // Keep the dimensions of the currently allocated FBO intact.  A scale
    // change is detected by ensureFbo() from wantedW/wantedH; that function
    // now releases the old allocation before resizing it.
    renderScale = scale;

    ofLogNotice("BRIDGE") << "shaderlow scale set to " << renderScale;
}

bool LowResShaderHostGenerator::load(const std::string& fragSrc) {
    // VideoCore IV has a very small GPU-memory pool.  Do not keep the outgoing
    // shader alive while compiling/linking its replacement: doing so raises
    // peak GPU memory precisely during the most allocation-heavy operation.
    if (shader) {
        shader->unload();
        shader.reset();
    }

    std::unique_ptr<ofShader> candidate(new ofShader());

    bool vOk = candidate->setupShaderFromSource(GL_VERTEX_SHADER, LOWRES_VERTEX_SRC);
    bool fOk = candidate->setupShaderFromSource(GL_FRAGMENT_SHADER, fragSrc);

    if (!vOk || !fOk) {
        ofLogError("BRIDGE") << "shaderlow_load: compile failed (vertex=" << vOk
                             << " fragment=" << fOk << "); no shader left active";
        return false;
    }

    candidate->bindDefaults();

    if (!candidate->linkProgram()) {
        ofLogError("BRIDGE") << "shaderlow_load: link failed; no shader left active";
        return false;
    }

    shader = std::move(candidate);
    ofLogNotice("BRIDGE") << "shaderlow_load: shader compiled and linked successfully";
    return true;
}

void LowResShaderHostGenerator::step(float t, float e, float b, bool trg,
                                     float a1, float a2, float a3, float a4, float a5) {
    time = t;
    energy = e;
    brightness = b;
    trigFired = trg;
    k1 = a1;
    k2 = a2;
    k3 = a3;
    k4 = a4;
    k5 = a5;
}

bool LowResShaderHostGenerator::ensureFbo(int targetW, int targetH) {
    int wantedW = std::max(16, (int)std::floor((float)targetW * renderScale + 0.5f));
    int wantedH = std::max(16, (int)std::floor((float)targetH * renderScale + 0.5f));

    if (renderFbo.isAllocated() && renderW == wantedW && renderH == wantedH) {
        return true;
    }

    // Never call allocate() on a live FBO when changing size.  Explicitly
    // release its GL/VideoCore resources first so a re-entry or scale change
    // cannot stack a new allocation on top of the old one.
    if (renderFbo.isAllocated()) {
        ofLogNotice("BRIDGE") << "shaderlow FBO releasing "
                              << renderW << "x" << renderH
                              << " before resize to " << wantedW << "x" << wantedH;
        renderFbo.clear();
        renderW = 0;
        renderH = 0;
    }

    ofFboSettings settings;
    settings.width = wantedW;
    settings.height = wantedH;
    settings.internalformat = GL_RGBA;
    settings.useDepth = false;
    settings.useStencil = false;
    settings.textureTarget = GL_TEXTURE_2D;
    settings.numSamples = 0;

    renderFbo.allocate(settings);

    if (!renderFbo.isAllocated()) {
        ofLogError("BRIDGE") << "shaderlow FBO allocation FAILED "
                             << wantedW << "x" << wantedH;
        // A failed ofFbo allocation can still leave partial GL objects behind.
        // Clear the object before returning so the next explicit retry starts
        // from a known-empty state.
        renderFbo.clear();
        renderW = renderH = 0;
        return false;
    }

    renderW = wantedW;
    renderH = wantedH;

    renderFbo.begin();
    ofClear(0, 0, 0, 0);
    renderFbo.end();

    ofLogNotice("BRIDGE") << "shaderlow FBO allocated "
                          << renderW << "x" << renderH
                          << " for target " << targetW << "x" << targetH;
    return true;
}

void LowResShaderHostGenerator::draw() {
    if (!shader) return;

    // SHADERLOW GL STAGE PROBE 01
    static unsigned long long probeFrame = 0;
    static unsigned int probeReports[16] = {0};
    ++probeFrame;

    auto probeGl = [&](int stage, const char* label) {
        GLenum err = GL_NO_ERROR;
        int drained = 0;

        while ((err = glGetError()) != GL_NO_ERROR && drained < 8) {
            if (probeReports[stage] < 3) {
                const char* name = "GL_ERROR";

                if (err == GL_OUT_OF_MEMORY) {
                    name = "GL_OUT_OF_MEMORY";
                }
                else if (err == GL_INVALID_ENUM) {
                    name = "GL_INVALID_ENUM";
                }
                else if (err == GL_INVALID_VALUE) {
                    name = "GL_INVALID_VALUE";
                }
                else if (err == GL_INVALID_OPERATION) {
                    name = "GL_INVALID_OPERATION";
                }
                else if (err == GL_INVALID_FRAMEBUFFER_OPERATION) {
                    name = "GL_INVALID_FRAMEBUFFER_OPERATION";
                }

                ofLogError("SHADERLOW_GL")
                    << "stage=" << stage
                    << " " << label
                    << " error=" << (unsigned int)err
                    << " " << name
                    << " frame=" << probeFrame
                    << " lowres=" << renderW << "x" << renderH
                    << " scale=" << renderScale;

                ++probeReports[stage];
            }

            ++drained;
        }
    };

    // Drain anything left by code that ran before shaderlow_draw().
    probeGl(0, "ENTRY_PENDING");


    // Capture the active EYESY canvas dimensions BEFORE entering our FBO.
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    probeGl(1, "AFTER_VIEWPORT");

    int targetW = viewport[2] > 0 ? viewport[2] : ofGetWidth();
    int targetH = viewport[3] > 0 ? viewport[3] : ofGetHeight();

    if (targetW <= 0 || targetH <= 0) return;
    if (!ensureFbo(targetW, targetH)) {
        probeGl(2, "ENSURE_FBO_FAILED");
        return;
    }
    probeGl(2, "AFTER_ENSURE_FBO");

    // Preserve the caller's blend state.  Disable blending while generating
    // the low-res RGBA texture so the shader's alpha is stored verbatim.
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);
    probeGl(3, "AFTER_DISABLE_BLEND");

    renderFbo.begin();
    probeGl(4, "AFTER_FBO_BEGIN");
    ofClear(0, 0, 0, 0);
    probeGl(5, "AFTER_FBO_CLEAR");

    shader->begin();
    probeGl(6, "AFTER_SHADER_BEGIN");
    shader->setUniform2f("u_resolution", (float)renderW, (float)renderH);
    shader->setUniform1f("u_time", time);
    shader->setUniform1f("u_energy", energy);
    shader->setUniform1f("u_brightness", brightness);
    shader->setUniform1f("u_trig", trigFired ? 1.0f : 0.0f);
    shader->setUniform1f("u_k1", k1);
    shader->setUniform1f("u_k2", k2);
    shader->setUniform1f("u_k3", k3);
    shader->setUniform1f("u_k4", k4);
    shader->setUniform1f("u_k5", k5);
    probeGl(7, "AFTER_UNIFORMS");

    ofSetColor(255, 255, 255, 255);
    ofDrawRectangle(0, 0, renderW, renderH);
    probeGl(8, "AFTER_LOWRES_SHADER_DRAW");

    shader->end();
    probeGl(9, "AFTER_SHADER_END");
    renderFbo.end();
    probeGl(10, "AFTER_FBO_END");

    if (blendWasEnabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);

    // Cheap texture upscale into the normal EYESY render target.
    ofSetColor(255, 255, 255, 255);
    renderFbo.draw(0, 0, targetW, targetH);
    probeGl(11, "AFTER_UPSCALE_DRAW");
}
