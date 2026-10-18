#include "MarkovMoshGenerator.h"
#include "ofLog.h"
#include <algorithm>
#include <cmath>

namespace {
float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float smooth01(float v) {
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

ofVec2f lerpVec(const ofVec2f& a, const ofVec2f& b, float t) {
    return a + (b - a) * t;
}
}

void MarkovMoshGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MarkovMoshGenerator::trampolineReset, 1);
    lua_setglobal(L, "markovmosh_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MarkovMoshGenerator::trampolineStep, 1);
    lua_setglobal(L, "markovmosh_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &MarkovMoshGenerator::trampolineMeta, 1);
    lua_setglobal(L, "markovmosh_meta");
}

int MarkovMoshGenerator::trampolineReset(lua_State* L) {
    MarkovMoshGenerator* self =
        static_cast<MarkovMoshGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

int MarkovMoshGenerator::trampolineStep(lua_State* L) {
    MarkovMoshGenerator* self =
        static_cast<MarkovMoshGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));

    if (!self) return 0;

    self->step(
        (float)luaL_optnumber(L, 1, 0.0),
        (float)luaL_optnumber(L, 2, 0.0),
        (float)luaL_optnumber(L, 3, 0.0),
        (float)luaL_optnumber(L, 4, 0.0),
        (float)luaL_optnumber(L, 5, 0.0),
        lua_toboolean(L, 6) != 0,
        (float)luaL_optnumber(L, 7, 0.5),
        (float)luaL_optnumber(L, 8, 0.4),
        (float)luaL_optnumber(L, 9, 0.45),
        (float)luaL_optnumber(L, 10, 0.25),
        (float)luaL_optnumber(L, 11, 0.7)
    );
    return 0;
}

int MarkovMoshGenerator::trampolineMeta(lua_State* L) {
    MarkovMoshGenerator* self =
        static_cast<MarkovMoshGenerator*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushnumber(L, self ? self->getPackedMeta() : 0.0);
    return 1;
}


// MODE GPU RECLAIM 02
void MarkovMoshGenerator::releaseGpuResources() {
    if (texture.isAllocated()) {
        texture.clear();
        ofLogNotice("BRIDGE")
            << "MODE_RECLAIM: Markov Mosh texture released 320x180";
    }

    allocated = false;
}

void MarkovMoshGenerator::ensureAllocated() {
    if (allocated) return;

    pixels.allocate(TEX_W, TEX_H, OF_PIXELS_RGBA);
    texture.allocate(TEX_W, TEX_H, GL_RGBA);
    texture.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
    texture.setTextureWrap(GL_REPEAT, GL_REPEAT);
    allocated = true;
    clearPixels();
    texture.loadData(pixels);

    ofLogNotice("BRIDGE")
        << "MarkovMoshGenerator allocated " << TEX_W << "x" << TEX_H;
}

void MarkovMoshGenerator::clearPixels() {
    if (!allocated) return;
    std::fill(pixels.begin(), pixels.end(), 0);
}

uint32_t MarkovMoshGenerator::nextRand() {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x ? x : 69069u;
    return rngState;
}

float MarkovMoshGenerator::rand01() {
    return (nextRand() & 0x00ffffffu) / 16777215.0f;
}

float MarkovMoshGenerator::randRange(float a, float b) {
    return a + (b - a) * rand01();
}

int MarkovMoshGenerator::randInt(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(nextRand() % (uint32_t)(hi - lo + 1));
}

ofFloatColor MarkovMoshGenerator::paletteColor(int palette,
                                                int role,
                                                float variation) const {
    static const ofFloatColor banks[7][3] = {
        { {0.92f,0.90f,0.82f,1}, {0.72f,0.10f,0.08f,1}, {0.08f,0.08f,0.07f,1} },
        { {0.95f,0.74f,0.05f,1}, {0.05f,0.12f,0.28f,1}, {0.48f,0.44f,0.37f,1} },
        { {0.02f,0.72f,0.83f,1}, {0.92f,0.08f,0.05f,1}, {0.02f,0.03f,0.07f,1} },
        { {0.83f,0.78f,0.64f,1}, {0.24f,0.34f,0.16f,1}, {0.56f,0.20f,0.08f,1} },
        { {0.40f,0.15f,0.70f,1}, {0.92f,0.94f,0.98f,1}, {0.03f,0.03f,0.04f,1} },
        { {0.74f,0.78f,0.80f,1}, {0.38f,0.42f,0.44f,1}, {0.08f,0.09f,0.10f,1} },
        { {0.55f,0.72f,0.56f,1}, {0.96f,0.55f,0.10f,1}, {0.12f,0.15f,0.14f,1} }
    };

    ofFloatColor c = banks[palette % 7][role % 3];
    float gain = 0.82f + variation * 0.34f;
    c.r = clamp01(c.r * gain);
    c.g = clamp01(c.g * gain);
    c.b = clamp01(c.b * gain);
    return c;
}

void MarkovMoshGenerator::reset() {
    ensureAllocated();

    rngState = 69069u;
    now = 0.0f;
    lastTime = 0.0f;
    chainAge = 0.0f;
    stateAge = 0.0f;
    transitionAge = 1.0f;
    directionTimer = 0.0f;
    triggerImpulse = 0.0f;

    state = DRIFT_X;
    previousState = DRIFT_X;
    signX = 1;
    signY = 1;
    biasX = 0.25f;
    biasY = 0.25f;

    clearPixels();
    spawnChain(true);
    texture.loadData(pixels);

    ofLogNotice("BRIDGE") << "markovmosh_reset() called";
}

void MarkovMoshGenerator::spawnChain(bool force) {
    if (!force && chainAge < 0.3f) return;

    lines.clear();

    int count = std::max(4, std::min(MAX_LINES, 4 + (int)std::floor(k1 * 10.0f)));
    int palette = std::min(6, (int)std::floor(clamp01(k4) * 7.0f));

    float diag = std::sqrt((float)(TEX_W * TEX_W + TEX_H * TEX_H));
    float margin = 0.65f * std::max(TEX_W, TEX_H);

    for (int i = 0; i < count; ++i) {
        float cx = randRange(TEX_W * 0.12f, TEX_W * 0.88f);
        float cy = randRange(TEX_H * 0.12f, TEX_H * 0.88f);
        float angle = randRange(0.0f, TWO_PI);
        float length = randRange(0.48f, 1.02f) * diag;

        ofVec2f d(std::cos(angle), std::sin(angle));
        ofVec2f a(cx, cy);
        ofVec2f b(cx, cy);
        a -= d * length * 0.5f;
        b += d * length * 0.5f;

        float incoming = randRange(0.0f, TWO_PI);
        ofVec2f offset(std::cos(incoming), std::sin(incoming));
        offset *= margin;

        Line line;
        line.a = a;
        line.b = b;
        line.startA = a - offset;
        line.startB = b - offset;
        line.thickness = randRange(1.6f, 4.8f);
        line.delay = i * randRange(0.015f, 0.07f);
        line.duration = randRange(0.42f, 1.05f);
        line.phase = randRange(0.0f, TWO_PI);
        line.flex = randRange(0.5f, 2.4f);
        line.paired = rand01() < (0.05f + 0.16f * k1);
        line.dashed = rand01() < (0.03f + 0.10f * k3);

        int role = (rand01() < 0.72f) ? 0 : 1;
        line.color = paletteColor(palette, role, rand01());

        lines.push_back(line);
    }

    chainAge = 0.0f;
    nextChainAt = randRange(3.5f, 8.5f) * (1.35f - 0.75f * k2);
}

void MarkovMoshGenerator::chooseNextState(bool audioKick) {
    previousState = state;

    float weights[STATE_COUNT] = {
        1.4f + 1.2f * mid,
        1.5f + 1.2f * low,
        0.55f + 1.1f * mid + 0.5f * energy,
        0.28f + 1.8f * high + 0.5f * triggerImpulse,
        0.10f + 0.70f * high,
        0.42f + 0.90f * (1.0f - energy)
    };

    // Keep the classic mostly-right/down drift identity prominent.
    weights[DRIFT_X] *= 1.15f;
    weights[DRIFT_Y] *= 1.20f;

    if (audioKick) {
        weights[TEAR] *= 1.9f;
        weights[SHEAR] *= 1.35f;
    }

    weights[(int)state] *= 0.55f;

    float sum = 0.0f;
    for (float w : weights) sum += std::max(0.0f, w);

    float pick = rand01() * std::max(sum, 0.001f);
    int selected = 0;
    float acc = 0.0f;
    for (int i = 0; i < STATE_COUNT; ++i) {
        acc += std::max(0.0f, weights[i]);
        if (pick <= acc) {
            selected = i;
            break;
        }
    }

    state = (State)selected;
    stateAge = 0.0f;
    transitionAge = 0.0f;
    transitionDuration = randRange(0.28f, 0.95f) * (1.15f - 0.45f * k2);
    stateHold = randRange(1.8f, 6.8f) * (1.35f - 0.85f * k2);

    if (state == DRIFT_X && rand01() < (0.14f + 0.30f * high)) {
        signX *= -1;
    }
    if (state == DRIFT_Y && rand01() < (0.14f + 0.30f * high)) {
        signY *= -1;
    }
}

void MarkovMoshGenerator::updateDirectionBias(float dt) {
    directionTimer += dt;
    if (directionTimer < 0.35f) return;
    directionTimer = 0.0f;

    biasX = 0.92f * biasX + 0.08f * randRange(-1.0f, 1.0f)
          + 0.05f * (high - mid);
    biasY = 0.92f * biasY + 0.08f * randRange(-1.0f, 1.0f)
          + 0.05f * (low - mid);

    biasX = std::max(-1.0f, std::min(1.0f, biasX));
    biasY = std::max(-1.0f, std::min(1.0f, biasY));

    // Positive remains more likely; last direction adds inertia.
    float pRight = 0.70f + 0.10f * biasX + (signX > 0 ? 0.08f : -0.04f);
    float pDown  = 0.72f + 0.10f * biasY + (signY > 0 ? 0.08f : -0.04f);

    if (rand01() < 0.12f + 0.18f * high) signX = rand01() < pRight ? 1 : -1;
    if (rand01() < 0.12f + 0.18f * low)  signY = rand01() < pDown  ? 1 : -1;
}

void MarkovMoshGenerator::updateStateMachine(float dt) {
    stateAge += dt;
    chainAge += dt;
    transitionAge += dt;
    triggerImpulse = std::max(0.0f, triggerImpulse - dt * 2.8f);

    updateDirectionBias(dt);

    if (trig) {
        triggerImpulse = 1.0f;
        if (rand01() < 0.42f) spawnChain(false);
        if (rand01() < 0.58f) chooseNextState(true);
    }

    if (chainAge >= nextChainAt) {
        spawnChain(false);
    }

    if (stateAge >= stateHold) {
        chooseNextState(false);
    }
}

void MarkovMoshGenerator::putPixel(int x,
                                    int y,
                                    const ofFloatColor& c,
                                    float alpha) {
    if ((unsigned)x >= TEX_W || (unsigned)y >= TEX_H) return;

    alpha = clamp01(alpha);
    ofColor dst = pixels.getColor(x, y);
    float ia = 1.0f - alpha;

    ofColor out;
    out.r = (unsigned char)std::min(255.0f, dst.r * ia + c.r * 255.0f * alpha);
    out.g = (unsigned char)std::min(255.0f, dst.g * ia + c.g * 255.0f * alpha);
    out.b = (unsigned char)std::min(255.0f, dst.b * ia + c.b * 255.0f * alpha);
    out.a = 255;
    pixels.setColor(x, y, out);
}

void MarkovMoshGenerator::drawDisc(float cx,
                                    float cy,
                                    float radius,
                                    const ofFloatColor& c,
                                    float alpha) {
    int minX = (int)std::floor(cx - radius - 1.0f);
    int maxX = (int)std::ceil (cx + radius + 1.0f);
    int minY = (int)std::floor(cy - radius - 1.0f);
    int maxY = (int)std::ceil (cy + radius + 1.0f);

    float rr = std::max(radius * radius, 0.001f);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 > rr) continue;
            float edge = 1.0f - std::sqrt(d2 / rr);
            putPixel(x, y, c, alpha * std::min(1.0f, 0.35f + edge * 1.6f));
        }
    }
}

void MarkovMoshGenerator::drawSegment(const ofVec2f& a,
                                       const ofVec2f& b,
                                       float thickness,
                                       const ofFloatColor& c,
                                       float alpha,
                                       bool dashed) {
    float length = a.distance(b);
    int steps = std::max(1, (int)std::ceil(length * 0.85f));
    float radius = std::max(0.7f, thickness * 0.5f);

    for (int i = 0; i <= steps; ++i) {
        float u = i / (float)steps;
        if (dashed && ((int)(u * 22.0f) & 1)) continue;
        ofVec2f p = lerpVec(a, b, u);
        drawDisc(p.x, p.y, radius, c, alpha);
    }
}

void MarkovMoshGenerator::renderLines() {
    clearPixels();

    float audioFlex = 0.25f * low + 0.45f * mid + 0.30f * high;
    float populationGain = 0.72f + 0.38f * k5;

    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];

        float localAge = chainAge - line.delay;
        float enter = smooth01(localAge / std::max(0.05f, line.duration));

        ofVec2f a = lerpVec(line.startA, line.a, enter);
        ofVec2f b = lerpVec(line.startB, line.b, enter);

        ofVec2f axis = b - a;
        float len = std::max(1.0f, axis.length());
        axis /= len;
        ofVec2f normal(-axis.y, axis.x);

        float flex = std::sin(now * (0.55f + 0.75f * k2)
                              + line.phase
                              + i * 0.31f)
                   * line.flex
                   * (0.25f + audioFlex * 1.35f);

        a += normal * flex;
        b -= normal * flex;

        ofFloatColor shadow(0.015f, 0.018f, 0.020f, 1.0f);
        ofFloatColor hiColor(
            clamp01(line.color.r * 1.12f + 0.03f),
            clamp01(line.color.g * 1.12f + 0.03f),
            clamp01(line.color.b * 1.12f + 0.03f),
            1.0f
        );

        float alpha = (0.72f + 0.22f * energy) * populationGain;

        drawSegment(a + ofVec2f(1.4f, 1.2f),
                    b + ofVec2f(1.4f, 1.2f),
                    line.thickness + 1.6f,
                    shadow,
                    alpha * 0.78f,
                    line.dashed);

        drawSegment(a,
                    b,
                    line.thickness,
                    line.color,
                    alpha,
                    line.dashed);

        drawSegment(a - normal * 0.55f,
                    b - normal * 0.55f,
                    std::max(0.65f, line.thickness * 0.22f),
                    hiColor,
                    alpha * 0.72f,
                    line.dashed);

        if (line.paired) {
            ofVec2f off = normal * (line.thickness * 1.9f + 2.5f);
            drawSegment(a + off,
                        b + off,
                        std::max(1.0f, line.thickness * 0.42f),
                        line.color,
                        alpha * 0.58f,
                        line.dashed);
        }
    }

    texture.loadData(pixels);
}

void MarkovMoshGenerator::step(float t,
                                float inEnergy,
                                float inLow,
                                float inMid,
                                float inHigh,
                                bool inTrig,
                                float inK1,
                                float inK2,
                                float inK3,
                                float inK4,
                                float inK5) {
    ensureAllocated();

    now = t;
    float dt = lastTime > 0.0f ? std::max(0.0f, std::min(0.1f, now - lastTime)) : 1.0f / 30.0f;
    lastTime = now;

    energy = clamp01(0.86f * energy + 0.14f * inEnergy);
    low = clamp01(0.84f * low + 0.16f * inLow);
    mid = clamp01(0.84f * mid + 0.16f * inMid);
    high = clamp01(0.84f * high + 0.16f * inHigh);
    trig = inTrig;

    k1 = clamp01(inK1);
    k2 = clamp01(inK2);
    k3 = clamp01(inK3);
    k4 = clamp01(inK4);
    k5 = clamp01(inK5);

    updateStateMachine(dt);
    renderLines();
}

float MarkovMoshGenerator::getPackedMeta() const {
    float morph = smooth01(transitionAge / std::max(0.05f, transitionDuration));
    float statePart = ((float)state + 0.98f * morph) / 8.0f;
    return statePart;
}
