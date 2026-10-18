#include "ShaderHostGenerator.h"
#include <cmath>
#include "ofGraphics.h"
#include "ofAppRunner.h"
#include "ofLog.h"

// Conservative GLES2-compatible vertex shader.
// Uses OF's default attribute/uniform names with bindDefaults().
static const char* VERTEX_SRC = R"(
attribute vec4 position;
attribute vec2 texcoord;

uniform mat4 modelViewProjectionMatrix;

varying vec2 v_texcoord;

void main() {
    v_texcoord = texcoord;
    gl_Position = modelViewProjectionMatrix * position;
}
)";

void ShaderHostGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineReset, 1);
    lua_setglobal(L, "shaderhost_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineLoad, 1);
    lua_setglobal(L, "shaderhost_load");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineStep, 1);
    lua_setglobal(L, "shaderhost_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineDraw, 1);
    lua_setglobal(L, "shaderhost_draw");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineIsLoaded, 1);
    lua_setglobal(L, "shaderhost_is_loaded");

    // --- SHADERHOST FEEDBACK V2 ---
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineFeedbackEnable, 1);
    lua_setglobal(L, "shaderhost_feedback_enable");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineFeedbackClear, 1);
    lua_setglobal(L, "shaderhost_feedback_clear");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineFeedbackIsEnabled, 1);
    lua_setglobal(L, "shaderhost_feedback_is_enabled");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ShaderHostGenerator::trampolineFeedbackIsActive, 1);
    lua_setglobal(L, "shaderhost_feedback_is_active");
    // --- end SHADERHOST FEEDBACK V2 ---
}

int ShaderHostGenerator::trampolineReset(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

int ShaderHostGenerator::trampolineLoad(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* src = lua_tostring(L, 1);
    bool ok = false;

    if (self && src) {
        ok = self->load(std::string(src));
    }

    lua_pushboolean(L, ok);
    return 1;
}

int ShaderHostGenerator::trampolineStep(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));

    float t   = (float)lua_tonumber(L, 1);
    float e   = (float)lua_tonumber(L, 2);
    float b   = (float)lua_tonumber(L, 3);
    float trg = lua_type(L, 4) == LUA_TNUMBER
        ? static_cast<float>(lua_tonumber(L, 4))
        : (lua_toboolean(L, 4) ? 1.0f : 0.0f);
    float a1  = (float)lua_tonumber(L, 5);
    float a2  = (float)lua_tonumber(L, 6);
    float a3  = (float)lua_tonumber(L, 7);
    float a4  = (float)lua_tonumber(L, 8);
    float a5  = (float)lua_tonumber(L, 9);

    if (self) self->step(t, e, b, trg, a1, a2, a3, a4, a5);
    return 0;
}

int ShaderHostGenerator::trampolineDraw(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->draw();
    return 0;
}

int ShaderHostGenerator::trampolineIsLoaded(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self ? self->isLoaded() : false);
    return 1;
}

// --- SHADERHOST FEEDBACK V2 ---
int ShaderHostGenerator::trampolineFeedbackEnable(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    bool enabled = lua_toboolean(L, 1) != 0;
    if (self) self->setFeedbackEnabled(enabled);
    return 0;
}

int ShaderHostGenerator::trampolineFeedbackClear(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->clearFeedback();
    return 0;
}

int ShaderHostGenerator::trampolineFeedbackIsEnabled(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self ? self->isFeedbackEnabled() : false);
    return 1;
}

int ShaderHostGenerator::trampolineFeedbackIsActive(lua_State* L) {
    ShaderHostGenerator* self = static_cast<ShaderHostGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self ? self->isFeedbackActive() : false);
    return 1;
}
// --- end SHADERHOST FEEDBACK V2 ---

void ShaderHostGenerator::reset() {
    // Clear uniforms only. Do not unload compiled shader.
    time = 0.0f;
    energy = 0.0f;
    brightness = 0.0f;
    triggerLevel = 0.0f;
    k1 = k2 = k3 = k4 = k5 = 0.0f;

    // --- SHADERHOST FEEDBACK V2 ---
    // Do NOT touch feedbackEnabled here: reset() is about clearing per-frame
    // uniform state, not about opting a mode in/out of feedback. A mode that
    // called shaderhost_feedback_enable(true) before shaderhost_reset() would
    // otherwise be silently un-opted-in.
    feedbackReady = false;
    feedbackClearRequested = true;
    // --- end SHADERHOST FEEDBACK V2 ---

    ofLogNotice("BRIDGE") << "shaderhost_reset() called, uniform cache cleared";
}

// --- SHADERHOST FEEDBACK V2 ---
void ShaderHostGenerator::setFeedbackEnabled(bool enabled) {
    // Mode opt-in only (Lua-controlled via shaderhost_feedback_enable).
    // This does NOT by itself decide whether the feedback path runs; see
    // isFeedbackActive(), which also requires the hardware gate to be open.
    if (feedbackEnabled == enabled) {
        return;
    }

    feedbackEnabled = enabled;

    if (feedbackEnabled) {
        // Allow a fresh allocation attempt each time a mode opts back in,
        // in case a previous attempt failed (e.g. transient GPU pressure).
        feedbackAllocationAttempted = false;
    }

    ofLogNotice("BRIDGE") << "ShaderHost feedback mode opt-in " << (feedbackEnabled ? "ON" : "OFF");
}

void ShaderHostGenerator::setFeedbackGateEnabled(bool enabled) {
    // Compatibility gate only; no physical-button binding. Independent of mode
    // opt-in. A mode that never called shaderhost_feedback_enable(true)
    // stays on the direct draw path no matter what this gate does.
    if (feedbackGateEnabled == enabled) {
        return;
    }

    feedbackGateEnabled = enabled;

    ofLogNotice("BRIDGE") << "ShaderHost feedback gate " << (feedbackGateEnabled ? "OPEN" : "CLOSED");
}

void ShaderHostGenerator::clearFeedback() {
    feedbackReady = false;
    feedbackClearRequested = true;

    if (feedbackAllocated) {
        for (int i = 0; i < 2; ++i) {
            feedbackFbos[i].begin();
            ofClear(0, 0, 0, 255);
            feedbackFbos[i].end();
        }
    }

    ofLogNotice("BRIDGE") << "ShaderHost feedback clear requested";
}

void ShaderHostGenerator::releaseGpuResources() {
    // Only these FBOs and this shader are owned by the host. Keep non-owning
    // external texture/provider registrations and the hardware gate intact.
    for (auto& fbo : feedbackFbos) {
        fbo.clear();
    }
    if (shader) {
        shader->unload();
        shader.reset();
    }
    feedbackAllocated = false;
    feedbackAllocationAttempted = false;
    feedbackReady = false;
    feedbackEnabled = false;
    feedbackClearRequested = true;
    feedbackReadIndex = 0;
    feedbackWriteIndex = 1;
    // Mode host retires work after all native and Lua owners are released.
}

void ShaderHostGenerator::allocateFeedbackIfNeeded() {
    if (feedbackAllocated || feedbackAllocationAttempted) {
        return;
    }

    feedbackAllocationAttempted = true;

    ofLogNotice("BRIDGE") << "ShaderHost feedback allocating " << kFeedbackW << "x" << kFeedbackH;

    ofFboSettings settings;
    settings.width = kFeedbackW;
    settings.height = kFeedbackH;
    settings.internalformat = GL_RGBA;
    settings.useDepth = false;
    settings.useStencil = false;
    settings.textureTarget = GL_TEXTURE_2D;
    settings.minFilter = GL_LINEAR;
    settings.maxFilter = GL_LINEAR;
    settings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    settings.wrapModeVertical = GL_CLAMP_TO_EDGE;
    settings.numSamples = 0;

    // If a previous attempt failed part-way through, make absolutely sure
    // neither ping-pong FBO retains a partial allocation before retrying.
    for (int i = 0; i < 2; ++i) {
        if (feedbackFbos[i].isAllocated()) {
            feedbackFbos[i].clear();
        }
    }

    for (int i = 0; i < 2; ++i) {
        feedbackFbos[i].allocate(settings);

        if (!feedbackFbos[i].isAllocated()) {
            ofLogError("BRIDGE") << "ShaderHost feedback FBO " << i << " allocation failed; "
                                  << "releasing partial feedback allocations, disabling feedback, "
                                  << "and falling back to direct draw.";
            // FBO 0 may have succeeded before FBO 1 failed.  Keeping that
            // allocation alive after a failed pair is a leak on VideoCore IV.
            for (int j = 0; j < 2; ++j) {
                feedbackFbos[j].clear();
            }
            feedbackAllocated = false;
            feedbackReady = false;
            feedbackReadIndex = 0;
            feedbackWriteIndex = 1;
            feedbackClearRequested = true;
            feedbackEnabled = false;
            return;
        }

        feedbackFbos[i].begin();
        ofClear(0, 0, 0, 255);
        feedbackFbos[i].end();
    }

    feedbackReadIndex = 0;
    feedbackWriteIndex = 1;
    feedbackReady = false;
    feedbackClearRequested = true;
    feedbackAllocated = true;

    ofLogNotice("BRIDGE") << "ShaderHost feedback allocated OK";
}
// --- end SHADERHOST FEEDBACK V2 ---

bool ShaderHostGenerator::load(const std::string& fragSrc) {
    // Release the outgoing program before compiling the incoming one.  The
    // previous implementation deliberately kept both alive until link success,
    // which is friendly on a desktop GPU but creates a large transient memory
    // peak on the CM3/VideoCore IV.  A failed load now leaves ShaderHost empty
    // instead of preserving the old program; this is safer than wedging VCHIQ.
    if (shader) {
        shader->unload();
        shader.reset();
    }

    std::unique_ptr<ofShader> candidate(new ofShader());

    bool vOk = candidate->setupShaderFromSource(GL_VERTEX_SHADER, VERTEX_SRC);
    bool fOk = candidate->setupShaderFromSource(GL_FRAGMENT_SHADER, fragSrc);

    if (!vOk || !fOk) {
        ofLogError("BRIDGE") << "shaderhost_load: compile failed (vertex=" << vOk
                              << " fragment=" << fOk << "); no shader left active.";
        return false;
    }

    candidate->bindDefaults();

    if (!candidate->linkProgram()) {
        ofLogError("BRIDGE") << "shaderhost_load: link failed; no shader left active.";
        return false;
    }

    shader = std::move(candidate);

    // SHADERHOST LAZY EXTERNAL ASSET 01
    // Allocate requested external assets here, during mode setup, before
    // ShaderHost enters any feedback/render FBO.
    prepareDeclaredExternalTextureProviders();

    ofLogNotice("BRIDGE") << "shaderhost_load: shader compiled and linked successfully";
    return true;
}

void ShaderHostGenerator::step(float t, float e, float b, float trg,
                               float a1, float a2, float a3, float a4, float a5) {
    time = t;
    energy = e;
    brightness = b;
    triggerLevel = std::isfinite(trg) ? ofClamp(trg, 0.0f, 1.0f) : 0.0f;
    k1 = a1;
    k2 = a2;
    k3 = a3;
    k4 = a4;
    k5 = a5;
}

void ShaderHostGenerator::draw() {
    if (!shader) return;

    // Use the active GL viewport, not the window size.
    // When ShaderHost draws inside the internal 1280x720 FBO, ofGetWidth()/ofGetHeight()
    // may still report HDMI/window size. The viewport reflects the current render target.
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);

    int w = viewport[2] > 0 ? viewport[2] : ofGetWidth();
    int h = viewport[3] > 0 ? viewport[3] : ofGetHeight();

    // --- SHADERHOST FEEDBACK V2 ---
    // Default path: identical to the pre-feedback implementation. No FBOs,
    // no u_prevFrame binding, no extra uniforms, no extra draw call.
    // feedbackActive requires BOTH mode opt-in (Lua) and the hardware gate
    // (compatibility API) to be true. A mode that never opted in can never reach
    // the feedback path no matter what the hardware gate is doing.
    if (!isFeedbackActive()) {
        drawDirect(w, h);
        return;
    }

    allocateFeedbackIfNeeded();

    // allocateFeedbackIfNeeded() may have flipped feedbackEnabled (mode
    // opt-in) back to false on failure. Re-check via isFeedbackActive()
    // rather than trusting the flag from the top of this function.
    if (!feedbackAllocated || !isFeedbackActive()) {
        drawDirect(w, h);
        return;
    }

    ofFbo& readFbo = feedbackFbos[feedbackReadIndex];
    ofFbo& writeFbo = feedbackFbos[feedbackWriteIndex];

    writeFbo.begin();

    if (feedbackClearRequested || !feedbackReady) {
        ofClear(0, 0, 0, 255);
        feedbackClearRequested = false;
    }

    ofEnableAlphaBlending();

    shader->begin();

    // Shader renders at the fixed small feedback resolution here, not the
    // (possibly 1280x720) viewport, so u_resolution matches what's actually
    // being rendered into writeFbo.
    shader->setUniform2f("u_resolution", (float)kFeedbackW, (float)kFeedbackH);
    shader->setUniform1f("u_time", time);
    shader->setUniform1f("u_energy", energy);
    shader->setUniform1f("u_brightness", brightness);
    shader->setUniform1f("u_trig", triggerLevel);
    shader->setUniform1f("u_k1", k1);
    shader->setUniform1f("u_k2", k2);
    shader->setUniform1f("u_k3", k3);
    shader->setUniform1f("u_k4", k4);
    shader->setUniform1f("u_k5", k5);

    shader->setUniform1f("u_feedbackEnabled", 1.0f);
    shader->setUniform1f("u_feedbackReady", feedbackReady ? 1.0f : 0.0f);

    int texUnit = 1;

    // u_prevFrame is the ONLY feedback-memory texture. Only bind it if the
    // read FBO is actually allocated and the currently loaded shader
    // declares the uniform.
    if (readFbo.isAllocated() && shaderDeclaresUniform("u_prevFrame")) {
        shader->setUniformTexture("u_prevFrame", readFbo.getTexture(), texUnit);
        texUnit++;
    }

    // Independent asset textures (e.g. u_corridorTex) are not feedback
    // memory. Bind them here too, but only if the shader declares them.
    bindDeclaredExternalTextures(texUnit);

    ofSetColor(255, 255, 255, 255);
    ofDrawRectangle(0, 0, kFeedbackW, kFeedbackH);

    shader->end();

    writeFbo.end();

    // Scale the small feedback buffer up to the real viewport for display.
    ofSetColor(255, 255, 255, 255);
    writeFbo.draw(0, 0, w, h);

    std::swap(feedbackReadIndex, feedbackWriteIndex);
    feedbackReady = true;
}

void ShaderHostGenerator::drawDirect(int w, int h) {
    // Unmodified pre-feedback direct draw path.
    ofEnableAlphaBlending();

    shader->begin();
    shader->setUniform1f("u_feedbackEnabled", 0.0f);
    shader->setUniform1f("u_feedbackReady", 0.0f);
    shader->setUniform2f("u_resolution", (float)w, (float)h);
    shader->setUniform1f("u_time", time);
    shader->setUniform1f("u_energy", energy);
    shader->setUniform1f("u_brightness", brightness);
    shader->setUniform1f("u_trig", triggerLevel);
    shader->setUniform1f("u_k1", k1);
    shader->setUniform1f("u_k2", k2);
    shader->setUniform1f("u_k3", k3);
    shader->setUniform1f("u_k4", k4);
    shader->setUniform1f("u_k5", k5);

    int texUnit = 1;

    // Asset/direct mode: no u_prevFrame, ever. Only declared external asset
    // textures (e.g. u_corridorTex) are bound here.
    bindDeclaredExternalTextures(texUnit);

    ofSetColor(255, 255, 255, 255);
    ofDrawRectangle(0, 0, w, h);

    shader->end();
}

bool ShaderHostGenerator::shaderDeclaresUniform(const std::string& uniformName) const {
    if (!shader) {
        return false;
    }

    return shader->getUniformLocation(uniformName) >= 0;
}


// SHADERHOST LAZY EXTERNAL ASSET 01
void ShaderHostGenerator::prepareDeclaredExternalTextureProviders() {
    if (!shader) return;

    for (auto& provider : externalTextureProviders) {
        if (!shaderDeclaresUniform(provider.uniformName)) {
            continue;
        }

        if (provider.prepare) {
            provider.prepare();
        }

        ofTexture* tex =
            provider.getIfReady ? provider.getIfReady() : nullptr;

        ofLogNotice("BRIDGE")
            << "ShaderHost lazy asset prepare "
            << provider.uniformName
            << " ready="
            << ((tex && tex->isAllocated()) ? "yes" : "no");
    }
}

void ShaderHostGenerator::bindDeclaredExternalTextures(int& texUnit) {
    // External asset textures are not feedback memory. They are bound here
    // (from the shared externalTextures bus) only when the currently loaded
    // shader actually declares the corresponding uniform, so a texture
    // registered for one mode/shader (e.g. u_corridorTex for corridor
    // shaders) can never leak into a shader that never asked for it.
    for (auto& entry : externalTextures) {
        const std::string& uniformName = entry.first;
        ofTexture* tex = entry.second;

        if (!tex || !tex->isAllocated()) {
            continue;
        }

        if (!shaderDeclaresUniform(uniformName)) {
            continue;
        }

        shader->setUniformTexture(uniformName, *tex, texUnit);
        texUnit++;
    }


    // SHADERHOST LAZY EXTERNAL ASSET 01
    // getIfReady() is deliberately non-allocating. This path may execute
    // while a ShaderHost feedback FBO is bound.
    for (auto& provider : externalTextureProviders) {
        if (!shaderDeclaresUniform(provider.uniformName)) {
            continue;
        }

        ofTexture* tex =
            provider.getIfReady ? provider.getIfReady() : nullptr;

        if (!tex || !tex->isAllocated()) {
            continue;
        }

        shader->setUniformTexture(
            provider.uniformName, *tex, texUnit);
        texUnit++;
    }

}
// --- end SHADERHOST FEEDBACK V2 ---

void ShaderHostGenerator::bindExternalTexture(const std::string& uniformName, ofTexture* tex) {
    for (auto& entry : externalTextures) {
        if (entry.first == uniformName) {
            entry.second = tex;
            return;
        }
    }

    externalTextures.push_back(std::make_pair(uniformName, tex));
}

void ShaderHostGenerator::bindExternalTextureProvider(
    const std::string& uniformName,
    std::function<void()> prepare,
    std::function<ofTexture*()> getIfReady) {

    for (auto& provider : externalTextureProviders) {
        if (provider.uniformName == uniformName) {
            provider.prepare = prepare;
            provider.getIfReady = getIfReady;
            return;
        }
    }

    ExternalTextureProvider provider;
    provider.uniformName = uniformName;
    provider.prepare = prepare;
    provider.getIfReady = getIfReady;
    externalTextureProviders.push_back(provider);
}
