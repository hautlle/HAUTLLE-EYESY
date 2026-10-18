#include "VectorImpactGenerator.h"

#include "ofGraphics.h"
#include "ofLog.h"

#include <algorithm>
#include <cmath>

namespace {
float lengthSquared(const glm::vec3& value) {
    return glm::dot(value, value);
}

int roundedAlpha(float value) {
    return static_cast<int>(std::max(0.0f, std::min(255.0f, value)) + 0.5f);
}
}

// C++11 requires out-of-class definitions when these integral constants are
// passed by reference to logging streams.
const int VectorImpactGenerator::TEX_W;
const int VectorImpactGenerator::TEX_H;

void VectorImpactGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VectorImpactGenerator::trampolinePrepare, 1);
    lua_setglobal(L, "vectorimpact_prepare");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VectorImpactGenerator::trampolineReset, 1);
    lua_setglobal(L, "vectorimpact_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VectorImpactGenerator::trampolineStep, 1);
    lua_setglobal(L, "vectorimpact_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VectorImpactGenerator::trampolineDraw, 1);
    lua_setglobal(L, "vectorimpact_draw");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &VectorImpactGenerator::trampolineIsReady, 1);
    lua_setglobal(L, "vectorimpact_is_ready");
}

int VectorImpactGenerator::trampolinePrepare(lua_State* L) {
    VectorImpactGenerator* self = static_cast<VectorImpactGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->prepare();
    return 0;
}

int VectorImpactGenerator::trampolineReset(lua_State* L) {
    VectorImpactGenerator* self = static_cast<VectorImpactGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

// vectorimpact_step(dt, low, mid, high, energy, trig, k1, k2, k3, k4, k5)
int VectorImpactGenerator::trampolineStep(lua_State* L) {
    VectorImpactGenerator* self = static_cast<VectorImpactGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));

    const float dt = static_cast<float>(lua_tonumber(L, 1));
    const float low = static_cast<float>(lua_tonumber(L, 2));
    const float mid = static_cast<float>(lua_tonumber(L, 3));
    const float high = static_cast<float>(lua_tonumber(L, 4));
    const float energy = static_cast<float>(lua_tonumber(L, 5));
    const bool trigFired = lua_toboolean(L, 6) != 0;
    const float a1 = static_cast<float>(lua_tonumber(L, 7));
    const float a2 = static_cast<float>(lua_tonumber(L, 8));
    const float a3 = static_cast<float>(lua_tonumber(L, 9));
    const float a4 = static_cast<float>(lua_tonumber(L, 10));
    const float a5 = static_cast<float>(lua_tonumber(L, 11));

    if (self) {
        self->step(dt,
                   low,
                   mid,
                   high,
                   energy,
                   trigFired,
                   a1,
                   a2,
                   a3,
                   a4,
                   a5);
    }
    return 0;
}

int VectorImpactGenerator::trampolineDraw(lua_State* L) {
    VectorImpactGenerator* self = static_cast<VectorImpactGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    const float width = static_cast<float>(lua_tonumber(L, 1));
    const float height = static_cast<float>(lua_tonumber(L, 2));
    if (self) self->draw(width, height);
    return 0;
}

int VectorImpactGenerator::trampolineIsReady(lua_State* L) {
    VectorImpactGenerator* self = static_cast<VectorImpactGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L,
                    self && self->allocated && self->geometryReady && self->initialized);
    return 1;
}

float VectorImpactGenerator::clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float VectorImpactGenerator::smooth01(float value) {
    const float x = clamp01(value);
    return x * x * (3.0f - 2.0f * x);
}

float VectorImpactGenerator::lerp(float a, float b, float amount) {
    return a + (b - a) * amount;
}

glm::vec3 VectorImpactGenerator::mixVec3(const glm::vec3& a,
                                          const glm::vec3& b,
                                          float amount) {
    return a + (b - a) * amount;
}

float VectorImpactGenerator::response(float dt, float rate) {
    return 1.0f - std::exp(-std::max(0.0f, dt) * std::max(0.0f, rate));
}

float VectorImpactGenerator::updateEnvelope(float current,
                                             float target,
                                             float dt,
                                             float attackRate,
                                             float releaseRate) {
    const float rate = target > current ? attackRate : releaseRate;
    return current + (target - current) * response(dt, rate);
}

glm::vec3 VectorImpactGenerator::safeNormalize(const glm::vec3& value,
                                                const glm::vec3& fallback) {
    const float len2 = lengthSquared(value);
    if (len2 < 0.0000001f) return fallback;
    return value / std::sqrt(len2);
}

glm::vec3 VectorImpactGenerator::rotateAroundAxis(const glm::vec3& point,
                                                   const glm::vec3& axis,
                                                   float angle) {
    const glm::vec3 n = safeNormalize(axis, glm::vec3(0.0f, 0.0f, 1.0f));
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return point * c
         + glm::cross(n, point) * s
         + n * glm::dot(n, point) * (1.0f - c);
}

void VectorImpactGenerator::updateSpring(ImpactSpring& spring,
                                          float dt,
                                          float stiffness,
                                          float damping) {
    const float safeDt = std::max(0.0f, std::min(0.05f, dt));
    const float acceleration = -stiffness * spring.displacement
                             - damping * spring.velocity;
    spring.velocity += acceleration * safeDt;
    spring.displacement += spring.velocity * safeDt;

    spring.displacement = std::max(-1.35f, std::min(1.35f, spring.displacement));
    spring.velocity = std::max(-8.0f, std::min(8.0f, spring.velocity));
}

void VectorImpactGenerator::exciteSpring(ImpactSpring& spring, float amount) {
    spring.velocity += std::max(0.0f, amount);
}

bool VectorImpactGenerator::edgeExists(const std::vector<Edge>& edges,
                                       int a,
                                       int b) {
    const int low = std::min(a, b);
    const int high = std::max(a, b);
    for (const Edge& edge : edges) {
        if (edge.a == low && edge.b == high) return true;
    }
    return false;
}

std::vector<VectorImpactGenerator::Edge>
VectorImpactGenerator::deriveShortestEdges(
    const std::vector<glm::vec3>& positions) {
    std::vector<Edge> edges;
    if (positions.size() < 2) return edges;

    float shortest = 1000.0f;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (std::size_t j = i + 1; j < positions.size(); ++j) {
            const float distance = std::sqrt(lengthSquared(positions[j] - positions[i]));
            if (distance > 0.001f) shortest = std::min(shortest, distance);
        }
    }

    if (shortest >= 999.0f) return edges;
    const float threshold = shortest * 1.055f;

    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (std::size_t j = i + 1; j < positions.size(); ++j) {
            const float distance = std::sqrt(lengthSquared(positions[j] - positions[i]));
            if (distance <= threshold) {
                Edge edge;
                edge.a = static_cast<int>(i);
                edge.b = static_cast<int>(j);
                edges.push_back(edge);
            }
        }
    }
    return edges;
}

std::vector<VectorImpactGenerator::Face>
VectorImpactGenerator::deriveTriangularFaces(
    int vertexCount,
    const std::vector<Edge>& edges) {
    std::vector<Face> faces;
    for (int a = 0; a < vertexCount; ++a) {
        for (int b = a + 1; b < vertexCount; ++b) {
            if (!edgeExists(edges, a, b)) continue;
            for (int c = b + 1; c < vertexCount; ++c) {
                if (edgeExists(edges, a, c) && edgeExists(edges, b, c)) {
                    Face face;
                    face.a = a;
                    face.b = b;
                    face.c = c;
                    faces.push_back(face);
                }
            }
        }
    }
    return faces;
}

void VectorImpactGenerator::buildGeometry() {
    geometryReady = false;

    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;

    std::vector<glm::vec3> icoRaw;
    icoRaw.push_back(glm::vec3(0.0f, 1.0f, phi));
    icoRaw.push_back(glm::vec3(0.0f, -1.0f, phi));
    icoRaw.push_back(glm::vec3(0.0f, 1.0f, -phi));
    icoRaw.push_back(glm::vec3(0.0f, -1.0f, -phi));
    icoRaw.push_back(glm::vec3(1.0f, phi, 0.0f));
    icoRaw.push_back(glm::vec3(-1.0f, phi, 0.0f));
    icoRaw.push_back(glm::vec3(1.0f, -phi, 0.0f));
    icoRaw.push_back(glm::vec3(-1.0f, -phi, 0.0f));
    icoRaw.push_back(glm::vec3(phi, 0.0f, 1.0f));
    icoRaw.push_back(glm::vec3(phi, 0.0f, -1.0f));
    icoRaw.push_back(glm::vec3(-phi, 0.0f, 1.0f));
    icoRaw.push_back(glm::vec3(-phi, 0.0f, -1.0f));

    icosaPositions.clear();
    for (const glm::vec3& value : icoRaw) {
        icosaPositions.push_back(safeNormalize(value, glm::vec3(1.0f, 0.0f, 0.0f)));
    }

    std::vector<glm::vec3> cuboRaw;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            cuboRaw.push_back(safeNormalize(glm::vec3(static_cast<float>(sx),
                                                       static_cast<float>(sy),
                                                       0.0f),
                                                glm::vec3(1.0f, 0.0f, 0.0f)));
            cuboRaw.push_back(safeNormalize(glm::vec3(static_cast<float>(sx),
                                                       0.0f,
                                                       static_cast<float>(sy)),
                                                glm::vec3(1.0f, 0.0f, 0.0f)));
            cuboRaw.push_back(safeNormalize(glm::vec3(0.0f,
                                                       static_cast<float>(sx),
                                                       static_cast<float>(sy)),
                                                glm::vec3(1.0f, 0.0f, 0.0f)));
        }
    }

    cuboctaPositions.assign(icosaPositions.size(), glm::vec3(0.0f));
    std::vector<bool> cuboUsed(cuboRaw.size(), false);
    for (std::size_t i = 0; i < icosaPositions.size(); ++i) {
        int bestIndex = -1;
        float bestDot = -1000.0f;
        for (std::size_t j = 0; j < cuboRaw.size(); ++j) {
            if (cuboUsed[j]) continue;
            const float alignment = glm::dot(icosaPositions[i], cuboRaw[j]);
            if (alignment > bestDot) {
                bestDot = alignment;
                bestIndex = static_cast<int>(j);
            }
        }
        if (bestIndex < 0) bestIndex = 0;
        cuboctaPositions[i] = cuboRaw[bestIndex];
        cuboUsed[bestIndex] = true;
    }

    std::vector<glm::vec3> cubeCorners;
    cubeCorners.push_back(safeNormalize(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(-1.0f, 1.0f, -1.0f), glm::vec3(1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(-1.0f, -1.0f, 1.0f), glm::vec3(1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(-1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(-1.0f, 1.0f, 1.0f), glm::vec3(1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(1.0f, -1.0f, 1.0f), glm::vec3(1.0f)));
    cubeCorners.push_back(safeNormalize(glm::vec3(1.0f, 1.0f, -1.0f), glm::vec3(1.0f)));

    starPositions.assign(icosaPositions.size(), glm::vec3(0.0f));
    for (std::size_t i = 0; i < icosaPositions.size(); ++i) {
        int bestIndex = 0;
        float bestDot = -1000.0f;
        for (std::size_t j = 0; j < cubeCorners.size(); ++j) {
            const float alignment = glm::dot(icosaPositions[i], cubeCorners[j]);
            if (alignment > bestDot) {
                bestDot = alignment;
                bestIndex = static_cast<int>(j);
            }
        }
        starPositions[i] = cubeCorners[bestIndex];
    }

    std::vector<int> cornerRepresentatives(cubeCorners.size(), -1);
    std::vector<bool> representativeUsed(icosaPositions.size(), false);
    for (std::size_t corner = 0; corner < cubeCorners.size(); ++corner) {
        int bestIndex = -1;
        float bestDot = -1000.0f;
        for (std::size_t i = 0; i < icosaPositions.size(); ++i) {
            if (representativeUsed[i]) continue;
            const float alignment = glm::dot(icosaPositions[i], cubeCorners[corner]);
            if (alignment > bestDot) {
                bestDot = alignment;
                bestIndex = static_cast<int>(i);
            }
        }
        if (bestIndex < 0) bestIndex = static_cast<int>(corner % icosaPositions.size());
        cornerRepresentatives[corner] = bestIndex;
        representativeUsed[bestIndex] = true;
        starPositions[bestIndex] = cubeCorners[corner];
    }

    starEdges.clear();
    const int tetraA[4] = {0, 1, 2, 3};
    const int tetraB[4] = {4, 5, 6, 7};
    for (int set = 0; set < 2; ++set) {
        const int* tetra = set == 0 ? tetraA : tetraB;
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                Edge edge;
                edge.a = std::min(cornerRepresentatives[tetra[i]],
                                  cornerRepresentatives[tetra[j]]);
                edge.b = std::max(cornerRepresentatives[tetra[i]],
                                  cornerRepresentatives[tetra[j]]);
                starEdges.push_back(edge);
            }
        }
    }

    starFaces.clear();
    const int tetraFacePattern[4][3] = {
        {0, 1, 2},
        {0, 1, 3},
        {0, 2, 3},
        {1, 2, 3}
    };
    for (int set = 0; set < 2; ++set) {
        const int* tetra = set == 0 ? tetraA : tetraB;
        for (int f = 0; f < 4; ++f) {
            Face face;
            face.a = cornerRepresentatives[tetra[tetraFacePattern[f][0]]];
            face.b = cornerRepresentatives[tetra[tetraFacePattern[f][1]]];
            face.c = cornerRepresentatives[tetra[tetraFacePattern[f][2]]];
            starFaces.push_back(face);
        }
    }

    cuboctaEdges = deriveShortestEdges(cuboctaPositions);
    icosaEdges = deriveShortestEdges(icosaPositions);
    cuboctaFaces = deriveTriangularFaces(static_cast<int>(cuboctaPositions.size()),
                                         cuboctaEdges);
    icosaFaces = deriveTriangularFaces(static_cast<int>(icosaPositions.size()),
                                       icosaEdges);

    geometryReady = !starPositions.empty()
                 && !cuboctaPositions.empty()
                 && !icosaPositions.empty()
                 && !starEdges.empty()
                 && !cuboctaEdges.empty()
                 && !icosaEdges.empty();

    if (geometryReady) {
        ofLogNotice("BRIDGE")
            << "VectorImpactGenerator: geometry ready "
            << "star=" << starEdges.size()
            << " cubocta=" << cuboctaEdges.size()
            << " icosa=" << icosaEdges.size();
    } else {
        ofLogError("BRIDGE")
            << "VectorImpactGenerator: geometry construction failed";
    }
}


// MODE GPU RECLAIM 02
void VectorImpactGenerator::releaseGpuResources() {
    if (renderFbo.isAllocated()) {
        renderFbo.clear();
        ofLogNotice("BRIDGE")
            << "MODE_RECLAIM: Vector Impact renderFbo released 640x360";
    }

    allocated = false;
    allocationFailed = false;
    initialized = false;
}

void VectorImpactGenerator::ensureAllocated() {
    if (allocated || allocationFailed) return;

    ofFboSettings settings;
    settings.width = TEX_W;
    settings.height = TEX_H;
    settings.internalformat = GL_RGBA;
    settings.useDepth = false;
    settings.useStencil = false;
    settings.textureTarget = GL_TEXTURE_2D;
    settings.numSamples = 0;

    // A failed ofFbo allocation can leave partial GL objects behind.  Start a
    // retry from an explicitly empty object and clear it again on failure.
    if (renderFbo.isAllocated()) {
        renderFbo.clear();
    }
    renderFbo.allocate(settings);
    if (!renderFbo.isAllocated()) {
        allocationFailed = true;
        renderFbo.clear();
        ofLogError("BRIDGE")
            << "VectorImpactGenerator: failed to allocate "
            << TEX_W << "x" << TEX_H
            << " FBO; partial resources released; retries latched until vectorimpact_reset()";
        return;
    }

    renderFbo.begin();
    ofClear(0, 0, 0, 255);
    renderFbo.end();

    allocated = true;
    ofLogNotice("BRIDGE")
        << "VectorImpactGenerator: vector FBO allocated "
        << TEX_W << "x" << TEX_H;
}

void VectorImpactGenerator::prepare() {
    ensureAllocated();
    if (!geometryReady) buildGeometry();
    if (!allocated || !geometryReady) return;

    if (!initialized) {
        initialized = true;
        renderFrame();
    }
}

void VectorImpactGenerator::reset() {
    if (allocationFailed && !allocated) allocationFailed = false;
    ensureAllocated();
    if (!geometryReady) buildGeometry();
    if (!allocated || !geometryReady) return;

    elapsed = 0.0f;
    lowEnvelope = 0.0f;
    midEnvelope = 0.0f;
    highEnvelope = 0.0f;
    energyEnvelope = 0.0f;
    slowEnergy = 0.0f;
    lowImpact = ImpactSpring();
    midImpact = ImpactSpring();
    highImpact = ImpactSpring();
    broadImpact = ImpactSpring();
    onsetCooldown = 0.0f;
    previousTrig = false;
    initialized = true;
    renderFrame();

    ofLogNotice("BRIDGE") << "vectorimpact_reset() called";
}

void VectorImpactGenerator::step(float dt,
                                 float lowBand,
                                 float midBand,
                                 float highBand,
                                 float energy,
                                 bool trigFired,
                                 float knob1,
                                 float knob2,
                                 float knob3,
                                 float knob4,
                                 float knob5) {
    prepare();
    if (!allocated || !geometryReady) return;

    const float safeDt = std::max(1.0f / 120.0f, std::min(0.05f, dt));
    elapsed += safeDt;

    k1 = clamp01(knob1);
    k2 = clamp01(knob2);
    k3 = clamp01(knob3);
    k4 = clamp01(knob4);
    k5 = clamp01(knob5);

    lowEnvelope = updateEnvelope(lowEnvelope,
                                 clamp01(lowBand),
                                 safeDt,
                                 22.0f,
                                 5.0f);
    midEnvelope = updateEnvelope(midEnvelope,
                                 clamp01(midBand),
                                 safeDt,
                                 26.0f,
                                 6.5f);
    highEnvelope = updateEnvelope(highEnvelope,
                                  clamp01(highBand),
                                  safeDt,
                                  34.0f,
                                  9.0f);
    energyEnvelope = updateEnvelope(energyEnvelope,
                                    clamp01(energy),
                                    safeDt,
                                    24.0f,
                                    4.5f);
    slowEnergy += (energyEnvelope - slowEnergy) * response(safeDt, 2.0f);

    onsetCooldown = std::max(0.0f, onsetCooldown - safeDt);
    const bool trigEdge = trigFired && !previousTrig;
    previousTrig = trigFired;

    const float novelty = std::max(0.0f,
                                   energyEnvelope - slowEnergy * 1.10f - 0.018f);
    const bool detectedOnset = novelty > 0.055f
                            && energyEnvelope > 0.10f
                            && onsetCooldown <= 0.0f;

    if (trigEdge || detectedOnset) {
        const float amount = std::min(1.45f,
                                      0.35f
                                    + energyEnvelope * 0.72f
                                    + novelty * 3.2f);

        const float lowScore = lowEnvelope * 1.12f;
        const float midScore = midEnvelope;
        const float highScore = highEnvelope * 0.92f;

        if (lowScore >= midScore && lowScore >= highScore) {
            exciteSpring(lowImpact, amount * 2.15f);
        } else if (midScore >= highScore) {
            exciteSpring(midImpact, amount * 2.30f);
        } else {
            exciteSpring(highImpact, amount * 2.50f);
        }

        exciteSpring(broadImpact, amount * 1.20f);
        onsetCooldown = 0.078f;
    }

    updateSpring(lowImpact, safeDt, 72.0f, 10.5f);
    updateSpring(midImpact, safeDt, 112.0f, 13.8f);
    updateSpring(highImpact, safeDt, 178.0f, 18.0f);
    updateSpring(broadImpact, safeDt, 92.0f, 12.0f);

    renderFrame();
}

void VectorImpactGenerator::buildCurrentAnchors(
    std::vector<glm::vec3>& anchors,
    float& starWeight,
    float& cuboctaWeight,
    float& icosaWeight) const {
    anchors.resize(icosaPositions.size());

    const float topology = clamp01(k1) * 2.0f;
    if (topology <= 1.0f) {
        const float t = smooth01(topology);
        starWeight = 1.0f - t;
        cuboctaWeight = t;
        icosaWeight = 0.0f;
        for (std::size_t i = 0; i < anchors.size(); ++i) {
            anchors[i] = mixVec3(starPositions[i], cuboctaPositions[i], t);
        }
    } else {
        const float t = smooth01(topology - 1.0f);
        starWeight = 0.0f;
        cuboctaWeight = 1.0f - t;
        icosaWeight = t;
        for (std::size_t i = 0; i < anchors.size(); ++i) {
            anchors[i] = mixVec3(cuboctaPositions[i], icosaPositions[i], t);
        }
    }
}

void VectorImpactGenerator::buildLayerPositions(
    const std::vector<glm::vec3>& anchors,
    std::vector<glm::vec3>& core,
    std::vector<glm::vec3>& shell,
    std::vector<glm::vec3>& impact) const {
    core.resize(anchors.size());
    shell.resize(anchors.size());
    impact.resize(anchors.size());

    const float radial = smooth01(k2);
    const float articulation = smooth01(k3);
    const float impactDepth = smooth01(k5);
    const float axisSeparation = smooth01(k4);

    const glm::vec3 coreAxis = safeNormalize(glm::vec3(1.0f, 1.0f, 1.0f),
                                             glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 sacredShellAxis = safeNormalize(
        glm::vec3(1.0f, 1.618034f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 sacredImpactAxis = safeNormalize(
        glm::vec3(1.618034f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 shellAxis = safeNormalize(
        mixVec3(coreAxis, sacredShellAxis, axisSeparation),
        coreAxis);
    const glm::vec3 impactAxis = safeNormalize(
        mixVec3(coreAxis, sacredImpactAxis, axisSeparation),
        coreAxis);

    // K4 begins with all layers sharing one axis/phase. As it rises, the shell
    // and impact layer move onto golden-ratio axes with independent rates and
    // golden-angle phase offsets. This makes K4 a real alignment-to-interlock
    // position morph rather than a generic global rotation.
    const float commonRotation = elapsed * 0.085f;
    const float goldenAngle = 2.39996323f;
    const float quietCore = commonRotation + elapsed * 0.020f * axisSeparation;
    const float quietShell = commonRotation
                           + axisSeparation * (-elapsed * 0.159f + goldenAngle);
    const float quietImpact = commonRotation
                            + axisSeparation * (elapsed * 0.052f + goldenAngle * 2.0f);

    const float broadScale = 1.0f
                           + broadImpact.displacement * impactDepth * 0.045f;
    const float coreScale = lerp(0.78f, 0.92f, radial)
                          * broadScale
                          * (1.0f + lowImpact.displacement * impactDepth * 0.075f);
    const float shellScale = lerp(0.82f, 1.30f, radial)
                           * (1.0f + midEnvelope * impactDepth * 0.035f);
    const float impactScale = lerp(0.88f, 1.58f, radial);

    for (std::size_t i = 0; i < anchors.size(); ++i) {
        const float parity = (i % 2 == 0) ? 1.0f : -1.0f;
        const float triad = static_cast<float>(static_cast<int>(i % 3) - 1);
        const float quartet = static_cast<float>(static_cast<int>(i % 4) - 1.5f);

        glm::vec3 c = anchors[i] * coreScale;
        c.z *= 1.0f - lowEnvelope * impactDepth * 0.14f;
        c.x *= 1.0f + lowEnvelope * impactDepth * 0.035f;
        c.y *= 1.0f + lowEnvelope * impactDepth * 0.035f;
        c.z += lowImpact.displacement * impactDepth * 0.085f;
        c = rotateAroundAxis(c, coreAxis, quietCore);
        c = rotateAroundAxis(c,
                             glm::vec3(0.0f, 1.0f, 0.0f),
                             elapsed * 0.028f);
        core[i] = c;

        glm::vec3 s = anchors[i] * shellScale;
        const glm::vec3 hingeAxis = (i % 3 == 0)
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : ((i % 3 == 1)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(0.0f, 0.0f, 1.0f));

        const glm::vec3 pivot = s * 0.36f;
        glm::vec3 hinged = s - pivot;
        const float hingeAngle = parity
            * (articulation * lerp(0.20f, 0.86f, radial)
               + midEnvelope * impactDepth * 0.28f
               + midImpact.displacement * impactDepth * 0.62f)
            * (0.72f + static_cast<float>((i * 17) % 5) * 0.075f);
        hinged = rotateAroundAxis(hinged, hingeAxis, hingeAngle);
        s = pivot + hinged;

        s.x += s.y * articulation * parity * 0.12f;
        s.y += s.z * articulation * triad * 0.075f;
        s.z += articulation * quartet * 0.035f;
        s = rotateAroundAxis(s, shellAxis, quietShell);
        shell[i] = s;

        glm::vec3 p = anchors[i] * impactScale;
        const float highRotation = parity
            * (highEnvelope * impactDepth * 0.24f
               + highImpact.displacement * impactDepth * 0.72f);
        p = rotateAroundAxis(p,
                             hingeAxis,
                             highRotation);
        p = rotateAroundAxis(p, impactAxis, quietImpact);

        const glm::vec3 outward = safeNormalize(p,
                                                safeNormalize(anchors[i],
                                                              glm::vec3(1.0f, 0.0f, 0.0f)));
        const float spike = impactDepth
            * (0.030f
               + highEnvelope * 0.16f
               + std::abs(highImpact.displacement) * 0.30f)
            * (0.68f + static_cast<float>((i * 23) % 7) * 0.075f);
        p += outward * spike;
        impact[i] = p;
    }
}

VectorImpactGenerator::ProjectedPoint
VectorImpactGenerator::projectPoint(const glm::vec3& point) const {
    ProjectedPoint projected;
    const float cameraDistance = 3.35f;
    const float depth = cameraDistance - point.z;
    if (depth < 0.28f) return projected;

    const float focal = static_cast<float>(TEX_H) * 0.93f;
    const float perspective = focal / depth;

    projected.screen.x = static_cast<float>(TEX_W) * 0.5f
                       + point.x * perspective;
    projected.screen.y = static_cast<float>(TEX_H) * 0.5f
                       - point.y * perspective;
    projected.cameraDepth = depth;
    projected.perspectiveScale = perspective;
    projected.valid = true;
    return projected;
}

void VectorImpactGenerator::appendTriangle(ofMesh& mesh,
                                           const ProjectedPoint& a,
                                           const ProjectedPoint& b,
                                           const ProjectedPoint& c,
                                           const ofColor& color) {
    if (!a.valid || !b.valid || !c.valid) return;
    mesh.addVertex(glm::vec3(a.screen.x, a.screen.y, 0.0f));
    mesh.addColor(color);
    mesh.addVertex(glm::vec3(b.screen.x, b.screen.y, 0.0f));
    mesh.addColor(color);
    mesh.addVertex(glm::vec3(c.screen.x, c.screen.y, 0.0f));
    mesh.addColor(color);
}

void VectorImpactGenerator::appendRibbon(ofMesh& mesh,
                                         const ProjectedPoint& a,
                                         const ProjectedPoint& b,
                                         float width,
                                         const ofColor& color) {
    if (!a.valid || !b.valid) return;
    const glm::vec2 delta = b.screen - a.screen;
    const float len2 = glm::dot(delta, delta);
    if (len2 < 0.01f) return;

    const float inverseLength = 1.0f / std::sqrt(len2);
    const glm::vec2 normal(-delta.y * inverseLength,
                            delta.x * inverseLength);
    const glm::vec2 offset = normal * std::max(0.35f, width * 0.5f);

    const glm::vec2 a0 = a.screen - offset;
    const glm::vec2 a1 = a.screen + offset;
    const glm::vec2 b0 = b.screen - offset;
    const glm::vec2 b1 = b.screen + offset;

    mesh.addVertex(glm::vec3(a0.x, a0.y, 0.0f)); mesh.addColor(color);
    mesh.addVertex(glm::vec3(b0.x, b0.y, 0.0f)); mesh.addColor(color);
    mesh.addVertex(glm::vec3(b1.x, b1.y, 0.0f)); mesh.addColor(color);

    mesh.addVertex(glm::vec3(a0.x, a0.y, 0.0f)); mesh.addColor(color);
    mesh.addVertex(glm::vec3(b1.x, b1.y, 0.0f)); mesh.addColor(color);
    mesh.addVertex(glm::vec3(a1.x, a1.y, 0.0f)); mesh.addColor(color);
}

void VectorImpactGenerator::appendFamilyFaces(
    const std::vector<glm::vec3>& positions,
    const std::vector<Face>& faces,
    float familyWeight,
    float baseAlpha,
    float gray,
    std::vector<FaceDrawItem>& drawItems) const {
    if (familyWeight <= 0.001f) return;

    const glm::vec3 lightDirection = safeNormalize(glm::vec3(-0.35f, 0.58f, 1.0f),
                                                    glm::vec3(0.0f, 0.0f, 1.0f));

    for (std::size_t i = 0; i < faces.size(); ++i) {
        if ((i % 3) == 2 && baseAlpha < 34.0f) continue;

        const Face& face = faces[i];
        if (face.a < 0 || face.b < 0 || face.c < 0
            || face.a >= static_cast<int>(positions.size())
            || face.b >= static_cast<int>(positions.size())
            || face.c >= static_cast<int>(positions.size())) {
            continue;
        }

        const glm::vec3& wa = positions[face.a];
        const glm::vec3& wb = positions[face.b];
        const glm::vec3& wc = positions[face.c];
        const glm::vec3 normal = safeNormalize(glm::cross(wb - wa, wc - wa),
                                               glm::vec3(0.0f, 0.0f, 1.0f));
        const float lighting = 0.28f
                             + std::max(0.0f, glm::dot(normal, lightDirection)) * 0.72f;
        const float facing = std::abs(normal.z);

        FaceDrawItem item;
        item.a = projectPoint(wa);
        item.b = projectPoint(wb);
        item.c = projectPoint(wc);
        if (!item.a.valid || !item.b.valid || !item.c.valid) continue;
        item.sortDepth = (item.a.cameraDepth
                        + item.b.cameraDepth
                        + item.c.cameraDepth) / 3.0f;
        item.color = ofColor(roundedAlpha(gray * lighting),
                             roundedAlpha(gray * lighting),
                             roundedAlpha(gray * lighting),
                             roundedAlpha(baseAlpha
                                          * familyWeight
                                          * lerp(0.55f, 1.0f, facing)));
        drawItems.push_back(item);
    }
}

void VectorImpactGenerator::appendFamilyEdges(
    const std::vector<glm::vec3>& positions,
    const std::vector<Edge>& edges,
    float familyWeight,
    float baseWidth,
    float baseAlpha,
    float gray,
    int stride,
    std::vector<EdgeDrawItem>& drawItems) const {
    if (familyWeight <= 0.001f) return;
    const int safeStride = std::max(1, stride);

    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (static_cast<int>(i % safeStride) != 0) continue;
        const Edge& edge = edges[i];
        if (edge.a < 0 || edge.b < 0
            || edge.a >= static_cast<int>(positions.size())
            || edge.b >= static_cast<int>(positions.size())) {
            continue;
        }

        EdgeDrawItem item;
        item.a = projectPoint(positions[edge.a]);
        item.b = projectPoint(positions[edge.b]);
        if (!item.a.valid || !item.b.valid) continue;
        item.sortDepth = (item.a.cameraDepth + item.b.cameraDepth) * 0.5f;
        const float depthScale = clamp01(3.8f / std::max(1.0f, item.sortDepth));
        item.width = baseWidth * lerp(0.72f, 1.18f, depthScale);
        item.color = ofColor(roundedAlpha(gray),
                             roundedAlpha(gray),
                             roundedAlpha(gray),
                             roundedAlpha(baseAlpha * familyWeight));
        drawItems.push_back(item);
    }
}

void VectorImpactGenerator::appendImpactSpokes(
    const std::vector<glm::vec3>& shell,
    const std::vector<glm::vec3>& impact,
    std::vector<EdgeDrawItem>& drawItems) const {
    const float activity = clamp01(0.08f
                                 + highEnvelope * 0.68f
                                 + std::abs(highImpact.displacement) * 0.72f
                                 + energyEnvelope * 0.12f);
    const float depth = smooth01(k5);
    if (activity * depth < 0.015f) return;

    for (std::size_t i = 0; i < shell.size() && i < impact.size(); ++i) {
        if ((i % 2) == 1 && activity < 0.34f) continue;
        EdgeDrawItem item;
        item.a = projectPoint(shell[i]);
        item.b = projectPoint(impact[i]);
        if (!item.a.valid || !item.b.valid) continue;
        item.sortDepth = (item.a.cameraDepth + item.b.cameraDepth) * 0.5f;
        item.width = lerp(0.70f, 1.65f, activity);
        const float gray = lerp(150.0f, 255.0f, activity);
        item.color = ofColor(roundedAlpha(gray),
                             roundedAlpha(gray),
                             roundedAlpha(gray),
                             roundedAlpha(245.0f * activity * depth));
        drawItems.push_back(item);
    }
}

void VectorImpactGenerator::appendVertexAccents(
    const std::vector<glm::vec3>& positions,
    ofMesh& mesh) const {
    const float activity = clamp01(highEnvelope * 0.62f
                                 + std::abs(highImpact.displacement) * 0.88f);
    const float depth = smooth01(k5);
    if (activity * depth < 0.045f) return;

    for (std::size_t i = 0; i < positions.size(); ++i) {
        if ((i % 3) != 0 && activity < 0.55f) continue;
        const ProjectedPoint p = projectPoint(positions[i]);
        if (!p.valid) continue;
        const float radius = lerp(1.1f, 3.4f, activity) * depth;
        const ofColor color(255,
                            255,
                            255,
                            roundedAlpha(210.0f * activity * depth));

        ProjectedPoint top = p;
        ProjectedPoint right = p;
        ProjectedPoint bottom = p;
        ProjectedPoint left = p;
        top.screen.y -= radius;
        right.screen.x += radius;
        bottom.screen.y += radius;
        left.screen.x -= radius;

        appendTriangle(mesh, top, right, bottom, color);
        appendTriangle(mesh, top, bottom, left, color);
    }
}

void VectorImpactGenerator::renderFrame() {
    if (!allocated || !geometryReady) return;

    std::vector<glm::vec3> anchors;
    std::vector<glm::vec3> core;
    std::vector<glm::vec3> shell;
    std::vector<glm::vec3> impact;
    float starWeight = 0.0f;
    float cuboctaWeight = 0.0f;
    float icosaWeight = 0.0f;

    buildCurrentAnchors(anchors, starWeight, cuboctaWeight, icosaWeight);
    buildLayerPositions(anchors, core, shell, impact);

    std::vector<FaceDrawItem> faceItems;
    std::vector<EdgeDrawItem> edgeItems;

    const float impactDepth = smooth01(k5);
    const float shellFacetAlpha = 10.0f
        + 28.0f * smooth01(k3)
        + 34.0f * clamp01(midEnvelope * impactDepth
                         + std::abs(midImpact.displacement) * impactDepth);
    const float coreFacetAlpha = 13.0f
        + 20.0f * energyEnvelope
        + 18.0f * std::abs(lowImpact.displacement) * impactDepth;

    appendFamilyFaces(core,
                      starFaces,
                      starWeight,
                      coreFacetAlpha,
                      88.0f,
                      faceItems);
    appendFamilyFaces(core,
                      cuboctaFaces,
                      cuboctaWeight,
                      coreFacetAlpha * 0.82f,
                      82.0f,
                      faceItems);
    appendFamilyFaces(core,
                      icosaFaces,
                      icosaWeight,
                      coreFacetAlpha * 0.72f,
                      76.0f,
                      faceItems);

    appendFamilyFaces(shell,
                      starFaces,
                      starWeight,
                      shellFacetAlpha,
                      74.0f,
                      faceItems);
    appendFamilyFaces(shell,
                      cuboctaFaces,
                      cuboctaWeight,
                      shellFacetAlpha * 0.90f,
                      69.0f,
                      faceItems);
    appendFamilyFaces(shell,
                      icosaFaces,
                      icosaWeight,
                      shellFacetAlpha * 0.78f,
                      64.0f,
                      faceItems);

    const float coreWidth = lerp(1.10f, 2.45f,
                                 clamp01(0.18f
                                       + energyEnvelope * 0.44f
                                       + std::abs(lowImpact.displacement) * impactDepth * 0.65f));
    const float shellWidth = lerp(0.72f, 1.85f,
                                  clamp01(0.12f
                                        + midEnvelope * 0.48f
                                        + std::abs(midImpact.displacement) * impactDepth * 0.70f));
    const float impactEdgeActivity = clamp01(highEnvelope * 0.58f
                                           + std::abs(highImpact.displacement) * 0.80f);

    appendFamilyEdges(core,
                      starEdges,
                      starWeight,
                      coreWidth,
                      245.0f,
                      236.0f,
                      1,
                      edgeItems);
    appendFamilyEdges(core,
                      cuboctaEdges,
                      cuboctaWeight,
                      coreWidth,
                      232.0f,
                      230.0f,
                      1,
                      edgeItems);
    appendFamilyEdges(core,
                      icosaEdges,
                      icosaWeight,
                      coreWidth,
                      222.0f,
                      226.0f,
                      1,
                      edgeItems);

    appendFamilyEdges(shell,
                      starEdges,
                      starWeight,
                      shellWidth,
                      164.0f,
                      160.0f,
                      1,
                      edgeItems);
    appendFamilyEdges(shell,
                      cuboctaEdges,
                      cuboctaWeight,
                      shellWidth,
                      154.0f,
                      150.0f,
                      1,
                      edgeItems);
    appendFamilyEdges(shell,
                      icosaEdges,
                      icosaWeight,
                      shellWidth,
                      142.0f,
                      146.0f,
                      1,
                      edgeItems);

    const int impactStride = impactEdgeActivity > 0.56f ? 1 : 2;
    appendFamilyEdges(impact,
                      starEdges,
                      starWeight,
                      lerp(0.62f, 1.48f, impactEdgeActivity),
                      160.0f * impactEdgeActivity * impactDepth,
                      245.0f,
                      impactStride,
                      edgeItems);
    appendFamilyEdges(impact,
                      cuboctaEdges,
                      cuboctaWeight,
                      lerp(0.58f, 1.36f, impactEdgeActivity),
                      150.0f * impactEdgeActivity * impactDepth,
                      238.0f,
                      impactStride,
                      edgeItems);
    appendFamilyEdges(impact,
                      icosaEdges,
                      icosaWeight,
                      lerp(0.54f, 1.28f, impactEdgeActivity),
                      142.0f * impactEdgeActivity * impactDepth,
                      232.0f,
                      impactStride,
                      edgeItems);

    appendImpactSpokes(shell, impact, edgeItems);

    std::sort(faceItems.begin(),
              faceItems.end(),
              [](const FaceDrawItem& a, const FaceDrawItem& b) {
                  return a.sortDepth > b.sortDepth;
              });
    std::sort(edgeItems.begin(),
              edgeItems.end(),
              [](const EdgeDrawItem& a, const EdgeDrawItem& b) {
                  return a.sortDepth > b.sortDepth;
              });

    ofMesh faceMesh;
    faceMesh.setMode(OF_PRIMITIVE_TRIANGLES);
    for (const FaceDrawItem& item : faceItems) {
        appendTriangle(faceMesh, item.a, item.b, item.c, item.color);
    }

    ofMesh edgeMesh;
    edgeMesh.setMode(OF_PRIMITIVE_TRIANGLES);
    for (const EdgeDrawItem& item : edgeItems) {
        appendRibbon(edgeMesh,
                     item.a,
                     item.b,
                     item.width,
                     item.color);
    }
    appendVertexAccents(impact, edgeMesh);

    renderFbo.begin();
    ofClear(0, 0, 0, 255);

    // The surrounding EYESY draw path may enter Lua with depth testing on.
    // Save and restore both states explicitly so this direct native renderer
    // neither depends on nor leaks the engine's current GL state.
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);

    ofPushStyle();
    ofDisableDepthTest();
    ofEnableAlphaBlending();
    faceMesh.draw();
    edgeMesh.draw();
    ofPopStyle();

    if (!blendWasEnabled) ofDisableAlphaBlending();
    if (depthWasEnabled) ofEnableDepthTest();
    renderFbo.end();
}

void VectorImpactGenerator::draw(float width, float height) {
    prepare();
    if (!allocated || !initialized) return;

    const float outputWidth = width > 1.0f
        ? width
        : static_cast<float>(ofGetWidth());
    const float outputHeight = height > 1.0f
        ? height
        : static_cast<float>(ofGetHeight());

    ofPushStyle();
    ofSetColor(255, 255, 255, 255);
    renderFbo.draw(0.0f, 0.0f, outputWidth, outputHeight);
    ofPopStyle();
}
