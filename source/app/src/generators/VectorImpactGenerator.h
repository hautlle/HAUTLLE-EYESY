#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"

#include <vector>

class VectorImpactGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

    // MODE GPU RECLAIM 02
    void releaseGpuResources();

private:
    struct Edge {
        int a = 0;
        int b = 0;
    };

    struct Face {
        int a = 0;
        int b = 0;
        int c = 0;
    };

    struct ProjectedPoint {
        glm::vec2 screen = glm::vec2(0.0f, 0.0f);
        float cameraDepth = 1.0f;
        float perspectiveScale = 1.0f;
        bool valid = false;
    };

    struct ImpactSpring {
        float displacement = 0.0f;
        float velocity = 0.0f;
    };

    struct FaceDrawItem {
        ProjectedPoint a;
        ProjectedPoint b;
        ProjectedPoint c;
        float sortDepth = 0.0f;
        ofColor color;
    };

    struct EdgeDrawItem {
        ProjectedPoint a;
        ProjectedPoint b;
        float sortDepth = 0.0f;
        float width = 1.0f;
        ofColor color;
    };

    static int trampolinePrepare(lua_State* L);
    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineIsReady(lua_State* L);

    void prepare();
    void step(float dt,
              float lowBand,
              float midBand,
              float highBand,
              float energy,
              bool trigFired,
              float knob1,
              float knob2,
              float knob3,
              float knob4,
              float knob5);
    void draw(float width, float height);

    void ensureAllocated();
    void buildGeometry();
    void renderFrame();

    void buildCurrentAnchors(std::vector<glm::vec3>& anchors,
                             float& starWeight,
                             float& cuboctaWeight,
                             float& icosaWeight) const;
    void buildLayerPositions(const std::vector<glm::vec3>& anchors,
                             std::vector<glm::vec3>& core,
                             std::vector<glm::vec3>& shell,
                             std::vector<glm::vec3>& impact) const;

    void appendFamilyFaces(const std::vector<glm::vec3>& positions,
                           const std::vector<Face>& faces,
                           float familyWeight,
                           float baseAlpha,
                           float gray,
                           std::vector<FaceDrawItem>& drawItems) const;
    void appendFamilyEdges(const std::vector<glm::vec3>& positions,
                           const std::vector<Edge>& edges,
                           float familyWeight,
                           float baseWidth,
                           float baseAlpha,
                           float gray,
                           int stride,
                           std::vector<EdgeDrawItem>& drawItems) const;
    void appendImpactSpokes(const std::vector<glm::vec3>& shell,
                            const std::vector<glm::vec3>& impact,
                            std::vector<EdgeDrawItem>& drawItems) const;
    void appendVertexAccents(const std::vector<glm::vec3>& positions,
                             ofMesh& mesh) const;

    ProjectedPoint projectPoint(const glm::vec3& point) const;
    static void appendTriangle(ofMesh& mesh,
                               const ProjectedPoint& a,
                               const ProjectedPoint& b,
                               const ProjectedPoint& c,
                               const ofColor& color);
    static void appendRibbon(ofMesh& mesh,
                             const ProjectedPoint& a,
                             const ProjectedPoint& b,
                             float width,
                             const ofColor& color);

    static std::vector<Edge> deriveShortestEdges(
        const std::vector<glm::vec3>& positions);
    static std::vector<Face> deriveTriangularFaces(
        int vertexCount,
        const std::vector<Edge>& edges);
    static bool edgeExists(const std::vector<Edge>& edges, int a, int b);

    static glm::vec3 rotateAroundAxis(const glm::vec3& point,
                                      const glm::vec3& axis,
                                      float angle);
    static glm::vec3 safeNormalize(const glm::vec3& value,
                                   const glm::vec3& fallback);
    static glm::vec3 mixVec3(const glm::vec3& a,
                             const glm::vec3& b,
                             float amount);
    static float clamp01(float value);
    static float smooth01(float value);
    static float lerp(float a, float b, float amount);
    static float response(float dt, float rate);
    static float updateEnvelope(float current,
                                float target,
                                float dt,
                                float attackRate,
                                float releaseRate);
    static void updateSpring(ImpactSpring& spring,
                             float dt,
                             float stiffness,
                             float damping);
    static void exciteSpring(ImpactSpring& spring, float amount);

    static const int TEX_W = 640;
    static const int TEX_H = 360;

    ofFbo renderFbo;
    bool allocated = false;
    bool allocationFailed = false;
    bool geometryReady = false;
    bool initialized = false;

    std::vector<glm::vec3> starPositions;
    std::vector<glm::vec3> cuboctaPositions;
    std::vector<glm::vec3> icosaPositions;

    std::vector<Edge> starEdges;
    std::vector<Edge> cuboctaEdges;
    std::vector<Edge> icosaEdges;

    std::vector<Face> starFaces;
    std::vector<Face> cuboctaFaces;
    std::vector<Face> icosaFaces;

    float elapsed = 0.0f;

    float k1 = 0.50f;
    float k2 = 0.44f;
    float k3 = 0.52f;
    float k4 = 0.50f;
    float k5 = 0.64f;

    float lowEnvelope = 0.0f;
    float midEnvelope = 0.0f;
    float highEnvelope = 0.0f;
    float energyEnvelope = 0.0f;
    float slowEnergy = 0.0f;

    ImpactSpring lowImpact;
    ImpactSpring midImpact;
    ImpactSpring highImpact;
    ImpactSpring broadImpact;

    float onsetCooldown = 0.0f;
    bool previousTrig = false;
};
