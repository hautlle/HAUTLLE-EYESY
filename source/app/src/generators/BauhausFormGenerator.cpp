#include "BauhausFormGenerator.h"

#include "ofGraphics.h"
#include "ofLog.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float PI_F = 3.14159265358979323846f;

float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}
}

void BauhausFormGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &BauhausFormGenerator::trampolinePrepare, 1);
    lua_setglobal(L, "bauhaus_prepare");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &BauhausFormGenerator::trampolineReset, 1);
    lua_setglobal(L, "bauhaus_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &BauhausFormGenerator::trampolineStep, 1);
    lua_setglobal(L, "bauhaus_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &BauhausFormGenerator::trampolineRecompose, 1);
    lua_setglobal(L, "bauhaus_recompose");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &BauhausFormGenerator::trampolineIsReady, 1);
    lua_setglobal(L, "bauhaus_is_ready");
}

int BauhausFormGenerator::trampolinePrepare(lua_State* L) {
    BauhausFormGenerator* self = static_cast<BauhausFormGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->prepare();
    return 0;
}

int BauhausFormGenerator::trampolineReset(lua_State* L) {
    BauhausFormGenerator* self = static_cast<BauhausFormGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

// bauhaus_step(dt, energy, trig, k1, k2, k3, k4, k5)
int BauhausFormGenerator::trampolineStep(lua_State* L) {
    BauhausFormGenerator* self = static_cast<BauhausFormGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));

    float dt = static_cast<float>(lua_tonumber(L, 1));
    float energy = static_cast<float>(lua_tonumber(L, 2));
    bool trigFired = lua_toboolean(L, 3) != 0;
    float a1 = static_cast<float>(lua_tonumber(L, 4));
    float a2 = static_cast<float>(lua_tonumber(L, 5));
    float a3 = static_cast<float>(lua_tonumber(L, 6));
    float a4 = static_cast<float>(lua_tonumber(L, 7));
    float a5 = static_cast<float>(lua_tonumber(L, 8));

    if (self) self->step(dt, energy, trigFired, a1, a2, a3, a4, a5);
    return 0;
}

int BauhausFormGenerator::trampolineRecompose(lua_State* L) {
    BauhausFormGenerator* self = static_cast<BauhausFormGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));

    // bauhaus_recompose() defaults to a hard poster reissue. Passing false
    // explicitly requests the same smooth target transition used by timed
    // mutations.
    bool immediate = lua_gettop(L) < 1 || lua_toboolean(L, 1) != 0;
    if (self) self->recompose(immediate);
    return 0;
}

int BauhausFormGenerator::trampolineIsReady(lua_State* L) {
    BauhausFormGenerator* self = static_cast<BauhausFormGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self && self->allocated && self->initialized);
    return 1;
}

float BauhausFormGenerator::clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float BauhausFormGenerator::random01() const {
    // Xorshift32: deterministic, tiny, and independent from global rand().
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return static_cast<float>(x & 0x00ffffffu) / 16777215.0f;
}

int BauhausFormGenerator::randomInt(int minInclusive, int maxInclusive) const {
    if (maxInclusive <= minInclusive) return minInclusive;
    int span = maxInclusive - minInclusive + 1;
    int value = static_cast<int>(random01() * static_cast<float>(span));
    if (value >= span) value = span - 1;
    return minInclusive + value;
}

void BauhausFormGenerator::ensureAllocated() {
    if (allocated) return;

    ofFboSettings settings;
    settings.width = TEX_W;
    settings.height = TEX_H;
    settings.internalformat = GL_RGBA;
    settings.useDepth = false;
    settings.useStencil = false;
    settings.textureTarget = GL_TEXTURE_2D;

    compositionFbo.allocate(settings);
    if (!compositionFbo.isAllocated()) {
        ofLogError("BRIDGE")
            << "BauhausFormGenerator: failed to allocate composition FBO "
            << TEX_W << "x" << TEX_H;
        return;
    }

    compositionFbo.begin();
    ofClear(0, 0, 0, 255);
    compositionFbo.end();

    allocated = true;
    ofLogNotice("BRIDGE")
        << "BauhausFormGenerator: composition FBO allocated "
        << TEX_W << "x" << TEX_H;
}


// MODE GPU RECLAIM 01
void BauhausFormGenerator::releaseGpuResources() {
    if (compositionFbo.isAllocated()) {
        compositionFbo.clear();
        ofLogNotice("BRIDGE")
            << "MODE_RECLAIM: Bauhaus compositionFbo released";
    }

    allocated = false;
    initialized = false;
}

ofTexture& BauhausFormGenerator::getTexture() {
    ensureAllocated();
    return compositionFbo.getTexture();
}

void BauhausFormGenerator::prepare() {
    ensureAllocated();
    if (!allocated) return;

    if (!initialized) {
        requestedFormCount = 3 + static_cast<int>(std::round(k1 * 17.0f));
        activeFormCount = requestedFormCount;
        paletteIndex = std::max(0, std::min(5, static_cast<int>(k4 * 6.0f)));
        buildComposition(true);
        initialized = true;
        renderToFbo();
    }
}

void BauhausFormGenerator::reset() {
    ensureAllocated();
    if (!allocated) return;

    // Reset means a genuinely new issue rather than returning to a fixed seed.
    compositionSerial++;
    rngState ^= 0x9e3779b9u + compositionSerial * 0x85ebca6bu;
    transitionTimer = 0.0f;
    countChangeCooldown = 0.0f;
    pressEnvelope = 0.0f;
    smoothEnergy = 0.0f;
    previousTrig = false;
    requestedFormCount = 3 + static_cast<int>(std::round(k1 * 17.0f));
    activeFormCount = requestedFormCount;
    buildComposition(true);
    initialized = true;
    renderToFbo();

    ofLogNotice("BRIDGE")
        << "bauhaus_reset() called, composition serial=" << compositionSerial;
}

void BauhausFormGenerator::recompose(bool immediate) {
    prepare();
    if (!allocated) return;

    compositionSerial++;
    buildComposition(immediate);
    transitionTimer = 0.0f;
    majorMutationCountdown = randomInt(4, 8);

    ofLogNotice("BRIDGE")
        << "BauhausFormGenerator: new composition serial=" << compositionSerial
        << " forms=" << activeFormCount
        << " palette=" << paletteIndex;
}

void BauhausFormGenerator::step(float dt,
                                float energy,
                                bool trigFired,
                                float knob1,
                                float knob2,
                                float knob3,
                                float knob4,
                                float knob5) {
    prepare();
    if (!allocated) return;

    // Prevent a long stall or malformed Lua value from creating a giant jump.
    if (!(dt > 0.0f) || dt > 0.20f) dt = 1.0f / 30.0f;

    k1 = clamp01(knob1);
    k2 = clamp01(knob2);
    k3 = clamp01(knob3);
    k4 = clamp01(knob4);
    k5 = clamp01(knob5);
    energy = clamp01(energy);

    int nextPalette = static_cast<int>(k4 * 6.0f);
    paletteIndex = std::max(0, std::min(5, nextPalette));

    requestedFormCount = 3 + static_cast<int>(std::round(k1 * 17.0f));
    requestedFormCount = std::max(3, std::min(MAX_FORMS, requestedFormCount));

    smoothEnergy += (energy - smoothEnergy) * (1.0f - std::exp(-dt * 8.0f));
    float audioStrike = clamp01((smoothEnergy - 0.12f) * 1.55f);
    pressEnvelope = std::max(pressEnvelope, audioStrike);
    pressEnvelope *= std::exp(-dt * 6.5f);

    bool trigEdge = trigFired && !previousTrig;
    previousTrig = trigFired;
    if (trigEdge) {
        pressEnvelope = 1.0f;
        // A trigger is the printing-press strike: issue the new composition
        // immediately. Autonomous changes below continue to transition smoothly.
        recompose(true);
    }

    countChangeCooldown = std::max(0.0f, countChangeCooldown - dt);
    if (requestedFormCount != activeFormCount && countChangeCooldown <= 0.0f) {
        activeFormCount = requestedFormCount;
        buildComposition(false);
        countChangeCooldown = 0.35f;
    }

    transitionTimer += dt;
    float mutationInterval = lerpf(16.0f, 1.45f, k2 * k2);
    if (transitionTimer >= mutationInterval) {
        transitionTimer = 0.0f;
        if (majorMutationCountdown <= 0) {
            recompose(false);
        } else {
            mutateComposition();
            majorMutationCountdown--;
        }
    }

    updateForms(dt);
    renderToFbo();
}

glm::vec2 BauhausFormGenerator::gridPosition(bool allowOffscreen) {
    int gx = randomInt(1, 11);
    int gy = randomInt(1, 7);

    float x = static_cast<float>(gx) / 12.0f;
    float y = static_cast<float>(gy) / 8.0f;

    if (allowOffscreen && random01() < 0.24f * k3) {
        int side = randomInt(0, 3);
        float spill = lerpf(0.02f, 0.13f, k3);
        if (side == 0) x = -spill;
        if (side == 1) x = 1.0f + spill;
        if (side == 2) y = -spill;
        if (side == 3) y = 1.0f + spill;
    }

    return glm::vec2(x, y);
}

glm::vec2 BauhausFormGenerator::randomSizeForType(ShapeType type,
                                                   bool dominant) const {
    float a = random01();
    float b = random01();

    switch (type) {
        case SHAPE_BAR:
            return glm::vec2(lerpf(dominant ? 0.46f : 0.20f,
                                   dominant ? 0.86f : 0.58f, a),
                             lerpf(0.018f, dominant ? 0.085f : 0.052f, b));
        case SHAPE_CIRCLE:
        case SHAPE_RING:
        case SHAPE_ARC: {
            float d = lerpf(dominant ? 0.24f : 0.07f,
                            dominant ? 0.48f : 0.24f, a);
            return glm::vec2(d, d * (16.0f / 9.0f));
        }
        case SHAPE_TRIANGLE:
            return glm::vec2(lerpf(dominant ? 0.24f : 0.08f,
                                   dominant ? 0.52f : 0.25f, a),
                             lerpf(dominant ? 0.24f : 0.08f,
                                   dominant ? 0.52f : 0.28f, b));
        case SHAPE_CROSS:
            return glm::vec2(lerpf(0.09f, dominant ? 0.30f : 0.20f, a),
                             lerpf(0.13f, dominant ? 0.42f : 0.28f, b));
        case SHAPE_RECT:
        default:
            return glm::vec2(lerpf(dominant ? 0.26f : 0.07f,
                                   dominant ? 0.66f : 0.34f, a),
                             lerpf(dominant ? 0.20f : 0.045f,
                                   dominant ? 0.58f : 0.28f, b));
    }
}

float BauhausFormGenerator::randomRotation() const {
    if (k3 < 0.26f) {
        return randomInt(0, 1) == 0 ? 0.0f : 90.0f;
    }

    if (k3 < 0.62f) {
        static const float rotations[] = {-15.0f, 0.0f, 0.0f, 0.0f, 15.0f, 90.0f};
        return rotations[randomInt(0, 5)];
    }

    static const float ruptured[] = {
        -45.0f, -30.0f, -15.0f, 0.0f, 0.0f, 15.0f, 30.0f, 45.0f, 90.0f
    };
    return ruptured[randomInt(0, 8)];
}

void BauhausFormGenerator::assignTarget(Form& form,
                                        int index,
                                        bool preserveType) {
    bool dominant = index == 0;

    if (!preserveType) {
        if (index == 0) {
            ShapeType dominantTypes[] = {SHAPE_RECT, SHAPE_CIRCLE, SHAPE_RING, SHAPE_BAR};
            form.type = dominantTypes[randomInt(0, 3)];
        } else if (index == 1) {
            form.type = SHAPE_BAR;
        } else if (index == 2) {
            ShapeType counterTypes[] = {SHAPE_CIRCLE, SHAPE_RECT, SHAPE_TRIANGLE};
            form.type = counterTypes[randomInt(0, 2)];
        } else {
            int pick = randomInt(0, 99);
            if (pick < 30) form.type = SHAPE_RECT;
            else if (pick < 48) form.type = SHAPE_CIRCLE;
            else if (pick < 60) form.type = SHAPE_RING;
            else if (pick < 76) form.type = SHAPE_BAR;
            else if (pick < 86) form.type = SHAPE_TRIANGLE;
            else if (pick < 94) form.type = SHAPE_CROSS;
            else form.type = SHAPE_ARC;
        }
    }

    if (index == 0) {
        static const glm::vec2 anchors[] = {
            glm::vec2(0.28f, 0.36f), glm::vec2(0.70f, 0.34f),
            glm::vec2(0.33f, 0.68f), glm::vec2(0.69f, 0.66f)
        };
        form.targetPosition = anchors[randomInt(0, 3)];
    } else if (index == 1) {
        form.targetPosition = glm::vec2(
            randomInt(0, 1) == 0 ? 0.50f : lerpf(0.26f, 0.74f, random01()),
            lerpf(0.18f, 0.82f, random01()));
    } else if (index == 2 && !forms.empty()) {
        const glm::vec2& dominantPos = forms[0].targetPosition;
        form.targetPosition = glm::vec2(
            dominantPos.x < 0.5f ? lerpf(0.66f, 0.86f, random01())
                                 : lerpf(0.14f, 0.34f, random01()),
            dominantPos.y < 0.5f ? lerpf(0.62f, 0.84f, random01())
                                 : lerpf(0.16f, 0.38f, random01()));
    } else {
        form.targetPosition = gridPosition(true);
    }

    form.targetSize = randomSizeForType(form.type, dominant);
    form.targetRotation = randomRotation();

    if (index == 0) {
        form.colorRole = random01() < 0.66f ? 1 : 2;
        form.layer = 1;
    } else if (index == 1) {
        form.colorRole = 1;
        form.layer = 2;
    } else if (index == 2) {
        form.colorRole = random01() < 0.70f ? 2 : 3;
        form.layer = 3;
    } else {
        float rolePick = random01();
        if (rolePick < 0.54f) form.colorRole = 1;
        else if (rolePick < 0.77f) form.colorRole = 2;
        else if (rolePick < 0.91f) form.colorRole = 3;
        else form.colorRole = 4;
        form.layer = 4 + index;
    }

    form.audioPolarity = random01() < 0.5f ? -1.0f : 1.0f;
}

void BauhausFormGenerator::buildComposition(bool immediate) {
    ensureAllocated();
    if (!allocated) return;

    int oldCount = static_cast<int>(forms.size());
    forms.resize(activeFormCount);

    for (int i = 0; i < activeFormCount; ++i) {
        bool newlyAdded = i >= oldCount;
        assignTarget(forms[i], i, false);

        if (immediate) {
            forms[i].position = forms[i].targetPosition;
            forms[i].size = forms[i].targetSize;
            forms[i].rotation = forms[i].targetRotation;
        } else if (newlyAdded) {
            forms[i].position = forms[i].targetPosition;
            if (forms[i].position.x < 0.5f) forms[i].position.x = -0.18f;
            else forms[i].position.x = 1.18f;
            forms[i].size = forms[i].targetSize * 0.15f;
            forms[i].rotation = forms[i].targetRotation + 45.0f * forms[i].audioPolarity;
        }
    }

    majorMutationCountdown = randomInt(4, 8);
}

void BauhausFormGenerator::mutateComposition() {
    if (forms.empty()) return;

    int moving = 1;
    if (k3 > 0.42f) moving++;
    if (k3 > 0.78f) moving++;
    moving = std::min(moving, static_cast<int>(forms.size()));

    std::vector<int> touched;
    touched.reserve(moving);

    while (static_cast<int>(touched.size()) < moving) {
        int index = randomInt(0, static_cast<int>(forms.size()) - 1);
        if (std::find(touched.begin(), touched.end(), index) != touched.end()) continue;
        touched.push_back(index);

        bool preserveType = random01() < lerpf(0.82f, 0.40f, k3);
        assignTarget(forms[index], index, preserveType);
    }
}

float BauhausFormGenerator::approachAngle(float current,
                                          float target,
                                          float amount) {
    float delta = std::fmod(target - current + 540.0f, 360.0f) - 180.0f;
    return current + delta * amount;
}

void BauhausFormGenerator::updateForms(float dt) {
    float speed = lerpf(0.70f, 4.20f, k2);
    float amount = 1.0f - std::exp(-dt * speed);

    for (Form& form : forms) {
        form.position += (form.targetPosition - form.position) * amount;
        form.size += (form.targetSize - form.size) * amount;
        form.rotation = approachAngle(form.rotation, form.targetRotation, amount);
    }
}

ofColor BauhausFormGenerator::colorForRole(int role) const {
    // role 0 = field; 1 = structure; 2 = dominant accent;
    // role 3 = secondary accent; 4 = neutral/support.
    static const ofColor palettes[6][5] = {
        {
            ofColor(239, 232, 210), ofColor(23, 24, 23),
            ofColor(202, 43, 35), ofColor(31, 72, 143), ofColor(231, 181, 31)
        },
        {
            ofColor(245, 244, 238), ofColor(18, 18, 18),
            ofColor(210, 34, 30), ofColor(98, 98, 94), ofColor(224, 218, 201)
        },
        {
            ofColor(235, 227, 204), ofColor(17, 21, 27),
            ofColor(22, 59, 135), ofColor(79, 112, 174), ofColor(188, 49, 37)
        },
        {
            ofColor(201, 202, 197), ofColor(24, 25, 25),
            ofColor(221, 85, 32), ofColor(22, 147, 164), ofColor(236, 228, 201)
        },
        {
            ofColor(14, 15, 15), ofColor(230, 222, 199),
            ofColor(207, 39, 32), ofColor(111, 112, 106), ofColor(246, 242, 225)
        },
        {
            ofColor(221, 211, 185), ofColor(43, 44, 41),
            ofColor(171, 55, 42), ofColor(61, 91, 116), ofColor(184, 157, 76)
        }
    };

    int p = std::max(0, std::min(5, paletteIndex));
    int r = std::max(0, std::min(4, role));
    return palettes[p][r];
}

void BauhausFormGenerator::drawForm(const Form& form, int index) {
    float x = form.position.x * static_cast<float>(TEX_W);
    float y = form.position.y * static_cast<float>(TEX_H);
    float w = std::max(1.0f, form.size.x * static_cast<float>(TEX_W));
    float h = std::max(1.0f, form.size.y * static_cast<float>(TEX_H));

    float strikeScale = 1.0f + pressEnvelope * (0.008f + 0.008f * (index % 3))
                                  * form.audioPolarity;
    float pressure = k5 * 0.75f + pressEnvelope * 0.25f;
    float stroke = 1.25f + pressure * 5.0f;

    ofPushMatrix();
    ofTranslate(x, y);
    ofRotateDeg(form.rotation);
    ofScale(strikeScale, strikeScale);

    ofColor shapeColor = colorForRole(form.colorRole);
    ofSetColor(shapeColor);
    ofFill();

    switch (form.type) {
        case SHAPE_CIRCLE: {
            float radius = 0.5f * std::min(w, h);
            ofDrawCircle(0.0f, 0.0f, radius);
            break;
        }
        case SHAPE_RING: {
            float radius = 0.5f * std::min(w, h);
            ofDrawCircle(0.0f, 0.0f, radius);
            ofSetColor(colorForRole(0));
            float inner = std::max(0.0f, radius - stroke * lerpf(2.2f, 5.4f, k5));
            ofDrawCircle(0.0f, 0.0f, inner);
            break;
        }
        case SHAPE_BAR:
            ofDrawRectangle(-w * 0.5f, -h * 0.5f, w, h);
            break;
        case SHAPE_TRIANGLE:
            ofDrawTriangle(-w * 0.5f, h * 0.5f,
                           0.0f, -h * 0.5f,
                           w * 0.5f, h * 0.5f);
            break;
        case SHAPE_CROSS: {
            float arm = std::max(2.0f, std::min(w, h) * 0.27f);
            ofDrawRectangle(-arm * 0.5f, -h * 0.5f, arm, h);
            ofDrawRectangle(-w * 0.5f, -arm * 0.5f, w, arm);
            break;
        }
        case SHAPE_ARC: {
            float radius = 0.5f * std::min(w, h);
            ofPath arc;
            arc.setFilled(false);
            arc.setStrokeColor(shapeColor);
            arc.setStrokeWidth(stroke * lerpf(1.6f, 3.2f, k5));
            float startAngle = index % 2 == 0 ? 18.0f : 198.0f;
            float endAngle = startAngle + lerpf(150.0f, 285.0f, k3);
            arc.arc(0.0f, 0.0f, radius, radius, startAngle, endAngle);
            arc.draw();
            break;
        }
        case SHAPE_RECT:
        default:
            ofDrawRectangle(-w * 0.5f, -h * 0.5f, w, h);
            break;
    }

    ofPopMatrix();
}

void BauhausFormGenerator::renderToFbo() {
    if (!allocated || !compositionFbo.isAllocated()) return;

    compositionFbo.begin();
    ofClear(colorForRole(0));

    ofPushStyle();
    ofEnableAlphaBlending();
    ofSetCircleResolution(48);

    for (int i = 0; i < static_cast<int>(forms.size()); ++i) {
        drawForm(forms[i], i);
    }

    // Fine structural registration rules. They are generated from the same
    // deterministic state but do not become independent moving objects.
    ofSetColor(colorForRole(1), 170);
    float ruleWeight = 1.0f + k5 * 2.2f;
    ofSetLineWidth(ruleWeight);

    if (!forms.empty()) {
        const glm::vec2 p = forms[0].position;
        float px = p.x * static_cast<float>(TEX_W);
        float py = p.y * static_cast<float>(TEX_H);
        float reach = lerpf(42.0f, 150.0f, 1.0f - k1 * 0.45f);
        ofDrawLine(px - reach, py, px + reach * 0.34f, py);
        ofDrawLine(px, py - reach * 0.28f, px, py + reach * 0.55f);
    }

    ofPopStyle();
    compositionFbo.end();
}
