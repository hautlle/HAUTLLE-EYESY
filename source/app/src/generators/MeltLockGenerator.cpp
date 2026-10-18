#include "MeltLockGenerator.h"

#include "ofGraphics.h"
#include "ofLog.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float PI_F = 3.14159265358979323846f;

const char* BLUR_VERT = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif

uniform mat4 modelViewProjectionMatrix;
attribute vec4 position;
attribute vec2 texcoord;
varying vec2 vTexCoord;

void main() {
    vTexCoord = texcoord;
    gl_Position = modelViewProjectionMatrix * position;
}
)GLSL";

const char* BLUR_FRAG = R"GLSL(
#ifdef GL_ES
precision mediump float;
precision lowp sampler2D;
#endif

uniform sampler2D tex0;
uniform vec2 u_direction;
varying vec2 vTexCoord;

void main() {
    vec3 color = texture2D(tex0, vTexCoord).rgb * 0.2270270270;
    color += texture2D(tex0, vTexCoord + u_direction * 1.3846153846).rgb * 0.3162162162;
    color += texture2D(tex0, vTexCoord - u_direction * 1.3846153846).rgb * 0.3162162162;
    color += texture2D(tex0, vTexCoord + u_direction * 3.2307692308).rgb * 0.0702702703;
    color += texture2D(tex0, vTexCoord - u_direction * 3.2307692308).rgb * 0.0702702703;
    gl_FragColor = vec4(color, 1.0);
}
)GLSL";
}

void MeltLockGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MeltLockGenerator::trampolinePrepare, 1);
    lua_setglobal(L, "meltlock_prepare");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MeltLockGenerator::trampolineReset, 1);
    lua_setglobal(L, "meltlock_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MeltLockGenerator::trampolineStep, 1);
    lua_setglobal(L, "meltlock_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MeltLockGenerator::trampolineIsReady, 1);
    lua_setglobal(L, "meltlock_is_ready");
}

int MeltLockGenerator::trampolinePrepare(lua_State* L) {
    MeltLockGenerator* self = static_cast<MeltLockGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->prepare();
    return 0;
}

int MeltLockGenerator::trampolineReset(lua_State* L) {
    MeltLockGenerator* self = static_cast<MeltLockGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

// meltlock_step(dt, energy, trig, k1, k2, k3, k4, k5)
int MeltLockGenerator::trampolineStep(lua_State* L) {
    MeltLockGenerator* self = static_cast<MeltLockGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));

    const float dt = static_cast<float>(lua_tonumber(L, 1));
    const float energy = static_cast<float>(lua_tonumber(L, 2));
    const bool trigFired = lua_toboolean(L, 3) != 0;
    const float a1 = static_cast<float>(lua_tonumber(L, 4));
    const float a2 = static_cast<float>(lua_tonumber(L, 5));
    const float a3 = static_cast<float>(lua_tonumber(L, 6));
    const float a4 = static_cast<float>(lua_tonumber(L, 7));
    const float a5 = static_cast<float>(lua_tonumber(L, 8));

    if (self) self->step(dt, energy, trigFired, a1, a2, a3, a4, a5);
    return 0;
}

int MeltLockGenerator::trampolineIsReady(lua_State* L) {
    MeltLockGenerator* self = static_cast<MeltLockGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L,
                    self && self->allocated && self->shaderReady && self->initialized);
    return 1;
}

float MeltLockGenerator::clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float MeltLockGenerator::lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float MeltLockGenerator::random01() const {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return static_cast<float>(x & 0x00ffffffu) / 16777215.0f;
}

float MeltLockGenerator::randomRange(float low, float high) const {
    return lerp(low, high, random01());
}

int MeltLockGenerator::randomInt(int minInclusive, int maxInclusive) const {
    if (maxInclusive <= minInclusive) return minInclusive;
    const int span = maxInclusive - minInclusive + 1;
    int value = static_cast<int>(random01() * static_cast<float>(span));
    if (value >= span) value = span - 1;
    return minInclusive + value;
}

void MeltLockGenerator::releaseGpuResources() {
    // ofFbo::clear() explicitly releases framebuffer/renderbuffer/texture GL
    // objects.  Use this after any partial allocation failure so a retry never
    // piles fresh VideoCore allocations on top of half-created resources.
    sourceFbo.clear();
    blurPingFbo.clear();
    blurPongFbo.clear();
    blurShader.unload();

    allocated = false;
    shaderReady = false;
    initialized = false;
}

bool MeltLockGenerator::buildBlurShader() {
    blurShader.unload();

    bool okay = true;
    okay = blurShader.setupShaderFromSource(GL_VERTEX_SHADER, BLUR_VERT) && okay;
    okay = blurShader.setupShaderFromSource(GL_FRAGMENT_SHADER, BLUR_FRAG) && okay;
    if (okay) blurShader.bindDefaults();
    okay = okay && blurShader.linkProgram();

    if (!okay) {
        ofLogError("BRIDGE")
            << "MeltLockGenerator: blur shader failed to compile/link";
        return false;
    }

    ofLogNotice("BRIDGE")
        << "MeltLockGenerator: blur shader ready";
    return true;
}

void MeltLockGenerator::ensureAllocated() {
    // A persistent allocation failure must not retry three FBO allocations and
    // a shader compile every frame. meltlock_reset() clears this latch and
    // permits one explicit retry.
    if (allocated || allocationFailed) return;

    // Defensive cleanup: if a previous attempt left any partial GL objects,
    // release them before beginning the next explicit allocation attempt.
    releaseGpuResources();

    ofFboSettings settings;
    settings.width = TEX_W;
    settings.height = TEX_H;
    settings.internalformat = GL_RGBA;
    settings.useDepth = false;
    settings.useStencil = false;
    settings.textureTarget = GL_TEXTURE_2D;
    settings.numSamples = 0;

    // Allocate sequentially and stop at the first failure.  The old code made
    // all three allocation calls before checking, multiplying allocator churn
    // when VideoCore was already under pressure.
    sourceFbo.allocate(settings);
    if (!sourceFbo.isAllocated()) {
        allocationFailed = true;
        releaseGpuResources();
        ofLogError("BRIDGE")
            << "MeltLockGenerator: source FBO allocation failed "
            << TEX_W << "x" << TEX_H
            << "; partial resources released; retry latched until meltlock_reset()";
        return;
    }

    blurPingFbo.allocate(settings);
    if (!blurPingFbo.isAllocated()) {
        allocationFailed = true;
        releaseGpuResources();
        ofLogError("BRIDGE")
            << "MeltLockGenerator: blur ping FBO allocation failed "
            << TEX_W << "x" << TEX_H
            << "; partial resources released; retry latched until meltlock_reset()";
        return;
    }

    blurPongFbo.allocate(settings);
    if (!blurPongFbo.isAllocated()) {
        allocationFailed = true;
        releaseGpuResources();
        ofLogError("BRIDGE")
            << "MeltLockGenerator: blur pong FBO allocation failed "
            << TEX_W << "x" << TEX_H
            << "; partial resources released; retry latched until meltlock_reset()";
        return;
    }

    sourceFbo.begin();
    ofClear(0, 0, 0, 255);
    sourceFbo.end();

    blurPingFbo.begin();
    ofClear(0, 0, 0, 255);
    blurPingFbo.end();

    blurPongFbo.begin();
    ofClear(0, 0, 0, 255);
    blurPongFbo.end();

    shaderReady = buildBlurShader();
    if (!shaderReady) {
        allocationFailed = true;
        releaseGpuResources();
        ofLogError("BRIDGE")
            << "MeltLockGenerator: blur shader setup failed; field FBOs released; "
            << "retry latched until meltlock_reset()";
        return;
    }

    allocated = true;

    ofLogNotice("BRIDGE")
        << "MeltLockGenerator: field FBOs allocated "
        << TEX_W << "x" << TEX_H;
}

ofTexture& MeltLockGenerator::getTexture() {
    ensureAllocated();
    return blurPongFbo.getTexture();
}

void MeltLockGenerator::initializeBlob(Blob& blob,
                                       int index,
                                       bool fromBoundary) {
    const bool rising = (index % 2 == 0);
    blob.polarity = rising ? -1.0f : 1.0f;

    blob.position.x = randomRange(0.12f, 0.88f);
    if (fromBoundary) {
        blob.position.y = rising ? randomRange(0.83f, 1.08f)
                                 : randomRange(-0.08f, 0.17f);
    } else {
        blob.position.y = randomRange(0.03f, 0.97f);
    }

    blob.velocity.x = randomRange(-0.025f, 0.025f);
    blob.velocity.y = blob.polarity * randomRange(0.025f, 0.065f);

    const float density = static_cast<float>(activeBlobCount - MIN_BLOBS)
                        / static_cast<float>(MAX_BLOBS - MIN_BLOBS);
    const float baseRadius = lerp(0.128f, 0.064f, density);
    blob.radiusScale = randomRange(0.72f, 1.38f);
    blob.targetRadius = baseRadius * blob.radiusScale;
    blob.radius = blob.targetRadius * randomRange(0.82f, 1.12f);

    blob.phase = randomRange(0.0f, PI_F * 2.0f);
    blob.phaseSpeed = randomRange(0.45f, 1.35f);
    blob.drift = randomRange(0.45f, 1.15f);
    blob.squash = randomRange(-0.18f, 0.24f);
}

void MeltLockGenerator::initializeBlobs(bool completelyNew) {
    requestedBlobCount = MIN_BLOBS
                       + static_cast<int>(std::round(k1 * (MAX_BLOBS - MIN_BLOBS)));
    requestedBlobCount = std::max(MIN_BLOBS,
                                  std::min(MAX_BLOBS, requestedBlobCount));
    activeBlobCount = requestedBlobCount;

    const int oldCount = static_cast<int>(blobs.size());
    blobs.resize(activeBlobCount);

    for (int i = 0; i < activeBlobCount; ++i) {
        if (completelyNew || i >= oldCount) {
            initializeBlob(blobs[i], i, i >= oldCount && !completelyNew);
        }
    }
}

void MeltLockGenerator::prepare() {
    ensureAllocated();
    if (!allocated || !shaderReady) return;

    if (!initialized) {
        initializeBlobs(true);
        initialized = true;
        renderField();
    }
}

void MeltLockGenerator::reset() {
    if (allocationFailed && !allocated) {
        allocationFailed = false;
    }
    ensureAllocated();
    if (!allocated || !shaderReady) return;

    resetSerial++;
    rngState ^= 0x9e3779b9u + resetSerial * 0x85ebca6bu;
    smoothEnergy = 0.0f;
    pressureEnvelope = 0.0f;
    fractureEnvelope = 0.0f;
    previousTrig = false;

    initializeBlobs(true);
    initialized = true;
    renderField();

    ofLogNotice("BRIDGE")
        << "meltlock_reset() called, serial=" << resetSerial;
}

void MeltLockGenerator::updateBlobCount() {
    requestedBlobCount = MIN_BLOBS
                       + static_cast<int>(std::round(k1 * (MAX_BLOBS - MIN_BLOBS)));
    requestedBlobCount = std::max(MIN_BLOBS,
                                  std::min(MAX_BLOBS, requestedBlobCount));

    if (requestedBlobCount == activeBlobCount) return;

    const int oldCount = activeBlobCount;
    activeBlobCount = requestedBlobCount;
    blobs.resize(activeBlobCount);

    for (int i = oldCount; i < activeBlobCount; ++i) {
        initializeBlob(blobs[i], i, true);
    }

    const float density = static_cast<float>(activeBlobCount - MIN_BLOBS)
                        / static_cast<float>(MAX_BLOBS - MIN_BLOBS);
    const float baseRadius = lerp(0.128f, 0.064f, density);
    // Preserve each body's identity while the population changes. Reusing the
    // stored radius scale avoids a full-field size jump at every K1 threshold.
    for (Blob& blob : blobs) {
        blob.targetRadius = baseRadius * blob.radiusScale;
    }
}

void MeltLockGenerator::applyPairForces(float dt) {
    const float phaseLock = k3 * k3;

    for (int i = 0; i < activeBlobCount; ++i) {
        for (int j = i + 1; j < activeBlobCount; ++j) {
            glm::vec2 delta = blobs[j].position - blobs[i].position;
            const float dist2 = delta.x * delta.x + delta.y * delta.y;
            if (dist2 < 0.000001f) continue;

            const float dist = std::sqrt(dist2);
            const float reach = (blobs[i].radius + blobs[j].radius) * 0.82f;
            if (dist >= reach) continue;

            const glm::vec2 normal = delta / dist;
            const float overlap = 1.0f - dist / reach;
            const float force = overlap * lerp(0.020f, 0.008f, phaseLock);
            blobs[i].velocity -= normal * force * dt * 30.0f;
            blobs[j].velocity += normal * force * dt * 30.0f;
        }
    }
}

void MeltLockGenerator::updateBlobs(float dt) {
    const float phaseLock = k3 * k3;
    const float flow = lerp(0.30f, 2.45f, k2 * k2);
    const float pressure = pressureEnvelope;

    applyPairForces(dt);

    for (int i = 0; i < activeBlobCount; ++i) {
        Blob& blob = blobs[i];
        blob.phase += dt * blob.phaseSpeed * flow;

        const float sideA = std::sin(blob.phase * 0.91f + i * 1.73f);
        const float sideB = std::sin(blob.phase * 0.33f + i * 0.47f);
        const float desiredX = 0.5f + sideA * 0.24f * blob.drift
                                     + sideB * 0.075f;

        const float horizontalPull = lerp(0.19f, 0.07f, phaseLock);
        blob.velocity.x += (desiredX - blob.position.x)
                         * horizontalPull * dt * flow;

        const float desiredVertical = blob.polarity
                                    * lerp(0.026f, 0.082f, k2)
                                    * randomRange(0.985f, 1.015f);
        blob.velocity.y += (desiredVertical - blob.velocity.y)
                         * (1.0f - std::exp(-dt * 0.85f * flow));

        // The liquid state allows curved convection. During phase lock the
        // weaker velocity axis is progressively suppressed, producing more
        // mechanical vertical/horizontal trajectories before pixel treatment.
        if (phaseLock > 0.0f) {
            if (std::abs(blob.velocity.x) > std::abs(blob.velocity.y)) {
                blob.velocity.y *= lerp(1.0f, 0.18f, phaseLock);
            } else {
                blob.velocity.x *= lerp(1.0f, 0.18f, phaseLock);
            }
        }

        const float damping = std::exp(-dt * lerp(0.55f, 1.55f, phaseLock));
        blob.velocity *= damping;
        blob.position += blob.velocity * dt * flow;

        if (phaseLock > 0.0f) {
            const float gx = 12.0f;
            const float gy = 8.0f;
            const glm::vec2 snapped(
                std::floor(blob.position.x * gx + 0.5f) / gx,
                std::floor(blob.position.y * gy + 0.5f) / gy);
            blob.position += (snapped - blob.position)
                           * (1.0f - std::exp(-dt * 2.2f * phaseLock));
        }

        if (blob.polarity < 0.0f && blob.position.y < -0.18f) {
            blob.polarity = 1.0f;
            blob.position.y = -0.16f;
            blob.position.x = randomRange(0.12f, 0.88f);
            blob.velocity.y = randomRange(0.018f, 0.050f);
            blob.targetRadius *= randomRange(0.86f, 1.16f);
        } else if (blob.polarity > 0.0f && blob.position.y > 1.18f) {
            blob.polarity = -1.0f;
            blob.position.y = 1.16f;
            blob.position.x = randomRange(0.12f, 0.88f);
            blob.velocity.y = -randomRange(0.018f, 0.050f);
            blob.targetRadius *= randomRange(0.86f, 1.16f);
        }

        if (blob.position.x < -0.20f) blob.position.x = 1.20f;
        if (blob.position.x > 1.20f) blob.position.x = -0.20f;

        const float breathing = 1.0f
                              + std::sin(blob.phase * 0.72f + i) * 0.10f
                              + pressure * (0.035f + 0.012f * (i % 3));
        const float radiusTarget = std::max(0.035f, blob.targetRadius * breathing);
        blob.radius += (radiusTarget - blob.radius)
                     * (1.0f - std::exp(-dt * 1.9f));

        blob.squash = std::sin(blob.phase * 0.57f + i * 0.8f)
                    * lerp(0.24f, 0.06f, phaseLock);
    }
}

void MeltLockGenerator::step(float dt,
                             float energy,
                             bool trigFired,
                             float knob1,
                             float knob2,
                             float knob3,
                             float knob4,
                             float knob5) {
    prepare();
    if (!allocated || !shaderReady) return;

    if (!(dt > 0.0f) || dt > 0.20f) dt = 1.0f / 30.0f;

    k1 = clamp01(knob1);
    k2 = clamp01(knob2);
    k3 = clamp01(knob3);
    k4 = clamp01(knob4);
    k5 = clamp01(knob5);
    energy = clamp01(energy);

    updateBlobCount();

    smoothEnergy += (energy - smoothEnergy)
                  * (1.0f - std::exp(-dt * 7.0f));
    const float strike = clamp01((smoothEnergy - 0.08f) * 1.65f);
    pressureEnvelope = std::max(pressureEnvelope, strike);
    pressureEnvelope *= std::exp(-dt * 4.7f);
    fractureEnvelope *= std::exp(-dt * 5.2f);

    const bool trigEdge = trigFired && !previousTrig;
    previousTrig = trigFired;
    if (trigEdge && !blobs.empty()) {
        pressureEnvelope = 1.0f;
        fractureEnvelope = 1.0f;

        const int first = randomInt(0, activeBlobCount - 1);
        blobs[first].polarity *= -1.0f;
        blobs[first].velocity.x += randomRange(-0.11f, 0.11f);
        blobs[first].velocity.y += randomRange(-0.07f, 0.07f);

        if (activeBlobCount > 5) {
            const int second = (first + randomInt(1, activeBlobCount - 1))
                             % activeBlobCount;
            blobs[second].velocity.x += randomRange(-0.08f, 0.08f);
        }
    }

    updateBlobs(dt);
    renderField();
}

void MeltLockGenerator::drawBlob(const Blob& blob, int index) {
    const float px = blob.position.x * static_cast<float>(TEX_W);
    const float py = blob.position.y * static_cast<float>(TEX_H);
    const float radiusPx = std::max(3.0f,
                                    blob.radius * static_cast<float>(TEX_H));

    const float phaseLock = k3 * k3;
    float width = radiusPx * 2.0f * (1.0f + blob.squash);
    float height = radiusPx * 2.0f * (1.0f - blob.squash * 0.55f);

    if (phaseLock > 0.0f) {
        const float stepPx = lerp(1.0f, 6.0f, phaseLock);
        width = std::floor(width / stepPx + 0.5f) * stepPx;
        height = std::floor(height / stepPx + 0.5f) * stepPx;
    }

    const float audioScale = 1.0f
                           + pressureEnvelope * (0.025f + 0.008f * (index % 4));
    width *= audioScale;
    height *= audioScale;

    // Concentric additive ellipses create a compact scalar source before the
    // fixed-cost separable blur. More rings would not materially improve the
    // low-resolution field on the Pi.
    static const float scales[] = {1.42f, 1.22f, 1.08f, 0.96f, 0.84f};
    static const int alphas[] = {18, 30, 48, 78, 116};

    for (int ring = 0; ring < 5; ++ring) {
        const float fractureJitter = fractureEnvelope
                                   * ((index + ring) % 2 == 0 ? 1.0f : -1.0f)
                                   * 1.4f;
        ofSetColor(255, 255, 255, alphas[ring]);
        ofDrawEllipse(px + fractureJitter,
                      py,
                      width * scales[ring],
                      height * scales[ring]);
    }
}

void MeltLockGenerator::renderBlurPass(ofFbo& source,
                                       ofFbo& destination,
                                       float dx,
                                       float dy) {
    destination.begin();
    ofClear(0, 0, 0, 255);
    ofSetColor(255);

    blurShader.begin();
    blurShader.setUniformTexture("tex0", source.getTexture(), 0);
    blurShader.setUniform2f("u_direction", dx, dy);
    source.draw(0.0f, 0.0f,
                static_cast<float>(TEX_W),
                static_cast<float>(TEX_H));
    blurShader.end();

    destination.end();
}

void MeltLockGenerator::renderField() {
    if (!allocated || !shaderReady) return;

    sourceFbo.begin();
    ofClear(0, 0, 0, 255);
    ofPushStyle();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofSetCircleResolution(32);

    for (int i = 0; i < activeBlobCount; ++i) {
        drawBlob(blobs[i], i);
    }

    ofPopStyle();
    sourceFbo.end();

    const float phaseLock = k3 * k3;
    const float softnessPx = lerp(3.6f, 1.15f, phaseLock)
                           * lerp(0.82f, 1.28f, k5);

    renderBlurPass(sourceFbo,
                   blurPingFbo,
                   softnessPx / static_cast<float>(TEX_W),
                   0.0f);
    renderBlurPass(blurPingFbo,
                   blurPongFbo,
                   0.0f,
                   softnessPx / static_cast<float>(TEX_H));
}
