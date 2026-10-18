#include "RobotFightGenerator.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
    const float PI_F = 3.14159265358979323846f;

    float clampRange(float v, float lo, float hi) {
        return std::max(lo, std::min(hi, v));
    }
}

void RobotFightGenerator::registerFunctions(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &RobotFightGenerator::trampolineReset, 1);
    lua_setglobal(L, "fight_reset");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &RobotFightGenerator::trampolineStep, 1);
    lua_setglobal(L, "fight_step");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &RobotFightGenerator::trampolineDraw, 1);
    lua_setglobal(L, "fight_draw");

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &RobotFightGenerator::trampolineReady, 1);
    lua_setglobal(L, "fight_is_ready");
}

int RobotFightGenerator::trampolineReset(lua_State* L) {
    RobotFightGenerator* self = static_cast<RobotFightGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (self) self->reset();
    return 0;
}

int RobotFightGenerator::trampolineStep(lua_State* L) {
    RobotFightGenerator* self = static_cast<RobotFightGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) return 0;

    const float dt = static_cast<float>(lua_tonumber(L, 1));
    const float energy = static_cast<float>(lua_tonumber(L, 2));
    const bool trigFired = lua_toboolean(L, 3) != 0;
    const float k1 = static_cast<float>(lua_tonumber(L, 4));
    const float k2 = static_cast<float>(lua_tonumber(L, 5));
    const float k3 = static_cast<float>(lua_tonumber(L, 6));
    const float k4 = static_cast<float>(lua_tonumber(L, 7));
    const float k5 = static_cast<float>(lua_tonumber(L, 8));

    self->step(dt, energy, trigFired, k1, k2, k3, k4, k5);
    return 0;
}

int RobotFightGenerator::trampolineDraw(lua_State* L) {
    RobotFightGenerator* self = static_cast<RobotFightGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    if (!self) return 0;

    const float w = static_cast<float>(lua_tonumber(L, 1));
    const float h = static_cast<float>(lua_tonumber(L, 2));
    self->draw(w, h);
    return 0;
}

int RobotFightGenerator::trampolineReady(lua_State* L) {
    RobotFightGenerator* self = static_cast<RobotFightGenerator*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushboolean(L, self && self->ready);
    return 1;
}

float RobotFightGenerator::clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float RobotFightGenerator::smooth01(float v) {
    v = clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

float RobotFightGenerator::smoother01(float v) {
    v = clamp01(v);
    return v * v * v * (v * (v * 6.0f - 15.0f) + 10.0f);
}

float RobotFightGenerator::lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

float RobotFightGenerator::degToRad(float d) {
    return d * PI_F / 180.0f;
}

RobotFightGenerator::Pose RobotFightGenerator::blendPose(
        const Pose& a, const Pose& b, float t) {
    t = smooth01(t);
    Pose p;
    p.rootForward = lerpf(a.rootForward, b.rootForward, t);
    p.rootDown = lerpf(a.rootDown, b.rootDown, t);
    p.pelvisTilt = lerpf(a.pelvisTilt, b.pelvisTilt, t);
    p.torsoLean = lerpf(a.torsoLean, b.torsoLean, t);
    p.headLean = lerpf(a.headLean, b.headLean, t);
    p.frontShoulder = lerpf(a.frontShoulder, b.frontShoulder, t);
    p.frontElbow = lerpf(a.frontElbow, b.frontElbow, t);
    p.rearShoulder = lerpf(a.rearShoulder, b.rearShoulder, t);
    p.rearElbow = lerpf(a.rearElbow, b.rearElbow, t);
    p.frontShoulderZ = lerpf(a.frontShoulderZ, b.frontShoulderZ, t);
    p.frontElbowZ = lerpf(a.frontElbowZ, b.frontElbowZ, t);
    p.frontHandZ = lerpf(a.frontHandZ, b.frontHandZ, t);
    p.rearShoulderZ = lerpf(a.rearShoulderZ, b.rearShoulderZ, t);
    p.rearElbowZ = lerpf(a.rearElbowZ, b.rearElbowZ, t);
    p.rearHandZ = lerpf(a.rearHandZ, b.rearHandZ, t);
    p.frontFootForward = lerpf(a.frontFootForward, b.frontFootForward, t);
    p.frontFootDown = lerpf(a.frontFootDown, b.frontFootDown, t);
    p.frontPlant = lerpf(a.frontPlant, b.frontPlant, t);
    p.rearFootForward = lerpf(a.rearFootForward, b.rearFootForward, t);
    p.rearFootDown = lerpf(a.rearFootDown, b.rearFootDown, t);
    p.rearPlant = lerpf(a.rearPlant, b.rearPlant, t);
    p.frontHipZ = lerpf(a.frontHipZ, b.frontHipZ, t);
    p.frontKneeZ = lerpf(a.frontKneeZ, b.frontKneeZ, t);
    p.frontFootZ = lerpf(a.frontFootZ, b.frontFootZ, t);
    p.rearHipZ = lerpf(a.rearHipZ, b.rearHipZ, t);
    p.rearKneeZ = lerpf(a.rearKneeZ, b.rearKneeZ, t);
    p.rearFootZ = lerpf(a.rearFootZ, b.rearFootZ, t);
    return p;
}

RobotFightGenerator::Pose RobotFightGenerator::sanitizePose(const Pose& in) {
    Pose p = in;
    p.rootForward = clampRange(p.rootForward, -0.16f, 0.16f);
    p.rootDown = clampRange(p.rootDown, -0.36f, 0.42f);
    p.pelvisTilt = clampRange(p.pelvisTilt, -0.42f, 0.42f);
    p.torsoLean = clampRange(p.torsoLean, -1.20f, 0.62f);
    p.headLean = clampRange(p.headLean, -0.48f, 0.48f);

    p.frontShoulder = clampRange(p.frontShoulder, -115.0f, 188.0f);
    p.rearShoulder = clampRange(p.rearShoulder, -115.0f, 188.0f);
    p.frontElbow = clampRange(p.frontElbow, -145.0f, 145.0f);
    p.rearElbow = clampRange(p.rearElbow, -145.0f, 145.0f);

    p.frontShoulderZ = clampRange(p.frontShoulderZ, -0.34f, 0.34f);
    p.frontElbowZ = clampRange(p.frontElbowZ, -0.34f, 0.34f);
    p.frontHandZ = clampRange(p.frontHandZ, -0.34f, 0.34f);
    p.rearShoulderZ = clampRange(p.rearShoulderZ, -0.34f, 0.34f);
    p.rearElbowZ = clampRange(p.rearElbowZ, -0.34f, 0.34f);
    p.rearHandZ = clampRange(p.rearHandZ, -0.34f, 0.34f);

    p.frontFootForward = clampRange(p.frontFootForward, -0.46f, 0.52f);
    p.rearFootForward = clampRange(p.rearFootForward, -0.46f, 0.52f);
    p.frontFootDown = clampRange(p.frontFootDown, 0.10f, 0.64f);
    p.rearFootDown = clampRange(p.rearFootDown, 0.10f, 0.64f);
    p.frontPlant = clamp01(p.frontPlant);
    p.rearPlant = clamp01(p.rearPlant);
    p.frontHipZ = clampRange(p.frontHipZ, -0.34f, 0.34f);
    p.frontKneeZ = clampRange(p.frontKneeZ, -0.34f, 0.34f);
    p.frontFootZ = clampRange(p.frontFootZ, -0.34f, 0.34f);
    p.rearHipZ = clampRange(p.rearHipZ, -0.34f, 0.34f);
    p.rearKneeZ = clampRange(p.rearKneeZ, -0.34f, 0.34f);
    p.rearFootZ = clampRange(p.rearFootZ, -0.34f, 0.34f);
    return p;
}

void RobotFightGenerator::criticalSpringZero(
        float& x, float& v, float omega, float dt) {
    omega = std::max(0.01f, omega);
    const float c2 = v + omega * x;
    const float e = std::exp(-omega * dt);
    const float nx = (x + c2 * dt) * e;
    const float nv = (v - c2 * omega * dt) * e;
    x = nx;
    v = nv;
}

uint32_t RobotFightGenerator::nextRand() {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return x;
}

float RobotFightGenerator::rand01() {
    return static_cast<float>(nextRand() & 0x00FFFFFFu) /
           static_cast<float>(0x01000000u);
}

int RobotFightGenerator::randInt(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + static_cast<int>(nextRand() % static_cast<uint32_t>(hi - lo + 1));
}

RobotFightGenerator::MoveDef RobotFightGenerator::moveDef(MoveType move) const {
    MoveDef d;
    d.duration = 0.40f;
    d.hitFrac = 0.48f;
    d.damage = 5.0f;
    d.knockback = 0.035f;
    d.hitStop = 0.030f;
    d.cameraPunch = 0.35f;

    switch (move) {
        case MOVE_JAB:
            d.duration = 0.34f; d.hitFrac = 0.45f; d.damage = 4.0f;
            d.knockback = 0.020f; d.hitStop = 0.024f; d.cameraPunch = 0.20f; break;
        case MOVE_CROSS:
            d.duration = 0.46f; d.hitFrac = 0.50f; d.damage = 7.0f;
            d.knockback = 0.038f; d.hitStop = 0.034f; d.cameraPunch = 0.36f; break;
        case MOVE_BODY:
            d.duration = 0.50f; d.hitFrac = 0.54f; d.damage = 6.0f;
            d.knockback = 0.032f; d.hitStop = 0.032f; d.cameraPunch = 0.31f; break;
        case MOVE_LOW_KICK:
            d.duration = 0.59f; d.hitFrac = 0.55f; d.damage = 7.0f;
            d.knockback = 0.030f; d.hitStop = 0.033f; d.cameraPunch = 0.30f; break;
        case MOVE_ROUNDHOUSE:
            d.duration = 0.78f; d.hitFrac = 0.57f; d.damage = 11.0f;
            d.knockback = 0.078f; d.hitStop = 0.054f; d.cameraPunch = 0.68f; break;
        case MOVE_HEAVY:
            d.duration = 0.86f; d.hitFrac = 0.59f; d.damage = 13.0f;
            d.knockback = 0.090f; d.hitStop = 0.062f; d.cameraPunch = 0.84f; break;
        case MOVE_SPECIAL:
            // Style-specific values are supplied by moveDefForStyle().
            d.duration = 0.88f; d.hitFrac = 0.57f; d.damage = 15.0f;
            d.knockback = 0.105f; d.hitStop = 0.070f; d.cameraPunch = 0.92f; break;
        case MOVE_BLOCK_HIGH:
        case MOVE_BLOCK_LOW:
        case MOVE_EVADE:
        case MOVE_HIT_HIGH:
        case MOVE_HIT_LOW:
            d.duration = 0.36f; d.hitFrac = 1.0f; d.damage = 0.0f;
            d.knockback = 0.0f; d.hitStop = 0.0f; d.cameraPunch = 0.0f; break;
        case MOVE_HIT_HEAVY:
            d.duration = 0.54f; d.hitFrac = 1.0f; d.damage = 0.0f;
            d.knockback = 0.0f; d.hitStop = 0.0f; d.cameraPunch = 0.0f; break;
        case MOVE_KNOCKDOWN:
            d.duration = 0.92f; d.hitFrac = 1.0f; d.damage = 0.0f;
            d.knockback = 0.0f; d.hitStop = 0.0f; d.cameraPunch = 0.0f; break;
        case MOVE_GETUP:
            d.duration = 0.74f; d.hitFrac = 1.0f; d.damage = 0.0f;
            d.knockback = 0.0f; d.hitStop = 0.0f; d.cameraPunch = 0.0f; break;
        case MOVE_JUMP_KICK:
            d.duration = 0.86f; d.hitFrac = 0.53f; d.damage = 10.0f;
            d.knockback = 0.068f; d.hitStop = 0.048f; d.cameraPunch = 0.58f; break;
        case MOVE_JUMP_PUNCH:
            d.duration = 0.76f; d.hitFrac = 0.50f; d.damage = 8.0f;
            d.knockback = 0.050f; d.hitStop = 0.039f; d.cameraPunch = 0.43f; break;
        case MOVE_HOP_BACK:
            d.duration = 0.66f; d.hitFrac = 1.0f; d.damage = 0.0f;
            d.knockback = 0.0f; d.hitStop = 0.0f; d.cameraPunch = 0.0f; break;
        case MOVE_IDLE:
        default:
            d.duration = 1.0f; d.hitFrac = 1.0f; d.damage = 0.0f;
            d.knockback = 0.0f; d.hitStop = 0.0f; d.cameraPunch = 0.0f; break;
    }
    return d;
}

RobotFightGenerator::MoveDef RobotFightGenerator::moveDefForStyle(
        MoveType move, FighterStyle style) const {
    MoveDef d = moveDef(move);
    if (move != MOVE_SPECIAL) return d;

    // One shared special state, six authored timings/weights. This keeps the
    // state machine small while allowing each archetype's finisher to feel
    // mechanically distinct.
    switch (style) {
        case STYLE_PRESSURE: // PISTON RUSH
            d.duration = 0.72f; d.hitFrac = 0.55f; d.damage = 14.0f;
            d.knockback = 0.080f; d.hitStop = 0.060f; d.cameraPunch = 0.82f; break;
        case STYLE_COUNTER: // INTERCEPT
            d.duration = 0.68f; d.hitFrac = 0.46f; d.damage = 15.0f;
            d.knockback = 0.086f; d.hitStop = 0.068f; d.cameraPunch = 0.90f; break;
        case STYLE_KICKER: // AXIS BREAK
            d.duration = 0.94f; d.hitFrac = 0.60f; d.damage = 17.0f;
            d.knockback = 0.112f; d.hitStop = 0.078f; d.cameraPunch = 1.00f; break;
        case STYLE_HEAVY: // RAM PRESS
            d.duration = 1.02f; d.hitFrac = 0.64f; d.damage = 19.0f;
            d.knockback = 0.135f; d.hitStop = 0.088f; d.cameraPunch = 1.12f; break;
        case STYLE_AGILE: // FLASH CUT
            d.duration = 0.66f; d.hitFrac = 0.48f; d.damage = 14.0f;
            d.knockback = 0.082f; d.hitStop = 0.060f; d.cameraPunch = 0.86f; break;
        case STYLE_BRAWLER: // SCRAP HOOK
        default:
            d.duration = 0.82f; d.hitFrac = 0.58f; d.damage = 16.0f;
            d.knockback = 0.105f; d.hitStop = 0.075f; d.cameraPunch = 0.98f; break;
    }
    return d;
}

RobotFightGenerator::StyleProfile RobotFightGenerator::styleProfile(FighterStyle style) const {
    StyleProfile p;
    // Balanced defaults, then small archetype-specific deviations.  These are
    // intentionally modest so styles bias the same shared fighter system rather
    // than becoming six incompatible animation rigs.
    p.preferredSep = 0.86f;
    p.stanceScale = 1.0f;
    p.stepScale = 1.0f;
    p.stepDurationScale = 1.0f;
    p.neutralDurationScale = 1.0f;
    p.recoilScale = 1.0f;
    p.initiativeHold = 0.72f;
    p.guardRootDown = 0.0f;
    p.guardTorsoLean = 0.0f;
    p.frontShoulderDelta = 0.0f;
    p.frontElbowDelta = 0.0f;
    p.rearShoulderDelta = 0.0f;
    p.rearElbowDelta = 0.0f;
    p.jumpKickBias = 0.70f;
    p.hopBackBias = 0.70f;
    p.jumpPunchBias = 0.70f;

    switch (style) {
        case STYLE_PRESSURE:
            p.preferredSep = 0.77f; p.stanceScale = 0.96f;
            p.stepScale = 1.10f; p.stepDurationScale = 0.91f;
            p.neutralDurationScale = 0.84f; p.recoilScale = 0.90f;
            p.initiativeHold = 0.88f; p.guardRootDown = 0.010f;
            p.guardTorsoLean = 0.030f; p.frontShoulderDelta = -6.0f;
            p.frontElbowDelta = 7.0f; p.rearShoulderDelta = -2.0f;
            p.jumpKickBias = 0.68f; p.hopBackBias = 0.46f; p.jumpPunchBias = 1.15f;
            break;
        case STYLE_COUNTER:
            p.preferredSep = 0.92f; p.stanceScale = 1.02f;
            p.stepScale = 0.94f; p.stepDurationScale = 1.02f;
            p.neutralDurationScale = 1.04f; p.recoilScale = 0.94f;
            p.initiativeHold = 0.74f; p.guardRootDown = 0.004f;
            p.guardTorsoLean = -0.018f; p.frontShoulderDelta = 3.0f;
            p.frontElbowDelta = -5.0f; p.rearShoulderDelta = 2.0f;
            p.rearElbowDelta = -4.0f;
            p.jumpKickBias = 0.42f; p.hopBackBias = 1.65f; p.jumpPunchBias = 0.48f;
            break;
        case STYLE_KICKER:
            p.preferredSep = 0.96f; p.stanceScale = 1.12f;
            p.stepScale = 0.98f; p.stepDurationScale = 0.98f;
            p.neutralDurationScale = 0.94f; p.recoilScale = 0.96f;
            p.initiativeHold = 0.78f; p.guardRootDown = 0.006f;
            p.guardTorsoLean = -0.006f; p.frontShoulderDelta = 2.0f;
            p.frontElbowDelta = -2.0f; p.rearShoulderDelta = 1.0f;
            p.jumpKickBias = 1.85f; p.hopBackBias = 0.72f; p.jumpPunchBias = 0.82f;
            break;
        case STYLE_HEAVY:
            p.preferredSep = 0.83f; p.stanceScale = 1.10f;
            p.stepScale = 0.84f; p.stepDurationScale = 1.16f;
            p.neutralDurationScale = 1.24f; p.recoilScale = 0.68f;
            p.initiativeHold = 0.84f; p.guardRootDown = 0.026f;
            p.guardTorsoLean = 0.012f; p.frontShoulderDelta = 4.0f;
            p.frontElbowDelta = -7.0f; p.rearShoulderDelta = 4.0f;
            p.rearElbowDelta = -6.0f;
            p.jumpKickBias = 0.08f; p.hopBackBias = 0.10f; p.jumpPunchBias = 0.06f;
            break;
        case STYLE_AGILE:
            p.preferredSep = 0.91f; p.stanceScale = 0.92f;
            p.stepScale = 1.12f; p.stepDurationScale = 0.80f;
            p.neutralDurationScale = 0.72f; p.recoilScale = 1.10f;
            p.initiativeHold = 0.67f; p.guardRootDown = -0.006f;
            p.guardTorsoLean = 0.004f; p.frontShoulderDelta = -2.0f;
            p.frontElbowDelta = 4.0f; p.rearElbowDelta = 3.0f;
            p.jumpKickBias = 1.28f; p.hopBackBias = 1.82f; p.jumpPunchBias = 1.70f;
            break;
        case STYLE_BRAWLER:
        default:
            p.preferredSep = 0.75f; p.stanceScale = 1.04f;
            p.stepScale = 1.06f; p.stepDurationScale = 0.95f;
            p.neutralDurationScale = 0.88f; p.recoilScale = 0.82f;
            p.initiativeHold = 0.82f; p.guardRootDown = 0.018f;
            p.guardTorsoLean = 0.020f; p.frontShoulderDelta = -3.0f;
            p.frontElbowDelta = 6.0f; p.rearShoulderDelta = 5.0f;
            p.rearElbowDelta = 4.0f;
            p.jumpKickBias = 0.38f; p.hopBackBias = 0.34f; p.jumpPunchBias = 0.72f;
            break;
    }
    return p;
}

const char* RobotFightGenerator::styleName(FighterStyle style) const {
    switch (style) {
        case STYLE_PRESSURE: return "PRESSURE";
        case STYLE_COUNTER: return "COUNTER";
        case STYLE_KICKER: return "KICKER";
        case STYLE_HEAVY: return "HEAVY";
        case STYLE_AGILE: return "AGILE";
        case STYLE_BRAWLER: return "BRAWLER";
        default: return "UNKNOWN";
    }
}

const char* RobotFightGenerator::specialName(FighterStyle style) const {
    switch (style) {
        case STYLE_PRESSURE: return "PISTON RUSH";
        case STYLE_COUNTER: return "INTERCEPT";
        case STYLE_KICKER: return "AXIS BREAK";
        case STYLE_HEAVY: return "RAM PRESS";
        case STYLE_AGILE: return "FLASH CUT";
        case STYLE_BRAWLER: return "SCRAP HOOK";
        default: return "SPECIAL";
    }
}

int RobotFightGenerator::signatureExchangeId(FighterStyle style) const {
    return 15 + static_cast<int>(style);
}

int RobotFightGenerator::specialExchangeId(FighterStyle style) const {
    return 21 + static_cast<int>(style);
}

float RobotFightGenerator::styleExchangeWeight(FighterStyle style, int exchangeId) const {
    // Ground phrases 0..11 are inherited from V0.6. V0.7 adds:
    // 12 jump-kick entry, 13 evasive hop-back counter, 14 leap-punch entry.
    static const float weights[STYLE_COUNT][15] = {
        {1.00f,0.72f,1.72f,0.0f,0.82f,1.05f,2.20f,0.52f,0.62f,0.42f,1.35f,0.70f, 0.42f,0.28f,0.72f},
        {1.55f,1.25f,0.48f,0.0f,1.52f,1.10f,0.35f,2.25f,0.75f,2.35f,0.55f,1.45f, 0.25f,1.05f,0.28f},
        {0.72f,1.72f,1.05f,0.0f,1.42f,1.55f,0.55f,0.88f,2.65f,0.82f,0.55f,1.35f, 1.25f,0.42f,0.46f},
        {0.65f,1.12f,0.55f,0.0f,1.02f,0.70f,1.82f,0.42f,0.48f,0.62f,2.55f,0.45f, 0.03f,0.04f,0.02f},
        {1.18f,0.78f,0.88f,0.0f,0.72f,1.30f,0.42f,1.90f,1.18f,1.55f,0.42f,2.35f, 0.92f,1.16f,1.18f},
        {0.72f,1.00f,1.18f,0.0f,1.00f,0.82f,2.15f,0.52f,0.52f,0.55f,2.05f,0.58f, 0.18f,0.16f,0.42f}
    };
    if (style < STYLE_PRESSURE || style >= STYLE_COUNT) return 1.0f;
    if (exchangeId < 0 || exchangeId >= 15) return 0.0f;

    float w = weights[static_cast<int>(style)][exchangeId];
    if (exchangeId >= 12) {
        const StyleProfile sp = styleProfile(style);
        if (exchangeId == 12) w *= sp.jumpKickBias;
        else if (exchangeId == 13) w *= sp.hopBackBias;
        else if (exchangeId == 14) w *= sp.jumpPunchBias;
    }
    return w;
}


void RobotFightGenerator::chooseNeutralBehavior(Fighter& f) {
    const int index = (&f == &fighters[0]) ? 0 : 1;
    const bool ownsInitiative = (index == initiativeOwner);
    float w[7] = {0.14f, 0.15f, 0.13f, 0.14f, 0.12f, 0.14f, 0.18f};

    // Style changes which neutral "thoughts" are likely, while initiative
    // nudges the same vocabulary toward pressure or defense.
    switch (f.style) {
        case STYLE_PRESSURE:
            w[0]=0.11f; w[1]=0.23f; w[2]=0.07f; w[3]=0.11f; w[4]=0.07f; w[5]=0.15f; w[6]=0.26f;
            break;
        case STYLE_COUNTER:
            w[0]=0.16f; w[1]=0.07f; w[2]=0.18f; w[3]=0.17f; w[4]=0.18f; w[5]=0.15f; w[6]=0.09f;
            break;
        case STYLE_KICKER:
            w[0]=0.18f; w[1]=0.14f; w[2]=0.14f; w[3]=0.12f; w[4]=0.12f; w[5]=0.14f; w[6]=0.16f;
            break;
        case STYLE_HEAVY:
            w[0]=0.35f; w[1]=0.12f; w[2]=0.12f; w[3]=0.07f; w[4]=0.07f; w[5]=0.18f; w[6]=0.09f;
            break;
        case STYLE_AGILE:
            w[0]=0.08f; w[1]=0.14f; w[2]=0.11f; w[3]=0.19f; w[4]=0.18f; w[5]=0.11f; w[6]=0.19f;
            break;
        case STYLE_BRAWLER:
        default:
            w[0]=0.15f; w[1]=0.22f; w[2]=0.07f; w[3]=0.10f; w[4]=0.07f; w[5]=0.18f; w[6]=0.21f;
            break;
    }

    if (ownsInitiative) {
        w[NEUTRAL_FEINT] *= 1.24f;
        w[NEUTRAL_WEIGHT_LEAD] *= 1.18f;
        w[NEUTRAL_WEIGHT_REAR] *= 0.82f;
    } else {
        w[NEUTRAL_SLIP_INSIDE] *= 1.15f;
        w[NEUTRAL_SLIP_OUTSIDE] *= 1.15f;
        w[NEUTRAL_WEIGHT_REAR] *= 1.12f;
        w[NEUTRAL_FEINT] *= 0.82f;
    }

    float total = 0.0f;
    for (int i = 0; i < 7; ++i) total += w[i];
    float r = rand01() * total;
    int choice = 0;
    for (int i = 0; i < 7; ++i) {
        r -= w[i];
        if (r <= 0.0f) { choice = i; break; }
    }
    f.neutralBehavior = static_cast<NeutralBehavior>(choice);

    f.neutralBehaviorTime = 0.0f;
    const StyleProfile sp = styleProfile(f.style);
    const float base = (0.46f + rand01() * 0.46f) * sp.neutralDurationScale;
    f.neutralBehaviorDuration = base + ((f.neutralBehavior == NEUTRAL_SETTLE) ? 0.22f * sp.neutralDurationScale : 0.0f);
}

void RobotFightGenerator::updateNeutralBehavior(Fighter& f, float dt) {
    if (f.move != MOVE_IDLE || f.airMode != AIR_GROUNDED) return;

    f.neutralBehaviorTime += dt;
    if (f.neutralBehaviorDuration <= 0.001f ||
        f.neutralBehaviorTime >= f.neutralBehaviorDuration) {
        chooseNeutralBehavior(f);
    }
}

RobotFightGenerator::Pose RobotFightGenerator::guardPose(const Fighter& f) const {
    Pose p;
    const StyleProfile sp = styleProfile(f.style);

    // V0.5 BASE GUARD
    // ----------------
    // This is an active striking stance rather than an anatomical rest pose:
    // knees loaded, torso slightly forward, lead hand advanced, rear hand near
    // the jaw, elbows tucked.  Arms are hierarchical, so negative elbow values
    // fold the forearm back upward toward the head instead of hanging downward.
    const float breathe = std::sin(f.idlePhase * 0.82f) * 0.0028f;
    p.rootForward = 0.0f;
    p.rootDown = 0.020f + sp.guardRootDown + breathe;
    p.pelvisTilt = -0.012f + std::sin(f.idlePhase * 0.47f) * 0.006f;
    p.torsoLean = 0.035f + sp.guardTorsoLean + std::sin(f.idlePhase * 0.53f) * 0.008f;
    p.headLean = -0.050f - p.torsoLean * 0.10f;

    // Lead hand floats slightly farther forward; rear hand stays tighter to the
    // head.  These values put both fists around shoulder/head height in the
    // current hierarchical rig instead of below the rib cage.
    p.frontShoulder = 45.0f + sp.frontShoulderDelta;
    p.frontElbow = -130.0f + sp.frontElbowDelta;
    p.rearShoulder = 38.0f + sp.rearShoulderDelta;
    p.rearElbow = -145.0f + sp.rearElbowDelta;

    p.frontShoulderZ = 0.115f;
    p.frontElbowZ = 0.145f;
    p.frontHandZ = 0.170f;
    p.rearShoulderZ = -0.115f;
    p.rearElbowZ = -0.145f;
    p.rearHandZ = -0.170f;

    p.frontFootForward = 0.19f * sp.stanceScale;
    p.frontFootDown = 0.56f;
    p.frontPlant = 1.0f;
    p.rearFootForward = -0.17f * sp.stanceScale;
    p.rearFootDown = 0.56f;
    p.rearPlant = 1.0f;
    p.frontHipZ = 0.090f;
    p.frontKneeZ = 0.115f;
    p.frontFootZ = 0.130f;
    p.rearHipZ = -0.090f;
    p.rearKneeZ = -0.115f;
    p.rearFootZ = -0.130f;

    // Neutral behavior is deliberately low-amplitude and phrase-like.  Each
    // behavior eases in then out so the fighter keeps a stable defensive
    // structure instead of receiving continuous random noise.
    const float nt = clamp01(f.neutralBehaviorTime / std::max(0.001f, f.neutralBehaviorDuration));
    const float env = std::sin(nt * PI_F);
    switch (f.neutralBehavior) {
        case NEUTRAL_WEIGHT_LEAD:
            p.rootForward += 0.020f * env;
            p.rootDown += 0.005f * env;
            p.pelvisTilt += 0.025f * env;
            p.torsoLean += 0.024f * env;
            break;
        case NEUTRAL_WEIGHT_REAR:
            p.rootForward -= 0.018f * env;
            p.rootDown += 0.004f * env;
            p.pelvisTilt -= 0.022f * env;
            p.torsoLean -= 0.020f * env;
            break;
        case NEUTRAL_SLIP_INSIDE:
            p.rootForward += 0.010f * env;
            p.torsoLean += 0.070f * env;
            p.headLean -= 0.090f * env;
            p.frontShoulder += 3.0f * env;
            p.frontElbow -= 4.0f * env;
            break;
        case NEUTRAL_SLIP_OUTSIDE:
            p.rootForward -= 0.012f * env;
            p.torsoLean -= 0.062f * env;
            p.headLean += 0.078f * env;
            p.rearShoulder -= 4.0f * env;
            p.rearElbow += 4.0f * env;
            break;
        case NEUTRAL_GUARD_TWITCH:
            p.frontShoulder -= 7.0f * env;
            p.frontElbow += 8.0f * env;
            p.rearShoulder -= 3.0f * env;
            break;
        case NEUTRAL_FEINT:
            p.rootForward += 0.026f * env;
            p.rootDown += 0.006f * env;
            p.torsoLean += 0.045f * env;
            p.frontShoulder -= 11.0f * env;
            p.frontElbow += 14.0f * env;
            break;
        case NEUTRAL_SETTLE:
        default:
            p.rootDown += 0.006f * env;
            p.frontShoulder += 2.0f * env;
            p.rearShoulder -= 2.0f * env;
            break;
    }

    if (f.airMode == AIR_GROUNDED && f.landingDuration > 0.001f &&
        f.landingTime < f.landingDuration) {
        const float lt = clamp01(f.landingTime / f.landingDuration);
        const float absorb = 1.0f - smooth01(lt);
        p.rootDown += f.landingStrength * absorb;
        p.torsoLean += 0.055f * absorb;
        p.pelvisTilt -= 0.045f * absorb;
        p.frontShoulder += 5.0f * absorb;
        p.frontElbow -= 5.0f * absorb;
        p.rearShoulder += 4.0f * absorb;
        p.rearElbow -= 4.0f * absorb;
    }

    if (f.stepActive) {
        const float t = clamp01(f.stepTime / std::max(0.001f, f.stepDuration));
        const float lift = std::sin(t * PI_F);
        p.rootDown -= lift * 0.008f;
        p.torsoLean += f.stepDirection * lift * 0.026f;
        p.pelvisTilt -= f.stepDirection * lift * 0.018f;
        // The hands stay in guard while the lower body moves.
        p.frontShoulder += f.stepDirection * lift * 1.8f;
        p.rearShoulder -= f.stepDirection * lift * 1.8f;
    }

    return sanitizePose(p);
}

RobotFightGenerator::Pose RobotFightGenerator::airPose(
        const Fighter& f, const Pose& base) const {
    Pose p = base;
    const float u = clamp01(f.airTime / std::max(0.001f, f.airDuration));
    const float arc = std::sin(u * PI_F);
    const float takeoffLoad = (u < 0.15f)
        ? (1.0f - smooth01(u / 0.15f))
        : 0.0f;

    // Both feet are free in the air. The body follows one authored vertical arc;
    // limb targets provide silhouette changes without any ballistic solver.
    p.rootDown += 0.040f * takeoffLoad - f.airHeight * arc;
    p.frontPlant = 0.0f;
    p.rearPlant = 0.0f;
    p.frontFootForward = 0.10f;
    p.frontFootDown = 0.34f;
    p.rearFootForward = -0.07f;
    p.rearFootDown = 0.34f;

    if (f.airMode == AIR_HOP_BACK) {
        // Compact defensive hop: keep guard closed and tuck the knees at apex.
        const float tuck = std::sin(u * PI_F);
        p.torsoLean -= 0.090f * tuck;
        p.headLean += 0.045f * tuck;
        p.frontShoulder += 10.0f * tuck;
        p.frontElbow -= 9.0f * tuck;
        p.rearShoulder += 8.0f * tuck;
        p.rearElbow -= 7.0f * tuck;
        p.frontFootForward = lerpf(0.10f, 0.02f, tuck);
        p.rearFootForward = lerpf(-0.07f, -0.01f, tuck);
        p.frontFootDown = lerpf(0.34f, 0.25f, tuck);
        p.rearFootDown = lerpf(0.34f, 0.24f, tuck);
    } else if (f.airMode == AIR_JUMP_KICK) {
        const float k = std::sin(clamp01((u - 0.16f) / 0.70f) * PI_F);
        p.torsoLean -= 0.105f * k;
        p.pelvisTilt += 0.110f * k;
        p.frontFootForward = lerpf(0.10f, 0.50f, k);
        p.frontFootDown = lerpf(0.34f, 0.20f, k);
        p.rearFootForward = lerpf(-0.07f, 0.01f, k);
        p.rearFootDown = lerpf(0.34f, 0.27f, k);
        p.frontHipZ = lerpf(p.frontHipZ, 0.12f, k);
        p.frontKneeZ = lerpf(p.frontKneeZ, 0.21f, k);
        p.frontFootZ = lerpf(p.frontFootZ, 0.31f, k);
        // Counterbalance while keeping the rear hand protective.
        p.frontShoulder = lerpf(p.frontShoulder, 104.0f, k);
        p.frontElbow = lerpf(p.frontElbow, -100.0f, k);
        p.rearShoulder = lerpf(p.rearShoulder, 44.0f, k);
        p.rearElbow = lerpf(p.rearElbow, -142.0f, k);
    } else if (f.airMode == AIR_JUMP_PUNCH) {
        const float a = std::sin(clamp01((u - 0.14f) / 0.72f) * PI_F);
        p.rootForward += 0.035f * a;
        p.torsoLean += 0.120f * a;
        p.pelvisTilt += 0.055f * a;
        p.frontShoulder = lerpf(p.frontShoulder, -4.0f, a);
        p.frontElbow = lerpf(p.frontElbow, 6.0f, a);
        p.frontElbowZ = lerpf(p.frontElbowZ, 0.20f, a);
        p.frontHandZ = lerpf(p.frontHandZ, 0.30f, a);
        p.rearShoulder = lerpf(p.rearShoulder, 42.0f, a);
        p.rearElbow = lerpf(p.rearElbow, -145.0f, a);
        // Knees tuck slightly so the airborne silhouette is distinct from a
        // grounded punch translated upward.
        p.frontFootForward = lerpf(0.10f, 0.04f, a);
        p.rearFootForward = lerpf(-0.07f, -0.01f, a);
        p.frontFootDown = lerpf(0.34f, 0.27f, a);
        p.rearFootDown = lerpf(0.34f, 0.25f, a);
    }

    return sanitizePose(p);
}

RobotFightGenerator::Pose RobotFightGenerator::attackPose(
        MoveType move, float u, const Pose& base) const {
    Pose wind = base;
    Pose strike = base;
    Pose follow = base;
    Pose recover = base;
    const float hit = moveDef(move).hitFrac;

    switch (move) {
        case MOVE_JAB:
            wind.rootForward = -0.010f;
            wind.frontShoulder = 58.0f;
            wind.frontElbow = -135.0f;
            strike.rootForward = 0.055f;
            strike.torsoLean = 0.105f;
            strike.pelvisTilt = 0.035f;
            strike.frontShoulder = -4.0f;
            strike.frontElbow = 7.0f;
            strike.frontElbowZ = 0.215f;
            strike.frontHandZ = 0.300f;
            follow.rootForward = 0.030f;
            follow.torsoLean = 0.055f;
            follow.frontShoulder = 20.0f;
            follow.frontElbow = -28.0f;
            break;

        case MOVE_CROSS:
            wind.rootForward = -0.040f;
            wind.rootDown = 0.015f;
            wind.torsoLean = -0.095f;
            wind.pelvisTilt = -0.075f;
            // Coil the torso while keeping the rear fist close to guard.
            wind.rearShoulder = 64.0f;
            wind.rearElbow = -145.0f;
            strike.rootForward = 0.100f;
            strike.torsoLean = 0.205f;
            strike.pelvisTilt = 0.115f;
            strike.rearShoulder = -8.0f;
            strike.rearElbow = 6.0f;
            // Rear cross passes through the torso plane and finishes in front.
            strike.rearShoulderZ = -0.090f;
            strike.rearElbowZ = 0.035f;
            strike.rearHandZ = 0.245f;
            // Non-striking lead hand remains protective.
            strike.frontShoulder = 46.0f;
            strike.frontElbow = -129.0f;
            follow.rootForward = 0.052f;
            follow.torsoLean = 0.090f;
            follow.rearShoulder = 24.0f;
            follow.rearElbow = -24.0f;
            follow.rearElbowZ = 0.015f;
            follow.rearHandZ = 0.140f;

            break;

        case MOVE_BODY:
            wind.rootForward = -0.024f;
            wind.rootDown = 0.045f;
            wind.torsoLean = -0.045f;
            wind.frontShoulder = 56.0f;
            wind.frontElbow = -128.0f;
            strike.rootForward = 0.075f;
            strike.rootDown = 0.060f;
            strike.torsoLean = 0.185f;
            strike.frontShoulder = 18.0f;
            strike.frontElbow = -6.0f;
            strike.frontElbowZ = 0.185f;
            strike.frontHandZ = 0.255f;
            follow.rootForward = 0.030f;
            follow.rootDown = 0.025f;
            follow.frontShoulder = 36.0f;
            follow.frontElbow = -42.0f;
            break;

        case MOVE_LOW_KICK:
            wind.rootForward = -0.025f;
            wind.rootDown = 0.018f;
            wind.torsoLean = -0.100f;
            wind.frontPlant = 0.12f;
            wind.frontFootForward = 0.14f;
            wind.frontFootDown = 0.29f;
            strike.rootForward = 0.052f;
            strike.torsoLean = -0.105f;
            strike.frontPlant = 0.0f;
            strike.frontFootForward = 0.44f;
            strike.frontFootDown = 0.39f;
            strike.frontHipZ = 0.105f;
            strike.frontKneeZ = 0.180f;
            strike.frontFootZ = 0.270f;
            // Kick with a compact guard rather than throwing both arms open.
            strike.frontShoulder = 52.0f;
            strike.frontElbow = -124.0f;
            strike.rearShoulder = 42.0f;
            strike.rearElbow = -145.0f;
            follow.rootForward = 0.015f;
            follow.frontPlant = 0.20f;
            follow.frontFootForward = 0.23f;
            follow.frontFootDown = 0.43f;
            recover.frontPlant = 0.78f;
            break;

        case MOVE_ROUNDHOUSE:
            // Rear foot becomes the striking foot; front foot remains locked.
            wind.rootForward = -0.052f;
            wind.rootDown = -0.006f;
            wind.torsoLean = -0.170f;
            wind.pelvisTilt = -0.120f;
            wind.rearPlant = 0.08f;
            wind.rearFootForward = 0.05f;
            wind.rearFootDown = 0.22f;
            wind.rearKneeZ = -0.175f;
            wind.rearFootZ = -0.220f;
            strike.rootForward = 0.082f;
            strike.rootDown = -0.020f;
            strike.torsoLean = -0.245f;
            strike.pelvisTilt = 0.160f;
            strike.rearPlant = 0.0f;
            strike.rearFootForward = 0.47f;
            strike.rearFootDown = 0.19f;
            // Roundhouse wraps from the far side to the camera side.
            strike.rearHipZ = -0.060f;
            strike.rearKneeZ = 0.085f;
            strike.rearFootZ = 0.300f;
            // One arm counterbalances the rotation, the other still protects.
            strike.frontShoulder = 112.0f;
            strike.frontElbow = -104.0f;
            strike.rearShoulder = 74.0f;
            strike.rearElbow = -118.0f;
            follow.rootForward = 0.028f;
            follow.torsoLean = -0.095f;
            follow.rearPlant = 0.16f;
            follow.rearFootForward = 0.20f;
            follow.rearFootDown = 0.39f;
            follow.rearKneeZ = 0.020f;
            follow.rearFootZ = 0.115f;
            recover.rearPlant = 0.78f;
            break;

        case MOVE_HEAVY:
            wind.rootForward = -0.070f;
            wind.rootDown = 0.035f;
            wind.torsoLean = -0.150f;
            wind.pelvisTilt = -0.105f;
            // Heavy strike loads through hips/torso; it does not drop both hands.
            wind.rearShoulder = 74.0f;
            wind.rearElbow = -138.0f;
            wind.frontShoulder = 46.0f;
            wind.frontElbow = -129.0f;
            strike.rootForward = 0.120f;
            strike.rootDown = -0.010f;
            strike.torsoLean = 0.245f;
            strike.pelvisTilt = 0.170f;
            strike.rearShoulder = -12.0f;
            strike.rearElbow = 9.0f;
            strike.rearShoulderZ = -0.080f;
            strike.rearElbowZ = 0.055f;
            strike.rearHandZ = 0.290f;
            strike.frontShoulder = 46.0f;
            strike.frontElbow = -129.0f;
            follow.rootForward = 0.050f;
            follow.torsoLean = 0.095f;
            follow.rearShoulder = 28.0f;
            follow.rearElbow = -22.0f;
            follow.rearElbowZ = 0.030f;
            follow.rearHandZ = 0.160f;

            break;

        default:
            return base;
    }

    float windShare = 0.55f;
    if (move == MOVE_JAB) windShare = 0.42f;
    if (move == MOVE_ROUNDHOUSE) windShare = 0.71f;
    if (move == MOVE_HEAVY) windShare = 0.75f;

    const float windEnd = std::max(0.14f, hit * windShare);
    const float followEnd = std::min(0.86f, hit + ((move == MOVE_HEAVY || move == MOVE_ROUNDHOUSE) ? 0.18f : 0.15f));
    const float recoverEnd = 0.94f;

    Pose out;
    if (u < windEnd) {
        out = blendPose(base, wind, u / windEnd);
    } else if (u < hit) {
        // Strike acceleration is deliberately sharper than the anticipation.
        out = blendPose(wind, strike, smoother01((u - windEnd) / std::max(0.001f, hit - windEnd)));
    } else if (u < followEnd) {
        out = blendPose(strike, follow, (u - hit) / std::max(0.001f, followEnd - hit));
    } else if (u < recoverEnd) {
        out = blendPose(follow, recover, (u - followEnd) / std::max(0.001f, recoverEnd - followEnd));
    } else {
        out = blendPose(recover, base, (u - recoverEnd) / std::max(0.001f, 1.0f - recoverEnd));
    }
    return sanitizePose(out);
}

RobotFightGenerator::Pose RobotFightGenerator::specialPose(
        const Fighter& f, float u, const Pose& base) const {
    Pose wind = base;
    Pose strike = base;
    Pose follow = base;
    Pose recover = base;

    switch (f.style) {
        case STYLE_PRESSURE: // PISTON RUSH: deep coil into a driving rear straight.
            wind.rootForward = -0.080f;
            wind.rootDown = 0.025f;
            wind.torsoLean = -0.170f;
            wind.pelvisTilt = -0.110f;
            wind.rearShoulder = 78.0f;
            wind.rearElbow = -145.0f;
            wind.frontShoulder = 44.0f;
            wind.frontElbow = -132.0f;
            strike.rootForward = 0.155f;
            strike.rootDown = -0.012f;
            strike.torsoLean = 0.315f;
            strike.pelvisTilt = 0.215f;
            strike.rearShoulder = -18.0f;
            strike.rearElbow = 12.0f;
            strike.rearShoulderZ = -0.080f;
            strike.rearElbowZ = 0.095f;
            strike.rearHandZ = 0.335f;
            strike.frontShoulder = 43.0f;
            strike.frontElbow = -132.0f;
            follow.rootForward = 0.072f;
            follow.torsoLean = 0.125f;
            follow.rearShoulder = 20.0f;
            follow.rearElbow = -18.0f;
            follow.rearHandZ = 0.180f;
            break;

        case STYLE_COUNTER: // INTERCEPT: lean away, then spear the lead hand through center.
            wind.rootForward = -0.105f;
            wind.rootDown = 0.005f;
            wind.torsoLean = -0.245f;
            wind.headLean = 0.075f;
            wind.frontShoulder = 66.0f;
            wind.frontElbow = -138.0f;
            strike.rootForward = 0.105f;
            strike.torsoLean = 0.175f;
            strike.pelvisTilt = 0.095f;
            strike.frontShoulder = -22.0f;
            strike.frontElbow = 22.0f;
            strike.frontShoulderZ = 0.060f;
            strike.frontElbowZ = 0.210f;
            strike.frontHandZ = 0.335f;
            strike.rearShoulder = 58.0f;
            strike.rearElbow = -142.0f;
            follow.rootForward = 0.030f;
            follow.torsoLean = 0.040f;
            follow.frontShoulder = 34.0f;
            follow.frontElbow = -55.0f;
            break;

        case STYLE_KICKER: // AXIS BREAK: rear leg sweeps high across the camera plane.
            wind.rootForward = -0.065f;
            wind.rootDown = -0.005f;
            wind.torsoLean = -0.235f;
            wind.pelvisTilt = -0.155f;
            wind.rearPlant = 0.04f;
            wind.rearFootForward = 0.02f;
            wind.rearFootDown = 0.20f;
            wind.rearKneeZ = -0.205f;
            wind.rearFootZ = -0.255f;
            strike.rootForward = 0.060f;
            strike.rootDown = -0.040f;
            strike.torsoLean = -0.335f;
            strike.pelvisTilt = 0.235f;
            strike.rearPlant = 0.0f;
            strike.rearFootForward = 0.515f;
            strike.rearFootDown = 0.115f;
            strike.rearHipZ = -0.030f;
            strike.rearKneeZ = 0.155f;
            strike.rearFootZ = 0.340f;
            strike.frontShoulder = 78.0f;
            strike.frontElbow = -132.0f;
            strike.rearShoulder = 58.0f;
            strike.rearElbow = -136.0f;
            follow.rootForward = 0.020f;
            follow.torsoLean = -0.135f;
            follow.rearPlant = 0.14f;
            follow.rearFootForward = 0.22f;
            follow.rearFootDown = 0.38f;
            follow.rearKneeZ = 0.030f;
            follow.rearFootZ = 0.120f;
            recover.rearPlant = 0.80f;
            break;

        case STYLE_HEAVY: // RAM PRESS: both forearms drive through a planted torso.
            wind.rootForward = -0.105f;
            wind.rootDown = 0.070f;
            wind.torsoLean = -0.185f;
            wind.pelvisTilt = -0.125f;
            wind.frontShoulder = 62.0f;
            wind.frontElbow = -138.0f;
            wind.rearShoulder = 72.0f;
            wind.rearElbow = -145.0f;
            strike.rootForward = 0.160f;
            strike.rootDown = 0.010f;
            strike.torsoLean = 0.345f;
            strike.pelvisTilt = 0.235f;
            strike.frontShoulder = -8.0f;
            strike.frontElbow = 7.0f;
            strike.rearShoulder = -14.0f;
            strike.rearElbow = 10.0f;
            strike.frontElbowZ = 0.190f;
            strike.frontHandZ = 0.315f;
            strike.rearElbowZ = 0.120f;
            strike.rearHandZ = 0.285f;
            follow.rootForward = 0.080f;
            follow.rootDown = 0.030f;
            follow.torsoLean = 0.135f;
            follow.frontShoulder = 28.0f;
            follow.frontElbow = -40.0f;
            follow.rearShoulder = 34.0f;
            follow.rearElbow = -42.0f;
            break;

        case STYLE_AGILE: // FLASH CUT: compressed slip into a sharp lead-hand dart.
            wind.rootForward = -0.115f;
            wind.rootDown = -0.018f;
            wind.torsoLean = -0.285f;
            wind.headLean = 0.100f;
            wind.pelvisTilt = -0.095f;
            wind.frontShoulder = 82.0f;
            wind.frontElbow = -142.0f;
            strike.rootForward = 0.145f;
            strike.rootDown = -0.030f;
            strike.torsoLean = 0.205f;
            strike.pelvisTilt = 0.165f;
            strike.frontShoulder = -28.0f;
            strike.frontElbow = 29.0f;
            strike.frontShoulderZ = 0.080f;
            strike.frontElbowZ = 0.225f;
            strike.frontHandZ = 0.340f;
            strike.rearShoulder = 56.0f;
            strike.rearElbow = -144.0f;
            follow.rootForward = 0.020f;
            follow.torsoLean = -0.025f;
            follow.frontShoulder = 42.0f;
            follow.frontElbow = -78.0f;
            break;

        case STYLE_BRAWLER: // SCRAP HOOK: load low, sling a looping rear hand across.
        default:
            wind.rootForward = -0.085f;
            wind.rootDown = 0.055f;
            wind.torsoLean = -0.205f;
            wind.pelvisTilt = -0.145f;
            wind.rearShoulder = 104.0f;
            wind.rearElbow = -112.0f;
            wind.frontShoulder = 48.0f;
            wind.frontElbow = -130.0f;
            strike.rootForward = 0.150f;
            strike.rootDown = 0.020f;
            strike.torsoLean = 0.285f;
            strike.pelvisTilt = 0.215f;
            strike.rearShoulder = 18.0f;
            strike.rearElbow = -8.0f;
            strike.rearShoulderZ = -0.040f;
            strike.rearElbowZ = 0.160f;
            strike.rearHandZ = 0.335f;
            strike.frontShoulder = 48.0f;
            strike.frontElbow = -132.0f;
            follow.rootForward = 0.060f;
            follow.torsoLean = 0.105f;
            follow.rearShoulder = 38.0f;
            follow.rearElbow = -45.0f;
            follow.rearHandZ = 0.160f;
            break;
    }

    const MoveDef d = moveDefForStyle(MOVE_SPECIAL, f.style);
    const float hit = d.hitFrac;
    const float windEnd = std::max(0.16f, hit * 0.70f);
    const float followEnd = std::min(0.88f, hit + 0.17f);
    const float recoverEnd = 0.95f;

    Pose out;
    if (u < windEnd) {
        out = blendPose(base, wind, u / windEnd);
    } else if (u < hit) {
        out = blendPose(wind, strike,
            smoother01((u - windEnd) / std::max(0.001f, hit - windEnd)));
    } else if (u < followEnd) {
        out = blendPose(strike, follow,
            (u - hit) / std::max(0.001f, followEnd - hit));
    } else if (u < recoverEnd) {
        out = blendPose(follow, recover,
            (u - followEnd) / std::max(0.001f, recoverEnd - followEnd));
    } else {
        out = blendPose(recover, base,
            (u - recoverEnd) / std::max(0.001f, 1.0f - recoverEnd));
    }
    return sanitizePose(out);
}

RobotFightGenerator::Pose RobotFightGenerator::reactionPose(
        MoveType move, float u, const Pose& base) const {
    Pose contact = base;
    Pose stagger = base;
    Pose brace = base;

    switch (move) {
        case MOVE_BLOCK_HIGH:
            contact.rootForward = -0.018f;
            contact.rootDown = 0.025f;
            contact.torsoLean = -0.080f;
            contact.frontShoulder = 26.0f;
            contact.frontElbow = -86.0f;
            contact.rearShoulder = 50.0f;
            contact.rearElbow = -104.0f;
            contact.frontHandZ = 0.210f;
            contact.rearHandZ = -0.055f;
            stagger = contact;
            stagger.rootForward = -0.034f;
            stagger.rootDown = 0.045f;
            brace = base;
            brace.frontShoulder = 54.0f;
            brace.frontElbow = -112.0f;
            brace.rearShoulder = 88.0f;
            brace.rearElbow = -136.0f;
            break;

        case MOVE_BLOCK_LOW:
            contact.rootForward = -0.018f;
            contact.rootDown = 0.105f;
            contact.torsoLean = -0.105f;
            contact.frontShoulder = 62.0f;
            contact.frontElbow = -108.0f;
            contact.rearShoulder = 92.0f;
            contact.rearElbow = -128.0f;
            stagger = contact;
            stagger.rootDown = 0.135f;
            brace = base;
            brace.rootDown = 0.055f;
            break;

        case MOVE_EVADE:
            contact.rootForward = -0.120f;
            contact.rootDown = -0.008f;
            contact.torsoLean = -0.220f;
            contact.headLean = 0.090f;
            contact.frontShoulder = 74.0f;
            contact.frontElbow = -116.0f;
            contact.rearShoulder = 108.0f;
            contact.rearElbow = -136.0f;
            contact.frontHandZ = 0.055f;
            contact.rearHandZ = -0.225f;
            stagger = contact;
            brace = base;
            brace.rootForward = -0.040f;
            break;

        case MOVE_HIT_HIGH:
            // The arms retain guard. The hit is communicated by head/chest and
            // a small COM displacement, not by throwing both arms behind the body.
            contact.rootForward = -0.022f;
            contact.torsoLean = -0.190f;
            contact.headLean = -0.245f;
            contact.frontShoulder = 54.0f;
            contact.frontElbow = -120.0f;
            contact.rearShoulder = 64.0f;
            contact.rearElbow = -142.0f;
            stagger = contact;
            stagger.rootForward = -0.055f;
            stagger.rootDown = 0.030f;
            stagger.torsoLean = -0.265f;
            stagger.headLean = -0.090f;
            stagger.pelvisTilt = -0.060f;
            stagger.frontShoulder = 60.0f;
            stagger.frontElbow = -112.0f;
            stagger.rearShoulder = 70.0f;
            stagger.rearElbow = -136.0f;
            brace = base;
            brace.rootForward = -0.020f;
            brace.rootDown = 0.020f;
            brace.torsoLean = -0.075f;
            brace.frontShoulder = 50.0f;
            brace.frontElbow = -124.0f;
            brace.rearShoulder = 60.0f;
            brace.rearElbow = -144.0f;
            break;

        case MOVE_HIT_LOW:
            contact.rootForward = -0.018f;
            contact.rootDown = 0.095f;
            contact.torsoLean = -0.105f;
            contact.pelvisTilt = -0.070f;
            contact.frontShoulder = 56.0f;
            contact.frontElbow = -118.0f;
            contact.rearShoulder = 64.0f;
            contact.rearElbow = -140.0f;
            stagger = contact;
            stagger.rootForward = -0.042f;
            stagger.rootDown = 0.125f;
            stagger.torsoLean = -0.165f;
            stagger.pelvisTilt = -0.110f;
            brace = base;
            brace.rootDown = 0.050f;
            brace.torsoLean = -0.045f;
            break;

        case MOVE_HIT_HEAVY:
            contact.rootForward = -0.032f;
            contact.rootDown = 0.010f;
            contact.torsoLean = -0.285f;
            contact.headLean = -0.275f;
            contact.pelvisTilt = -0.080f;
            contact.frontShoulder = 58.0f;
            contact.frontElbow = -112.0f;
            contact.rearShoulder = 68.0f;
            contact.rearElbow = -136.0f;
            stagger = contact;
            stagger.rootForward = -0.082f;
            stagger.rootDown = 0.050f;
            stagger.torsoLean = -0.410f;
            stagger.headLean = -0.105f;
            stagger.pelvisTilt = -0.145f;
            // Only the near arm breaks guard slightly; the far arm remains protective.
            stagger.frontShoulder = 72.0f;
            stagger.frontElbow = -98.0f;
            stagger.rearShoulder = 82.0f;
            stagger.rearElbow = -128.0f;
            brace = base;
            brace.rootForward = -0.030f;
            brace.rootDown = 0.045f;
            brace.torsoLean = -0.120f;
            brace.frontShoulder = 50.0f;
            brace.frontElbow = -122.0f;
            brace.rearShoulder = 60.0f;
            brace.rearElbow = -142.0f;
            break;

        case MOVE_KNOCKDOWN:
            contact.rootForward = -0.045f;
            contact.torsoLean = -0.340f;
            contact.headLean = -0.260f;
            contact.frontShoulder = 72.0f;
            contact.frontElbow = 42.0f;
            contact.rearShoulder = 112.0f;
            contact.rearElbow = -30.0f;
            stagger = contact;
            stagger.rootForward = -0.125f;
            stagger.rootDown = 0.345f;
            stagger.torsoLean = -1.030f;
            stagger.headLean = -0.120f;
            stagger.pelvisTilt = -0.42f;
            stagger.frontShoulder = 128.0f;
            stagger.frontElbow = 34.0f;
            stagger.rearShoulder = 148.0f;
            stagger.rearElbow = -24.0f;
            stagger.frontPlant = 0.18f;
            stagger.frontFootForward = 0.10f;
            stagger.frontFootDown = 0.18f;
            stagger.rearPlant = 0.18f;
            stagger.rearFootForward = -0.12f;
            stagger.rearFootDown = 0.20f;
            brace = stagger;
            break;

        case MOVE_GETUP:
            contact = base;
            contact.rootDown = 0.260f;
            contact.torsoLean = -0.62f;
            contact.frontPlant = 0.35f;
            contact.rearPlant = 0.35f;
            stagger = base;
            stagger.rootDown = 0.110f;
            stagger.torsoLean = -0.24f;
            brace = base;
            break;

        default:
            return base;
    }

    Pose out;
    if (move == MOVE_KNOCKDOWN) {
        if (u < 0.13f) out = blendPose(base, contact, u / 0.13f);
        else if (u < 0.64f) out = blendPose(contact, stagger, (u - 0.13f) / 0.51f);
        else out = stagger;
        return sanitizePose(out);
    }

    if (move == MOVE_GETUP) {
        if (u < 0.30f) out = contact;
        else if (u < 0.72f) out = blendPose(contact, stagger, (u - 0.30f) / 0.42f);
        else out = blendPose(stagger, brace, (u - 0.72f) / 0.28f);
        return sanitizePose(out);
    }

    // Contact is nearly instantaneous, then held. Stagger and recovery are
    // distinct trajectories instead of playing the contact pose backward.
    if (u < 0.12f) out = blendPose(base, contact, u / 0.12f);
    else if (u < 0.28f) out = contact;
    else if (u < 0.58f) out = blendPose(contact, stagger, (u - 0.28f) / 0.30f);
    else if (u < 0.80f) out = blendPose(stagger, brace, (u - 0.58f) / 0.22f);
    else out = blendPose(brace, base, (u - 0.80f) / 0.20f);
    return sanitizePose(out);
}

RobotFightGenerator::Pose RobotFightGenerator::samplePose(const Fighter& f) const {
    const Pose base = guardPose(f);
    if (f.airMode != AIR_GROUNDED) return airPose(f, base);
    if (f.move == MOVE_IDLE) return base;

    const MoveDef d = moveDefForStyle(f.move, f.style);
    const float u = clamp01(f.moveTime / std::max(0.001f, d.duration));
    if (f.move == MOVE_SPECIAL) {
        return specialPose(f, u, base);
    }
    if (f.move >= MOVE_JAB && f.move <= MOVE_HEAVY) {
        return attackPose(f.move, u, base);
    }
    return reactionPose(f.move, u, base);
}


void RobotFightGenerator::plantDefaultStance(Fighter& f) {
    const StyleProfile sp = styleProfile(f.style);
    const float front = 0.19f * sp.stanceScale;
    const float rear = -0.17f * sp.stanceScale;
    f.frontFootX = f.x + static_cast<float>(f.facing) * front;
    f.rearFootX = f.x + static_cast<float>(f.facing) * rear;
    f.frontFootY = 0.0f;
    f.rearFootY = 0.0f;
}

void RobotFightGenerator::shiftFighterWorld(Fighter& f, float dx) {
    if (std::fabs(dx) < 0.000001f) return;
    f.x += dx;
    f.frontFootX += dx;
    f.rearFootX += dx;
    f.stepStartX += dx;
    f.stepTargetX += dx;
    f.swingStartX += dx;
    f.swingTargetX += dx;
    f.trailStartX += dx;
    f.trailTargetX += dx;
    f.airStartX += dx;
    f.airTargetX += dx;
}

void RobotFightGenerator::cancelStep(Fighter& f) {
    if (f.stepActive) {
        if (f.stepSwingFront) f.frontFootY = 0.0f;
        else f.rearFootY = 0.0f;
    }
    f.stepActive = false;
    f.stepTime = 0.0f;
    f.stepDuration = 0.0f;
    f.stepStartX = f.x;
    f.stepTargetX = f.x;
    f.stepDirection = 0.0f;
    f.swingStartX = f.stepSwingFront ? f.frontFootX : f.rearFootX;
    f.swingTargetX = f.swingStartX;
    f.trailStartX = f.stepSwingFront ? f.rearFootX : f.frontFootX;
    f.trailTargetX = f.trailStartX;
}

void RobotFightGenerator::startStep(Fighter& f, float forwardAmount, float duration) {
    if (roundOver || f.move != MOVE_IDLE || f.stepActive || f.airMode != AIR_GROUNDED) return;

    const StyleProfile sp = styleProfile(f.style);
    forwardAmount *= sp.stepScale;
    duration *= sp.stepDurationScale;
    forwardAmount = clampRange(forwardAmount, -0.18f, 0.18f);
    if (std::fabs(forwardAmount) < 0.008f) return;

    f.stepActive = true;
    f.stepStartX = f.x;
    f.stepTargetX = clampRange(
        f.x + static_cast<float>(f.facing) * forwardAmount, -0.78f, 0.78f);
    f.stepTime = 0.0f;
    f.stepDuration = clampRange(duration, 0.20f, 0.56f);
    f.stepDirection = (forwardAmount >= 0.0f) ? 1.0f : -1.0f;

    // Forward travel naturally leads with the front foot; repeated pressure
    // alternates so the stance does not continually widen. Backward travel
    // favors the rear foot first.
    const bool preferred = (forwardAmount >= 0.0f) ? f.nextSwingFront : !f.nextSwingFront;
    f.stepSwingFront = preferred;
    f.nextSwingFront = !preferred;

    const float desiredFront = f.stepTargetX + static_cast<float>(f.facing) * (0.19f * sp.stanceScale);
    const float desiredRear = f.stepTargetX - static_cast<float>(f.facing) * (0.17f * sp.stanceScale);
    if (f.stepSwingFront) {
        f.swingStartX = f.frontFootX;
        f.swingTargetX = desiredFront;
        f.trailStartX = f.rearFootX;
        f.trailTargetX = desiredRear;
    } else {
        f.swingStartX = f.rearFootX;
        f.swingTargetX = desiredRear;
        f.trailStartX = f.frontFootX;
        f.trailTargetX = desiredFront;
    }
}

void RobotFightGenerator::updateStep(Fighter& f, float dt) {
    if (!f.stepActive) return;

    f.stepTime += dt;
    const float t = clamp01(f.stepTime / std::max(0.001f, f.stepDuration));
    const float split = 0.58f;
    float leadPhase = 0.0f;
    float trailPhase = 0.0f;
    float rootProgress = 0.0f;

    if (t < split) {
        leadPhase = smooth01(t / split);
        rootProgress = 0.64f * smoother01(t / split);
    } else {
        leadPhase = 1.0f;
        trailPhase = smooth01((t - split) / (1.0f - split));
        rootProgress = 0.64f + 0.36f * smoother01((t - split) / (1.0f - split));
    }

    f.x = lerpf(f.stepStartX, f.stepTargetX, rootProgress);
    const float leadLift = std::sin(leadPhase * PI_F);
    const float trailLift = std::sin(trailPhase * PI_F);
    const float distanceScale = std::fabs(f.stepTargetX - f.stepStartX) / 0.18f;
    const float liftHeight = 0.052f + 0.022f * clamp01(distanceScale);

    if (f.stepSwingFront) {
        f.frontFootX = lerpf(f.swingStartX, f.swingTargetX, leadPhase);
        f.frontFootY = -leadLift * liftHeight;
        f.rearFootX = lerpf(f.trailStartX, f.trailTargetX, trailPhase);
        f.rearFootY = -trailLift * (liftHeight * 0.78f);
    } else {
        f.rearFootX = lerpf(f.swingStartX, f.swingTargetX, leadPhase);
        f.rearFootY = -leadLift * liftHeight;
        f.frontFootX = lerpf(f.trailStartX, f.trailTargetX, trailPhase);
        f.frontFootY = -trailLift * (liftHeight * 0.78f);
    }

    if (t >= 1.0f) {
        f.x = f.stepTargetX;
        const StyleProfile sp = styleProfile(f.style);
        f.frontFootX = f.stepTargetX + static_cast<float>(f.facing) * (0.19f * sp.stanceScale);
        f.rearFootX = f.stepTargetX - static_cast<float>(f.facing) * (0.17f * sp.stanceScale);
        f.frontFootY = 0.0f;
        f.rearFootY = 0.0f;
        cancelStep(f);
    }
}

void RobotFightGenerator::startAir(
        Fighter& f, MoveType command, Outcome outcome,
        float forwardAmount, float duration) {
    if (roundOver || f.move != MOVE_IDLE || f.stepActive || f.airMode != AIR_GROUNDED) return;

    cancelStep(f);
    f.airMode = AIR_GROUNDED;
    if (command == MOVE_JUMP_KICK) f.airMode = AIR_JUMP_KICK;
    else if (command == MOVE_HOP_BACK) f.airMode = AIR_HOP_BACK;
    else if (command == MOVE_JUMP_PUNCH) f.airMode = AIR_JUMP_PUNCH;
    else return;

    const StyleProfile sp = styleProfile(f.style);
    f.airTime = 0.0f;
    f.airDuration = clampRange(duration, 0.54f, 1.02f);
    f.airStartX = f.x;
    const float scaledForward = forwardAmount * (0.92f + 0.08f * sp.stepScale);
    f.airTargetX = clampRange(
        f.x + static_cast<float>(f.facing) * scaledForward, -0.78f, 0.78f);
    f.airOutcome = outcome;
    f.airHitResolved = false;
    f.airHitFrac = 1.0f;

    if (f.airMode == AIR_JUMP_KICK) {
        f.airHeight = (f.style == STYLE_AGILE) ? 0.285f : 0.255f;
        f.airHitFrac = moveDef(MOVE_JUMP_KICK).hitFrac;
    } else if (f.airMode == AIR_JUMP_PUNCH) {
        f.airHeight = (f.style == STYLE_AGILE) ? 0.265f : 0.235f;
        f.airHitFrac = moveDef(MOVE_JUMP_PUNCH).hitFrac;
    } else {
        f.airHeight = (f.style == STYLE_AGILE) ? 0.205f : 0.170f;
    }

    // Once airborne, foot anchors are presentation history only; both feet are
    // released by airPose(). Landing replants them at the new root location.
    f.landingTime = f.landingDuration;
}

void RobotFightGenerator::finishAir(Fighter& f, bool compressedLanding) {
    if (f.airMode == AIR_GROUNDED) return;
    f.airMode = AIR_GROUNDED;
    f.airTime = 0.0f;
    f.airDuration = 0.0f;
    f.airStartX = f.x;
    f.airTargetX = f.x;
    f.airHeight = 0.0f;
    f.airHitFrac = 1.0f;
    f.airHitResolved = false;
    plantDefaultStance(f);

    if (compressedLanding) {
        f.landingTime = 0.0f;
        f.landingDuration = 0.16f + rand01() * 0.08f;
        float strength = 0.055f;
        if (f.style == STYLE_HEAVY) strength = 0.075f;
        else if (f.style == STYLE_AGILE) strength = 0.042f;
        f.landingStrength = strength;
    } else {
        f.landingTime = f.landingDuration;
        f.landingStrength = 0.0f;
    }
}

void RobotFightGenerator::updateAir(Fighter& f, float dt) {
    if (f.airMode == AIR_GROUNDED) return;

    f.airTime += dt;
    const float u = clamp01(f.airTime / std::max(0.001f, f.airDuration));
    f.x = lerpf(f.airStartX, f.airTargetX, smoother01(u));

    if (!f.airHitResolved &&
        (f.airMode == AIR_JUMP_KICK || f.airMode == AIR_JUMP_PUNCH) &&
        u >= f.airHitFrac) {
        const int index = (&f == &fighters[0]) ? 0 : 1;
        const MoveType strike = (f.airMode == AIR_JUMP_KICK)
            ? MOVE_JUMP_KICK : MOVE_JUMP_PUNCH;
        applyHit(index, 1 - index, strike, f.airOutcome);
        f.airHitResolved = true;
    }

    if (u >= 1.0f) {
        f.x = f.airTargetX;
        finishAir(f, true);
    }
}

bool RobotFightGenerator::anyStepActive() const {
    return fighters[0].stepActive || fighters[1].stepActive;
}

bool RobotFightGenerator::anyAirborne() const {
    return fighters[0].airMode != AIR_GROUNDED || fighters[1].airMode != AIR_GROUNDED;
}

void RobotFightGenerator::resetRound() {
    fighters[0].x = -0.48f;
    fighters[0].z = -0.03f;
    fighters[0].facing = 1;
    fighters[0].health = 100.0f;
    fighters[0].move = MOVE_IDLE;
    fighters[0].moveTime = 0.0f;
    fighters[0].outcome = OUTCOME_HIT;
    fighters[0].hitResolved = false;
    fighters[0].flash = 0.0f;
    fighters[0].idlePhase = 0.0f;
    fighters[0].neutralBehavior = NEUTRAL_SETTLE;
    fighters[0].neutralBehaviorTime = 0.0f;
    fighters[0].neutralBehaviorDuration = 0.62f;
    fighters[0].nextSwingFront = true;
    fighters[0].stepActive = false;
    fighters[0].stepSwingFront = true;
    fighters[0].stepStartX = fighters[0].x;
    fighters[0].stepTargetX = fighters[0].x;
    fighters[0].stepTime = 0.0f;
    fighters[0].stepDuration = 0.0f;
    fighters[0].stepDirection = 0.0f;
    fighters[0].swingStartX = fighters[0].x;
    fighters[0].swingTargetX = fighters[0].x;
    fighters[0].trailStartX = fighters[0].x;
    fighters[0].trailTargetX = fighters[0].x;
    fighters[0].airMode = AIR_GROUNDED;
    fighters[0].airTime = 0.0f;
    fighters[0].airDuration = 0.0f;
    fighters[0].airStartX = fighters[0].x;
    fighters[0].airTargetX = fighters[0].x;
    fighters[0].airHeight = 0.0f;
    fighters[0].airHitFrac = 1.0f;
    fighters[0].airOutcome = OUTCOME_HIT;
    fighters[0].airHitResolved = false;
    fighters[0].landingTime = 1.0f;
    fighters[0].landingDuration = 0.0f;
    fighters[0].landingStrength = 0.0f;
    fighters[0].impactX = 0.0f;
    fighters[0].impactVX = 0.0f;
    fighters[0].impactY = 0.0f;
    fighters[0].impactVY = 0.0f;
    fighters[0].chassis = randInt(0, 2);
    fighters[0].style = static_cast<FighterStyle>(randInt(0, STYLE_COUNT - 1));
    fighters[0].momentum = 0.50f;
    plantDefaultStance(fighters[0]);

    fighters[1].x = 0.48f;
    fighters[1].z = 0.03f;
    fighters[1].facing = -1;
    fighters[1].health = 100.0f;
    fighters[1].move = MOVE_IDLE;
    fighters[1].moveTime = 0.0f;
    fighters[1].outcome = OUTCOME_HIT;
    fighters[1].hitResolved = false;
    fighters[1].flash = 0.0f;
    fighters[1].idlePhase = 1.7f;
    fighters[1].neutralBehavior = NEUTRAL_GUARD_TWITCH;
    fighters[1].neutralBehaviorTime = 0.18f;
    fighters[1].neutralBehaviorDuration = 0.74f;
    fighters[1].nextSwingFront = true;
    fighters[1].stepActive = false;
    fighters[1].stepSwingFront = true;
    fighters[1].stepStartX = fighters[1].x;
    fighters[1].stepTargetX = fighters[1].x;
    fighters[1].stepTime = 0.0f;
    fighters[1].stepDuration = 0.0f;
    fighters[1].stepDirection = 0.0f;
    fighters[1].swingStartX = fighters[1].x;
    fighters[1].swingTargetX = fighters[1].x;
    fighters[1].trailStartX = fighters[1].x;
    fighters[1].trailTargetX = fighters[1].x;
    fighters[1].airMode = AIR_GROUNDED;
    fighters[1].airTime = 0.0f;
    fighters[1].airDuration = 0.0f;
    fighters[1].airStartX = fighters[1].x;
    fighters[1].airTargetX = fighters[1].x;
    fighters[1].airHeight = 0.0f;
    fighters[1].airHitFrac = 1.0f;
    fighters[1].airOutcome = OUTCOME_HIT;
    fighters[1].airHitResolved = false;
    fighters[1].landingTime = 1.0f;
    fighters[1].landingDuration = 0.0f;
    fighters[1].landingStrength = 0.0f;
    fighters[1].impactX = 0.0f;
    fighters[1].impactVX = 0.0f;
    fighters[1].impactY = 0.0f;
    fighters[1].impactVY = 0.0f;
    fighters[1].chassis = randInt(0, 2);
    if (fighters[1].chassis == fighters[0].chassis) {
        fighters[1].chassis = (fighters[1].chassis + 1 + (roundNumber & 1)) % 3;
    }
    fighters[1].style = static_cast<FighterStyle>(randInt(0, STYLE_COUNT - 1));
    if (fighters[1].style == fighters[0].style) {
        fighters[1].style = static_cast<FighterStyle>(
            (static_cast<int>(fighters[1].style) + 1 + (roundNumber % (STYLE_COUNT - 1))) % STYLE_COUNT);
    }
    fighters[1].momentum = 0.50f;
    plantDefaultStance(fighters[1]);

    actionCount = 0;
    nextAction = 0;
    currentExchangeId = -1;
    exchangeTime = 0.0f;
    exchangeDuration = 0.0f;
    neutralTimer = 0.65f;
    exchangeActive = false;
    roundOver = false;
    roundTimer = 0.0f;
    initiativeOwner = (roundNumber & 1) ? 0 : 1;
    exchangeNextInitiative = initiativeOwner;
    clashCooldown = 11.0f + rand01() * 8.0f;
    airCooldown = 4.0f + rand01() * 4.0f;
    signatureCooldown[0] = 2.0f + rand01() * 3.5f;
    signatureCooldown[1] = 2.0f + rand01() * 3.5f;
    specialCooldown[0] = 8.0f + rand01() * 8.0f;
    specialCooldown[1] = 8.0f + rand01() * 8.0f;
    specialBanner = 0.0f;
    specialBannerActor = -1;

    cameraCenterX = 0.0f;
    cameraFramingZoom = 1.0f;
}

void RobotFightGenerator::reset() {
    rngState = 0x6D2B79F5u;
    for (int i = 0; i < 48; ++i) {
        sparks[i].active = false;
        sparks[i].age = 0.0f;
        sparks[i].life = 0.0f;
        sparks[i].size = 0.0f;
    }

    energySmooth = 0.0f;
    triggerBoost = 0.0f;
    triggerDensity = 0.0f;
    timeSinceTrigger = 10.0f;
    hitStopRemaining = 0.0f;
    cameraShakeX = 0.0f;
    cameraShakeY = 0.0f;
    cameraZoomImpulse = 0.0f;
    cameraCenterX = 0.0f;
    cameraFramingZoom = 1.0f;
    screenFlash = 0.0f;
    roundNumber = 1;
    lastWallTime = ofGetElapsedTimef();

    resetRound();
    ready = true;
    ofLogNotice("BRIDGE")
        << "RobotFightGenerator reset: V0.8 style signatures + momentum-gated specials ready";
}

void RobotFightGenerator::startMove(Fighter& f, MoveType move, Outcome outcome) {
    if (move != MOVE_IDLE) {
        cancelStep(f);
        if (f.airMode != AIR_GROUNDED) finishAir(f, false);
    }
    f.move = move;
    f.moveTime = 0.0f;
    f.outcome = outcome;
    f.hitResolved = false;
}

void RobotFightGenerator::updateFighter(Fighter& f, float dt) {
    float idleTempo = 1.0f;
    float settleTempo = 1.0f;
    switch (f.style) {
        case STYLE_PRESSURE: idleTempo = 1.08f; settleTempo = 1.02f; break;
        case STYLE_COUNTER: idleTempo = 0.94f; settleTempo = 0.96f; break;
        case STYLE_KICKER: idleTempo = 1.00f; settleTempo = 0.98f; break;
        case STYLE_HEAVY: idleTempo = 0.78f; settleTempo = 0.82f; break;
        case STYLE_AGILE: idleTempo = 1.24f; settleTempo = 1.16f; break;
        case STYLE_BRAWLER: idleTempo = 1.05f; settleTempo = 0.88f; break;
        default: break;
    }
    f.idlePhase += dt * (2.0f + energySmooth * 0.70f) * idleTempo;
    f.flash = std::max(0.0f, f.flash - dt * 4.5f);
    updateNeutralBehavior(f, dt);

    updateAir(f, dt);
    if (f.airMode == AIR_GROUNDED && f.landingDuration > 0.001f &&
        f.landingTime < f.landingDuration) {
        f.landingTime = std::min(f.landingDuration, f.landingTime + dt);
    }
    updateStep(f, dt);

    // Exact critically damped return to the stance center. No oscillation and
    // no permanent skating from a hit impulse.
    criticalSpringZero(f.impactX, f.impactVX, 9.5f * settleTempo, dt);
    criticalSpringZero(f.impactY, f.impactVY, 11.0f * settleTempo, dt);
    f.impactX = clampRange(f.impactX, -0.105f, 0.105f);
    f.impactY = clampRange(f.impactY, -0.060f, 0.090f);

    if (f.move == MOVE_IDLE) return;

    const MoveDef d = moveDefForStyle(f.move, f.style);
    f.moveTime += dt;

    if (f.move >= MOVE_JAB && f.move <= MOVE_SPECIAL && !f.hitResolved) {
        if (f.moveTime >= d.duration * d.hitFrac) {
            const int index = (&f == &fighters[0]) ? 0 : 1;
            resolveAttack(index);
            f.hitResolved = true;
        }
    }

    if (f.moveTime >= d.duration) {
        if (f.move == MOVE_KNOCKDOWN && roundOver) {
            f.moveTime = d.duration;
        } else {
            f.move = MOVE_IDLE;
            f.moveTime = 0.0f;
            f.hitResolved = false;
        }
    }
}

void RobotFightGenerator::enforceSpacingBounds() {
    for (int i = 0; i < 2; ++i) {
        const float clamped = clampRange(fighters[i].x, -0.82f, 0.82f);
        shiftFighterWorld(fighters[i], clamped - fighters[i].x);
    }

    float sep = fighters[1].x - fighters[0].x;
    if (sep < 0.48f) {
        const float push = (0.48f - sep) * 0.5f;
        shiftFighterWorld(fighters[0], -push);
        shiftFighterWorld(fighters[1], push);
    }

    sep = fighters[1].x - fighters[0].x;
    if (sep > 1.38f && !exchangeActive) {
        const float pull = (sep - 1.38f) * 0.08f;
        shiftFighterWorld(fighters[0], pull);
        shiftFighterWorld(fighters[1], -pull);
    }
}

void RobotFightGenerator::maintainNeutralSpacing(float dt) {
    (void)dt;
    if (roundOver || exchangeActive) return;
    if (fighters[0].move != MOVE_IDLE || fighters[1].move != MOVE_IDLE) return;
    if (anyStepActive() || anyAirborne()) return;

    const StyleProfile sp0 = styleProfile(fighters[0].style);
    const StyleProfile sp1 = styleProfile(fighters[1].style);
    const float styleSep = (sp0.preferredSep + sp1.preferredSep) * 0.5f;
    const float desiredSep = clampRange(
        styleSep - kCombat * 0.055f - energySmooth * 0.030f, 0.70f, 0.99f);
    const float momentumDiff = fighters[0].momentum - fighters[1].momentum;
    const float targetCenter = clampRange(momentumDiff * 0.18f, -0.11f, 0.11f);
    const float targets[2] = {
        targetCenter - desiredSep * 0.5f,
        targetCenter + desiredSep * 0.5f
    };
    const float errors[2] = {
        targets[0] - fighters[0].x,
        targets[1] - fighters[1].x
    };

    // Only one fighter performs a neutral spacing adjustment at a time.  This
    // avoids the mirrored "both shuffle together" look and makes initiative
    // visible even before an exchange begins.
    int mover = -1;
    const float e0 = std::fabs(errors[0]);
    const float e1 = std::fabs(errors[1]);
    if (e0 > 0.050f || e1 > 0.050f) {
        if (std::fabs(e0 - e1) < 0.020f) {
            mover = (rand01() < 0.64f) ? initiativeOwner : (1 - initiativeOwner);
        } else {
            mover = (e0 > e1) ? 0 : 1;
        }
    } else {
        // Very occasional probe without changing long-term spacing. Pressure
        // and brawler personalities are more willing to test range; counter
        // and heavy fighters are happier to make the opponent come to them.
        float probeChance = 0.008f + kCombat * 0.009f;
        switch (fighters[initiativeOwner].style) {
            case STYLE_PRESSURE: probeChance *= 1.75f; break;
            case STYLE_BRAWLER: probeChance *= 1.55f; break;
            case STYLE_AGILE: probeChance *= 1.30f; break;
            case STYLE_COUNTER: probeChance *= 0.62f; break;
            case STYLE_HEAVY: probeChance *= 0.72f; break;
            default: break;
        }
        if (rand01() < probeChance) mover = initiativeOwner;
    }

    if (mover < 0) return;
    float worldDelta = errors[mover];
    if (std::fabs(worldDelta) <= 0.050f) {
        worldDelta = static_cast<float>(fighters[mover].facing) *
                     (0.026f + rand01() * 0.018f);
    }
    worldDelta = clampRange(worldDelta, -0.072f, 0.072f);
    const float forwardAmount = worldDelta * static_cast<float>(fighters[mover].facing);
    const float duration = 0.27f + rand01() * 0.16f;
    startStep(fighters[mover], forwardAmount, duration);
}

ofVec2f RobotFightGenerator::impactPoint(const Fighter& attacker, MoveType move) const {
    float y = -0.78f;
    float reach = 0.27f;
    switch (move) {
        case MOVE_BODY: y = -0.62f; reach = 0.30f; break;
        case MOVE_LOW_KICK: y = -0.27f; reach = 0.34f; break;
        case MOVE_ROUNDHOUSE: y = -0.58f; reach = 0.41f; break;
        case MOVE_HEAVY: y = -0.82f; reach = 0.35f; break;
        case MOVE_SPECIAL:
            switch (attacker.style) {
                case STYLE_KICKER: y = -0.72f; reach = 0.43f; break;
                case STYLE_HEAVY: y = -0.78f; reach = 0.37f; break;
                case STYLE_BRAWLER: y = -0.70f; reach = 0.36f; break;
                default: y = -0.82f; reach = 0.37f; break;
            }
            break;
        case MOVE_JUMP_KICK: y = -0.82f; reach = 0.39f; break;
        case MOVE_JUMP_PUNCH: y = -0.84f; reach = 0.34f; break;
        case MOVE_CROSS: y = -0.80f; reach = 0.33f; break;
        case MOVE_JAB:
        default: y = -0.79f; reach = 0.30f; break;
    }
    return ofVec2f(attacker.x + attacker.facing * reach, y);
}

void RobotFightGenerator::spawnSparks(const ofVec2f& pos, int count, float force) {
    int spawned = 0;
    for (int i = 0; i < 48 && spawned < count; ++i) {
        if (sparks[i].active) continue;
        const float a = rand01() * PI_F * 2.0f;
        const float s = (0.25f + rand01() * 0.75f) * force;
        sparks[i].active = true;
        sparks[i].pos = pos;
        sparks[i].vel.set(std::cos(a) * s, std::sin(a) * s - 0.18f * force);
        sparks[i].age = 0.0f;
        sparks[i].life = 0.16f + rand01() * 0.24f;
        sparks[i].size = 0.008f + rand01() * 0.012f;
        ++spawned;
    }
}

void RobotFightGenerator::updateSparks(float dt) {
    for (int i = 0; i < 48; ++i) {
        Spark& s = sparks[i];
        if (!s.active) continue;
        s.age += dt;
        if (s.age >= s.life) {
            s.active = false;
            continue;
        }
        s.vel.y += dt * 1.4f;
        s.pos += s.vel * dt;
        s.vel *= std::exp(-dt * 2.6f);
    }
}


void RobotFightGenerator::applyHit(
        int attackerIndex, int defenderIndex, MoveType move, Outcome outcome) {
    Fighter& a = fighters[attackerIndex];
    Fighter& d = fighters[defenderIndex];
    const MoveDef md = moveDefForStyle(move, a.style);
    const float defenderRecoil = styleProfile(d.style).recoilScale;
    ofVec2f p = impactPoint(a, move);

    const float defenderContactX = d.x - static_cast<float>(a.facing) * 0.14f;
    p.x = lerpf(p.x, defenderContactX, 0.62f);

    if (outcome == OUTCOME_MISS) {
        cancelStep(d);
        if (d.airMode == AIR_GROUNDED) {
            startMove(d, MOVE_EVADE, OUTCOME_MISS);
        }
        a.momentum = std::max(0.0f, a.momentum - 0.035f);
        d.momentum = std::min(1.0f, d.momentum + 0.055f);
        cameraShakeX += (rand01() - 0.5f) * 0.003f * kCinema;
        return;
    }

    if (outcome == OUTCOME_CLASH) {
        cancelStep(fighters[0]);
        cancelStep(fighters[1]);
        p.x = (fighters[0].x + fighters[1].x) * 0.5f;
        spawnSparks(p, 16, 0.68f);
        hitStopRemaining = std::max(hitStopRemaining, 0.032f + 0.046f * kCinema);
        cameraShakeX += (rand01() - 0.5f) * 0.032f * kCinema;
        cameraShakeY += (rand01() - 0.5f) * 0.022f * kCinema;
        cameraZoomImpulse = std::max(cameraZoomImpulse, 0.030f + 0.046f * kCinema);
        screenFlash = std::max(screenFlash, 0.36f);
        fighters[0].impactX -= 0.012f;
        fighters[0].impactVX -= 0.42f;
        fighters[1].impactX += 0.012f;
        fighters[1].impactVX += 0.42f;
        fighters[0].momentum *= 0.97f;
        fighters[1].momentum *= 0.97f;
        return;
    }

    if (outcome == OUTCOME_BLOCK) {
        cancelStep(d);
        const bool low = (move == MOVE_LOW_KICK);
        startMove(d, low ? MOVE_BLOCK_LOW : MOVE_BLOCK_HIGH, OUTCOME_BLOCK);
        const float away = static_cast<float>(a.facing);
        d.impactX += away * (0.007f + md.knockback * 0.10f) * defenderRecoil;
        d.impactVX += away * (0.16f + md.knockback * 2.0f) * defenderRecoil;
        spawnSparks(p, 7, 0.42f);
        hitStopRemaining = std::max(hitStopRemaining, 0.014f + md.hitStop * 0.45f * kCinema);
        cameraShakeX += (rand01() - 0.5f) * 0.014f * kCinema;
        cameraShakeY += (rand01() - 0.5f) * 0.010f * kCinema;
        screenFlash = std::max(screenFlash, 0.11f);
        a.momentum = std::min(1.0f, a.momentum + 0.018f);
        d.momentum = std::min(1.0f, d.momentum + 0.012f);
        return;
    }

    cancelStep(d);
    d.health = std::max(0.0f, d.health - md.damage);
    d.flash = 1.0f;

    const float away = static_cast<float>(a.facing);
    const bool special = (move == MOVE_SPECIAL);
    const bool heavy = (move == MOVE_ROUNDHOUSE || move == MOVE_HEAVY ||
                        move == MOVE_JUMP_KICK || special);
    d.impactX += away * (heavy ? 0.018f : 0.010f) * defenderRecoil;
    d.impactVX += away * (0.24f + md.damage * (heavy ? 0.038f : 0.024f)) * defenderRecoil;
    if (move == MOVE_BODY || move == MOVE_LOW_KICK) {
        d.impactY += 0.008f * defenderRecoil;
        d.impactVY += 0.055f * defenderRecoil;
    }

    if (move == MOVE_LOW_KICK) {
        startMove(d, MOVE_HIT_LOW, OUTCOME_HIT);
    } else if (heavy) {
        startMove(d, MOVE_HIT_HEAVY, OUTCOME_HIT);
    } else {
        startMove(d, MOVE_HIT_HIGH, OUTCOME_HIT);
    }

    a.momentum = std::min(1.0f, a.momentum + 0.045f + md.damage * 0.004f);
    d.momentum = std::max(0.0f, d.momentum - 0.030f - md.damage * 0.003f);

    const int sparkBonus = special ? 7 : 0;
    const float forceBonus = special ? 0.10f : 0.0f;
    spawnSparks(p, 10 + sparkBonus + static_cast<int>(md.damage * 0.45f),
                0.46f + forceBonus + md.damage * 0.018f);
    hitStopRemaining = std::max(hitStopRemaining, md.hitStop * (0.65f + 0.95f * kCinema));
    cameraShakeX += (rand01() - 0.5f) * md.cameraPunch * (special ? 0.070f : 0.055f) * kCinema;
    cameraShakeY += (rand01() - 0.5f) * md.cameraPunch * (special ? 0.048f : 0.035f) * kCinema;
    cameraZoomImpulse = std::max(cameraZoomImpulse, md.cameraPunch * (special ? 0.058f : 0.044f) * kCinema);
    screenFlash = std::max(screenFlash, (special ? 0.28f : 0.18f) + md.cameraPunch * 0.28f);

    if (d.health <= 0.0f) {
        startMove(d, MOVE_KNOCKDOWN, OUTCOME_HIT);
        d.impactVX += away * 0.30f;
        roundOver = true;
        roundTimer = 2.45f;
        exchangeActive = false;
        actionCount = 0;
        nextAction = 0;
        hitStopRemaining = std::max(hitStopRemaining, 0.075f + 0.055f * kCinema);
        cameraZoomImpulse = std::max(cameraZoomImpulse, 0.085f * kCinema);
        initiativeOwner = attackerIndex;
    }
}

void RobotFightGenerator::resolveAttack(int attackerIndex) {
    if (roundOver) return;
    Fighter& a = fighters[attackerIndex];
    if (a.move < MOVE_JAB || a.move > MOVE_SPECIAL) return;
    applyHit(attackerIndex, 1 - attackerIndex, a.move, a.outcome);
}

void RobotFightGenerator::loadExchange(int id, bool flipActors) {
    currentExchangeId = id;
    actionCount = 0;
    nextAction = 0;
    exchangeTime = 0.0f;

    const int A = flipActors ? 1 : 0;
    const int B = 1 - A;

    switch (id) {
        case 0: // probing pressure -> counter and separation
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT,  0.10f, 0.28f};
            actions[1] = {0.31f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[2] = {0.80f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_MISS, 0.0f, 0.0f};
            actions[3] = {1.42f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT,  0.10f, 0.24f};
            actions[4] = {1.69f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {2.34f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.28f};
            actionCount = 6; exchangeDuration = 2.72f; exchangeNextInitiative = B;
            break;

        case 1: // low strike -> failed heavy -> counter
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.24f};
            actions[1] = {0.27f, A, ACTION_MOVE, MOVE_LOW_KICK, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.92f, B, ACTION_MOVE, MOVE_JAB, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[3] = {1.36f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_MISS, 0.0f, 0.0f};
            actions[4] = {2.24f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.22f};
            actions[5] = {2.50f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 6; exchangeDuration = 3.12f; exchangeNextInitiative = B;
            break;

        case 2: // clean combo with pursuit between impacts
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.12f, 0.28f};
            actions[1] = {0.31f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.72f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[3] = {1.25f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.20f};
            actions[4] = {1.48f, A, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {2.28f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.28f};
            actionCount = 6; exchangeDuration = 2.66f; exchangeNextInitiative = A;
            break;

        case 3: // both close distance, then clash
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.24f};
            actions[1] = {0.00f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.24f};
            actions[2] = {0.30f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_CLASH, 0.0f, 0.0f};
            actions[3] = {0.30f, B, ACTION_MOVE, MOVE_HEAVY, OUTCOME_CLASH, 0.0f, 0.0f};
            actions[4] = {1.28f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.25f};
            actions[5] = {1.28f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.25f};
            actionCount = 6; exchangeDuration = 1.68f; exchangeNextInitiative = A;
            break;

        case 4: // body pressure is checked, defender takes over
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.09f, 0.26f};
            actions[1] = {0.29f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[2] = {0.86f, B, ACTION_MOVE, MOVE_LOW_KICK, OUTCOME_HIT, 0.0f, 0.0f};
            actions[3] = {1.60f, A, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_MISS, 0.0f, 0.0f};
            actions[4] = {2.43f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.24f};
            actions[5] = {2.71f, B, ACTION_MOVE, MOVE_HEAVY, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 6; exchangeDuration = 3.65f; exchangeNextInitiative = B;
            break;

        case 5: // back-and-forth, each side visibly takes ground
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.24f};
            actions[1] = {0.27f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.80f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.20f};
            actions[3] = {1.04f, B, ACTION_MOVE, MOVE_JAB, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {1.50f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[5] = {2.13f, B, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 6; exchangeDuration = 2.94f; exchangeNextInitiative = B;
            break;

        case 6: // sustained pressure / pursuit
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.13f, 0.28f};
            actions[1] = {0.31f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.84f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.20f};
            actions[3] = {1.07f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[4] = {1.62f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.05f, 0.18f};
            actions[5] = {1.84f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 6; exchangeDuration = 2.72f; exchangeNextInitiative = A;
            break;

        case 7: // bait, retreat, punish
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.22f};
            actions[1] = {0.25f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_MISS, 0.0f, 0.0f};
            actions[2] = {0.66f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.24f};
            actions[3] = {0.80f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.13f, 0.28f};
            actions[4] = {1.12f, B, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {1.68f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.05f, 0.18f};
            actions[6] = {1.90f, B, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 7; exchangeDuration = 2.80f; exchangeNextInitiative = B;
            break;

        case 8: // long-range kicking pressure
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.06f, 0.24f};
            actions[1] = {0.27f, A, ACTION_MOVE, MOVE_LOW_KICK, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.92f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.24f};
            actions[3] = {1.18f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.23f};
            actions[4] = {1.46f, A, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[5] = {2.34f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.06f, 0.24f};
            actionCount = 6; exchangeDuration = 2.72f; exchangeNextInitiative = A;
            break;

        case 9: // invitation -> evade -> compact counter combination
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.24f};
            actions[1] = {0.28f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_MISS, 0.0f, 0.0f};
            actions[2] = {0.72f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actions[3] = {1.24f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.05f, 0.20f};
            actions[4] = {1.48f, B, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {2.10f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.26f};
            actionCount = 6; exchangeDuration = 2.52f; exchangeNextInitiative = B;
            break;

        case 10: // heavy walkdown: compact pressure into one committed blow
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.34f};
            actions[1] = {0.38f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[2] = {0.98f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.06f, 0.30f};
            actions[3] = {1.34f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {2.30f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.09f, 0.30f};
            actionCount = 5; exchangeDuration = 2.66f; exchangeNextInitiative = A;
            break;

        case 11: // touch, exit, let the chase miss, then re-enter
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.20f};
            actions[1] = {0.23f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.66f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.10f, 0.22f};
            actions[3] = {0.82f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.24f};
            actions[4] = {1.12f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_MISS, 0.0f, 0.0f};
            actions[5] = {1.68f, A, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_HIT, 0.0f, 0.0f};
            actions[6] = {2.50f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.22f};
            actionCount = 7; exchangeDuration = 2.82f; exchangeNextInitiative = A;
            break;

        case 12: // jump-kick entry: one clean airborne commitment, then reset
            actions[0] = {0.00f, A, ACTION_AIR, MOVE_JUMP_KICK, OUTCOME_HIT, 0.17f, 0.86f};
            actions[1] = {0.98f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.05f, 0.24f};
            actions[2] = {1.26f, B, ACTION_MOVE, MOVE_JAB, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[3] = {1.76f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.05f, 0.24f};
            actionCount = 4; exchangeDuration = 2.08f; exchangeNextInitiative = B;
            break;

        case 13: // hop-back counter: lead yields range, challenger chases and misses
            actions[0] = {0.00f, A, ACTION_AIR, MOVE_HOP_BACK, OUTCOME_MISS, -0.15f, 0.64f};
            actions[1] = {0.06f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.22f};
            actions[2] = {0.28f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_MISS, 0.0f, 0.0f};
            actions[3] = {0.82f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.09f, 0.20f};
            actions[4] = {1.05f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {1.66f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.24f};
            actionCount = 6; exchangeDuration = 1.98f; exchangeNextInitiative = A;
            break;

        case 14: // leap punch: forward aerial strike with compact follow-up
            actions[0] = {0.00f, A, ACTION_AIR, MOVE_JUMP_PUNCH, OUTCOME_BLOCK, 0.15f, 0.76f};
            actions[1] = {0.90f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {1.48f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.24f};
            actions[3] = {1.74f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_MISS, 0.0f, 0.0f};
            actionCount = 4; exchangeDuration = 2.18f; exchangeNextInitiative = A;
            break;

        // V0.8 style signatures. These are recurring identity phrases built
        // entirely from the trusted shared move vocabulary.
        case 15: // PRESSURE signature: three-beat piston chain
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.09f, 0.22f};
            actions[1] = {0.24f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.64f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[3] = {1.15f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {1.76f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.24f};
            actions[5] = {1.92f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.06f, 0.20f};
            actions[6] = {2.18f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_BLOCK, 0.0f, 0.0f};
            actionCount = 7; exchangeDuration = 3.04f; exchangeNextInitiative = A;
            break;

        case 16: // COUNTER signature: invite commitment, punish twice, leave
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.24f};
            actions[1] = {0.16f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.09f, 0.24f};
            actions[2] = {0.42f, B, ACTION_MOVE, MOVE_HEAVY, OUTCOME_MISS, 0.0f, 0.0f};
            actions[3] = {1.04f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.20f};
            actions[4] = {1.26f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {1.78f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[6] = {2.34f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.24f};
            actionCount = 7; exchangeDuration = 2.72f; exchangeNextInitiative = A;
            break;

        case 17: // KICKER signature: low-line touch into high-line roundhouse
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.05f, 0.24f};
            actions[1] = {0.26f, A, ACTION_MOVE, MOVE_LOW_KICK, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.92f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.06f, 0.22f};
            actions[3] = {1.16f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.08f, 0.22f};
            actions[4] = {1.42f, A, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {2.30f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.24f};
            actionCount = 6; exchangeDuration = 2.62f; exchangeNextInitiative = A;
            break;

        case 18: // HEAVY signature: absorb range, body pressure, committed blow
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.34f};
            actions[1] = {0.22f, B, ACTION_MOVE, MOVE_JAB, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[2] = {0.78f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[3] = {1.42f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.06f, 0.30f};
            actions[4] = {1.78f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {2.72f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.10f, 0.30f};
            actionCount = 6; exchangeDuration = 3.08f; exchangeNextInitiative = A;
            break;

        case 19: // AGILE signature: touch, disappear, punish the chase
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.18f};
            actions[1] = {0.20f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.58f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.11f, 0.19f};
            actions[3] = {0.72f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.22f};
            actions[4] = {0.98f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_MISS, 0.0f, 0.0f};
            actions[5] = {1.46f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.11f, 0.18f};
            actions[6] = {1.66f, A, ACTION_MOVE, MOVE_ROUNDHOUSE, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 7; exchangeDuration = 2.58f; exchangeNextInitiative = A;
            break;

        case 20: // BRAWLER signature: body-body-head pressure
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.09f, 0.23f};
            actions[1] = {0.25f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.80f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[3] = {1.32f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {1.86f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.07f, 0.22f};
            actions[5] = {2.00f, A, ACTION_MOVE, MOVE_HEAVY, OUTCOME_HIT, 0.0f, 0.0f};
            actionCount = 6; exchangeDuration = 2.94f; exchangeNextInitiative = A;
            break;

        // V0.8 specials. The style owner is always actor A; the special move
        // itself samples a style-dependent pose/timing from MOVE_SPECIAL.
        case 21: // PRESSURE / PISTON RUSH
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.09f, 0.20f};
            actions[1] = {0.22f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[2] = {0.64f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_HIT, 0.0f, 0.0f};
            actions[3] = {1.16f, A, ACTION_MOVE, MOVE_SPECIAL, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {1.98f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.11f, 0.28f};
            actionCount = 5; exchangeDuration = 2.38f; exchangeNextInitiative = A;
            break;

        case 22: // COUNTER / INTERCEPT
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.22f};
            actions[1] = {0.08f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.22f};
            actions[2] = {0.34f, B, ACTION_MOVE, MOVE_HEAVY, OUTCOME_MISS, 0.0f, 0.0f};
            actions[3] = {0.96f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.18f};
            actions[4] = {1.16f, A, ACTION_MOVE, MOVE_SPECIAL, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {1.96f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.22f};
            actionCount = 6; exchangeDuration = 2.28f; exchangeNextInitiative = A;
            break;

        case 23: // KICKER / AXIS BREAK
            actions[0] = {0.00f, A, ACTION_MOVE, MOVE_LOW_KICK, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[1] = {0.66f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.07f, 0.21f};
            actions[2] = {0.92f, A, ACTION_MOVE, MOVE_SPECIAL, OUTCOME_HIT, 0.0f, 0.0f};
            actions[3] = {2.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.08f, 0.24f};
            actionCount = 4; exchangeDuration = 2.32f; exchangeNextInitiative = A;
            break;

        case 24: // HEAVY / RAM PRESS
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.11f, 0.36f};
            actions[1] = {0.24f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[2] = {0.90f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.06f, 0.30f};
            actions[3] = {1.28f, A, ACTION_MOVE, MOVE_SPECIAL, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {2.42f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.12f, 0.34f};
            actionCount = 5; exchangeDuration = 2.82f; exchangeNextInitiative = A;
            break;

        case 25: // AGILE / FLASH CUT
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.10f, 0.18f};
            actions[1] = {0.08f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.20f};
            actions[2] = {0.30f, B, ACTION_MOVE, MOVE_CROSS, OUTCOME_MISS, 0.0f, 0.0f};
            actions[3] = {0.76f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.13f, 0.17f};
            actions[4] = {0.96f, A, ACTION_MOVE, MOVE_SPECIAL, OUTCOME_HIT, 0.0f, 0.0f};
            actions[5] = {1.72f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.09f, 0.18f};
            actionCount = 6; exchangeDuration = 2.02f; exchangeNextInitiative = A;
            break;

        case 26: // BRAWLER / SCRAP HOOK
            actions[0] = {0.00f, A, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, 0.10f, 0.22f};
            actions[1] = {0.24f, A, ACTION_MOVE, MOVE_BODY, OUTCOME_HIT, 0.0f, 0.0f};
            actions[2] = {0.80f, A, ACTION_MOVE, MOVE_CROSS, OUTCOME_BLOCK, 0.0f, 0.0f};
            actions[3] = {1.34f, A, ACTION_MOVE, MOVE_SPECIAL, OUTCOME_HIT, 0.0f, 0.0f};
            actions[4] = {2.28f, B, ACTION_STEP, MOVE_IDLE, OUTCOME_HIT, -0.10f, 0.28f};
            actionCount = 5; exchangeDuration = 2.62f; exchangeNextInitiative = A;
            break;

        default:
            actions[0] = {0.00f, A, ACTION_MOVE, MOVE_JAB, OUTCOME_BLOCK, 0.0f, 0.0f};
            actionCount = 1; exchangeDuration = 0.72f; exchangeNextInitiative = B;
            break;
    }

    exchangeActive = true;
}

void RobotFightGenerator::finishExchange() {
    exchangeActive = false;
    initiativeOwner = exchangeNextInitiative;

    fighters[initiativeOwner].momentum = std::min(1.0f, fighters[initiativeOwner].momentum + 0.055f);
    fighters[1 - initiativeOwner].momentum *= 0.96f;

    const float calm = 0.82f - kCombat * 0.44f;
    const float audioPush = energySmooth * 0.22f
                          + triggerBoost * kMusicLock * 0.30f
                          + triggerDensity * kMusicLock * 0.12f;
    neutralTimer = std::max(0.14f, calm - audioPush + rand01() * 0.26f);
}

int RobotFightGenerator::chooseStyledExchange(int leadIndex) {
    const FighterStyle style = fighters[leadIndex].style;
    float weights[21] = {0};

    const bool high = (kCombat > 0.65f || triggerDensity > 0.52f || triggerBoost > 0.60f);
    const bool mid = high || (kCombat > 0.36f || energySmooth > 0.38f);
    const bool airReady = (airCooldown <= 0.0f);

    for (int id = 0; id < 15; ++id) {
        if (id == 3) continue; // clash is handled separately

        bool allowed = false;
        if (high) {
            allowed = true;
        } else if (mid) {
            allowed = (id != 6 && id != 10);
        } else {
            allowed = (id == 0 || id == 1 || id == 2 || id == 8 || id == 9 || id == 11);
        }
        if (id >= 12 && !airReady) allowed = false;
        if (!allowed) continue;

        float w = styleExchangeWeight(style, id);

        // Musical accents can invite an aerial punctuation, but they never
        // directly trigger a jump and the shared cooldown still applies.
        if (triggerBoost > 0.70f && kMusicLock > 0.52f) {
            if (id == 2 || id == 6 || id == 10 || id == 11) w *= 1.60f;
            if (id == 12 || id == 14) w *= 1.28f;
            if (id == 0 || id == 5) w *= 0.72f;
        }
        if (triggerDensity > 0.58f && kCombat > 0.50f) {
            if (id == 5 || id == 6 || id == 8) w *= 1.34f;
        }
        // Keep aerial phrases as punctuation even for agile styles.
        if (id >= 12) w *= 0.78f;
        weights[id] = w;
    }

    // The fighter's own signature phrase joins the lottery only after its
    // personal cooldown clears. Unlike specials, signatures do not require a
    // rare event window: they are how personality becomes learnable.
    const int sigId = signatureExchangeId(style);
    if (mid && signatureCooldown[leadIndex] <= 0.0f) {
        float sigWeight = 1.10f + fighters[leadIndex].momentum * 0.70f;
        if (high) sigWeight *= 1.18f;
        if (triggerBoost > 0.68f && kMusicLock > 0.42f) sigWeight *= 1.24f;
        switch (style) {
            case STYLE_PRESSURE: sigWeight *= 1.12f; break;
            case STYLE_COUNTER: sigWeight *= 1.00f; break;
            case STYLE_KICKER: sigWeight *= 1.08f; break;
            case STYLE_HEAVY: sigWeight *= 0.92f; break;
            case STYLE_AGILE: sigWeight *= 1.10f; break;
            case STYLE_BRAWLER: sigWeight *= 1.06f; break;
            default: break;
        }
        weights[sigId] = sigWeight;
    }

    float total = 0.0f;
    for (int id = 0; id < 21; ++id) total += weights[id];
    if (total <= 0.0001f) return 0;

    float r = rand01() * total;
    for (int id = 0; id < 21; ++id) {
        r -= weights[id];
        if (r <= 0.0f) return id;
    }
    return 0;
}

void RobotFightGenerator::startNextExchange() {
    // Initiative ownership now interacts with personality.  Pressure/heavy
    // fighters tend to keep the conversational turn; agile/counter fighters
    // are more willing to yield space and steal it back later.
    int lead = initiativeOwner;
    const StyleProfile ownerStyle = styleProfile(fighters[initiativeOwner].style);
    const float calmTurnover = (1.0f - kCombat) * 0.06f;
    const float turnoverChance = (1.0f - ownerStyle.initiativeHold) * 0.42f + calmTurnover;
    if (rand01() < turnoverChance) lead = 1 - lead;

    // Clash remains a rare authored event with its own cooldown.  Styles do not
    // get to force it; that keeps simultaneous heavies exceptional.
    const bool clashEligible = clashCooldown <= 0.0f
                            && kCombat > 0.52f
                            && energySmooth > 0.28f
                            && std::fabs(fighters[1].x - fighters[0].x) < 1.08f;
    const bool accentWindow = triggerBoost > 0.72f && kMusicLock > 0.45f;
    const float clashChance = accentWindow ? 0.070f : 0.010f;

    int id = -1;
    if (clashEligible && rand01() < clashChance) {
        id = 3;
        clashCooldown = 22.0f + rand01() * 26.0f;
    }

    // Specials are ready-state opportunities, not normal lottery entries. The
    // fighter needs momentum, its own cooldown, and enough combat/audio energy.
    // A fresh musical accent raises the chance, but cannot bypass readiness.
    if (id < 0) {
        const float leadMomentum = fighters[lead].momentum;
        const bool specialEligible = specialCooldown[lead] <= 0.0f
                                  && leadMomentum > 0.56f
                                  && kCombat > 0.46f
                                  && energySmooth > 0.24f;
        if (specialEligible) {
            float specialChance = 0.020f
                                + kCombat * 0.030f
                                + leadMomentum * 0.025f;
            if (accentWindow) specialChance *= 2.35f;
            if (energySmooth > 0.68f) specialChance *= 1.22f;
            specialChance = clampRange(specialChance, 0.025f, 0.185f);
            if (rand01() < specialChance) {
                id = specialExchangeId(fighters[lead].style);
                specialCooldown[lead] = 20.0f + rand01() * 18.0f;
                fighters[lead].momentum = std::max(0.38f, fighters[lead].momentum - 0.16f);
            }
        }
    }

    if (id < 0) id = chooseStyledExchange(lead);
    if (id >= 12 && id <= 14) {
        airCooldown = 5.5f + rand01() * 6.5f;
    }
    if (id >= 15 && id <= 20) {
        signatureCooldown[lead] = 7.5f + rand01() * 6.5f;
    }
    loadExchange(id, lead == 1);
}

void RobotFightGenerator::updateDirector(float dt, bool trigFired) {
    if (roundOver) return;

    const float tempoScale = 0.82f
                           + kCombat * 0.54f
                           + energySmooth * 0.10f
                           + triggerDensity * kMusicLock * 0.10f;

    if (exchangeActive) {
        exchangeTime += dt * tempoScale;

        while (nextAction < actionCount && exchangeTime >= actions[nextAction].at) {
            const ExchangeAction& a = actions[nextAction];

            // At high Music Lock, attacks are allowed to wait briefly for a
            // fresh trig. The wait is capped, so sparse material never freezes
            // the choreography. Steps remain unquantized and keep the bodies alive.
            if (a.type == ACTION_MOVE && kMusicLock > 0.62f) {
                const float maxWait = 0.035f + kMusicLock * 0.105f;
                const float overdue = exchangeTime - a.at;
                const bool recentPulse = timeSinceTrigger < (0.055f + kMusicLock * 0.055f);
                if (!recentPulse && overdue < maxWait) break;
            }

            Fighter& actor = fighters[a.actor];
            if (a.type == ACTION_STEP) {
                startStep(actor, a.amount, a.duration);
            } else if (a.type == ACTION_AIR) {
                startAir(actor, a.move, a.outcome, a.amount, a.duration);
            } else {
                startMove(actor, a.move, a.outcome);
                if (a.move == MOVE_SPECIAL) {
                    specialBanner = 0.92f;
                    specialBannerActor = a.actor;
                }
            }
            ++nextAction;
        }

        if (exchangeTime >= exchangeDuration && nextAction >= actionCount) {
            finishExchange();
        }
        return;
    }

    neutralTimer -= dt * (0.92f + energySmooth * 0.42f + triggerDensity * 0.10f);

    if (trigFired && kMusicLock > 0.18f) {
        neutralTimer -= 0.10f + kMusicLock * 0.30f;
    }

    if (neutralTimer <= 0.0f &&
        fighters[0].move == MOVE_IDLE && fighters[1].move == MOVE_IDLE &&
        !anyStepActive() && !anyAirborne()) {
        startNextExchange();
    }
}

void RobotFightGenerator::updateCamera(float dt) {
    const float targetCenter = (fighters[0].x + fighters[1].x) * 0.5f;
    const float centerFollow = 1.0f - std::exp(-dt * (2.0f + kCinema * 4.0f));
    cameraCenterX += (targetCenter - cameraCenterX) * centerFollow;

    const float sep = std::fabs(fighters[1].x - fighters[0].x);
    float targetZoom = 1.07f - (sep - 0.66f) * 0.36f;
    targetZoom = std::max(0.88f, std::min(1.08f, targetZoom));
    targetZoom = lerpf(1.0f, targetZoom, 0.28f + kCinema * 0.72f);

    const float zoomFollow = 1.0f - std::exp(-dt * (2.4f + kCinema * 2.8f));
    cameraFramingZoom += (targetZoom - cameraFramingZoom) * zoomFollow;
}


void RobotFightGenerator::step(float dt, float energy, bool trigFired,
                               float k1, float k2, float k3, float k4, float k5) {
    if (!ready) reset();

    // Drive simulation from real elapsed time rather than "one fixed tick per
    // rendered frame". If rendering slows, combat still proceeds in real time.
    const float now = ofGetElapsedTimef();
    float wallDt = (lastWallTime >= 0.0f) ? (now - lastWallTime) : dt;
    lastWallTime = now;
    if (!(wallDt > 0.0f) || wallDt > 0.250f) {
        wallDt = (dt > 0.0f) ? dt : (1.0f / 30.0f);
    }
    dt = clampRange(wallDt, 0.001f, 0.100f);
    kCombat = clamp01(k1);
    kMusicLock = clamp01(k2);
    kCinema = clamp01(k3);
    kPalette = clamp01(k4);
    kArena = clamp01(k5);

    energy = clamp01(energy);
    const float energyFollow = 1.0f - std::exp(-dt * 5.0f);
    energySmooth += (energy - energySmooth) * energyFollow;

    timeSinceTrigger += dt;
    if (trigFired) {
        triggerBoost = 1.0f;
        triggerDensity = std::min(1.0f, triggerDensity + 0.22f + kMusicLock * 0.10f);
        timeSinceTrigger = 0.0f;
    }
    triggerBoost *= std::exp(-dt * 2.8f);
    triggerDensity *= std::exp(-dt * 0.85f);
    clashCooldown = std::max(0.0f, clashCooldown - dt);
    airCooldown = std::max(0.0f, airCooldown - dt);
    for (int i = 0; i < 2; ++i) {
        signatureCooldown[i] = std::max(0.0f, signatureCooldown[i] - dt);
        specialCooldown[i] = std::max(0.0f, specialCooldown[i] - dt);
    }
    specialBanner = std::max(0.0f, specialBanner - dt);

    cameraShakeX *= std::exp(-dt * 18.0f);
    cameraShakeY *= std::exp(-dt * 18.0f);
    cameraZoomImpulse *= std::exp(-dt * 7.5f);
    screenFlash *= std::exp(-dt * 10.0f);

    updateSparks(dt);

    // Hit-stop freezes the complete body state: pose time, root impulse, feet,
    // and director timing. That makes the contact pose read as one event.
    if (hitStopRemaining > 0.0f) {
        hitStopRemaining = std::max(0.0f, hitStopRemaining - dt);
        updateCamera(dt);
        return;
    }

    if (roundOver) {
        roundTimer -= dt;
        updateFighter(fighters[0], dt);
        updateFighter(fighters[1], dt);
        enforceSpacingBounds();
        updateCamera(dt);
        if (roundTimer <= 0.0f) {
            ++roundNumber;
            resetRound();
        }
        return;
    }

    updateDirector(dt, trigFired);
    updateFighter(fighters[0], dt);
    updateFighter(fighters[1], dt);

    if (!exchangeActive) maintainNeutralSpacing(dt);
    enforceSpacingBounds();
    updateCamera(dt);
}


RobotFightGenerator::ChassisDims RobotFightGenerator::chassisDims(const Fighter& f) const {
    ChassisDims d;
    d.shoulderHalf = 0.132f;
    d.hipHalf = 0.066f;
    d.torsoWidth = 0.138f;
    d.upperArmLen = 0.250f;
    d.forearmLen = 0.226f;
    d.thighLen = 0.310f;
    d.shinLen = 0.316f;
    d.limbScale = 1.0f;
    d.headScale = 1.0f;

    if (f.chassis == 1) {
        d.shoulderHalf = 0.154f;
        d.hipHalf = 0.078f;
        d.torsoWidth = 0.158f;
        d.upperArmLen = 0.242f;
        d.forearmLen = 0.218f;
        d.thighLen = 0.304f;
        d.shinLen = 0.306f;
        d.limbScale = 1.16f;
        d.headScale = 1.08f;
    } else if (f.chassis == 2) {
        d.shoulderHalf = 0.116f;
        d.hipHalf = 0.058f;
        d.torsoWidth = 0.120f;
        d.upperArmLen = 0.265f;
        d.forearmLen = 0.240f;
        d.thighLen = 0.322f;
        d.shinLen = 0.330f;
        d.limbScale = 0.88f;
        d.headScale = 0.92f;
    }
    return d;
}

ofVec2f RobotFightGenerator::bodyOffset(
        float forward, float down, float angle, int facing) const {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float fx = c * static_cast<float>(facing);
    const float fy = s;
    const float dx = -s * static_cast<float>(facing);
    const float dy = c;
    return ofVec2f(fx * forward + dx * down,
                   fy * forward + dy * down);
}

ofVec2f RobotFightGenerator::hierarchicalLimb(
        float localDegrees, float length, float parentAngle, int facing) const {
    const float a = parentAngle + degToRad(localDegrees);
    return ofVec2f(std::cos(a) * length * static_cast<float>(facing),
                   std::sin(a) * length);
}

void RobotFightGenerator::solveTwoBone(
        const ofVec2f& hip, const ofVec2f& desiredFoot,
        float upperLen, float lowerLen, int facing,
        ofVec2f& outKnee, ofVec2f& outFoot) const {
    ofVec2f delta = desiredFoot - hip;
    float d = delta.length();
    if (d < 0.0001f) {
        delta.set(0.0f, 1.0f);
        d = 1.0f;
    }
    const ofVec2f dir = delta / d;

    const float minD = std::fabs(upperLen - lowerLen) + 0.003f;
    const float maxD = upperLen + lowerLen - 0.003f;
    const float solvedD = clampRange(d, minD, maxD);
    outFoot = hip + dir * solvedD;

    const float a = (upperLen * upperLen - lowerLen * lowerLen + solvedD * solvedD)
                  / (2.0f * solvedD);
    const float h2 = std::max(0.0f, upperLen * upperLen - a * a);
    const float h = std::sqrt(h2);
    const ofVec2f base = hip + dir * a;
    const ofVec2f perp(-dir.y, dir.x);

    // With screen Y pointing down, -facing bends the knee toward the opponent.
    const float pole = -static_cast<float>(facing);
    outKnee = base + perp * (h * pole);
}

RobotFightGenerator::RigPoints RobotFightGenerator::solveRig(
        const Fighter& f, const Pose& inputPose) const {
    const Pose p = sanitizePose(inputPose);
    const ChassisDims d = chassisDims(f);
    RigPoints r;

    const float rootX = f.x + f.impactX
                      + p.rootForward * static_cast<float>(f.facing);
    const float pelvisY = -0.56f + p.rootDown + f.impactY + f.z * 0.025f;
    r.pelvis.set(rootX, pelvisY);
    r.pelvisAngle = p.pelvisTilt;
    r.torsoAngle = p.torsoLean;
    r.headAngle = r.torsoAngle + p.headLean;

    const float laneZ = f.z * 0.85f;

    // Pose Z is authored in FIGHTER-LOCAL stance space, not camera/world space.
    // The opponent faces the opposite direction, so its entire stance twist must
    // mirror in depth as well as in X.  V0.3.1 omitted this transform, which made
    // both fighters present their lead/front shoulder toward the camera.
    // laneZ remains world-space because it represents actual arena lane position.
    const float stanceZSign = static_cast<float>(f.facing);
    const auto stanceZ = [&](float localZ) {
        return laneZ + localZ * stanceZSign;
    };

    r.frontShoulderZ = stanceZ(p.frontShoulderZ);
    r.frontElbowZ = stanceZ(p.frontElbowZ);
    r.frontHandZ = stanceZ(p.frontHandZ);
    r.rearShoulderZ = stanceZ(p.rearShoulderZ);
    r.rearElbowZ = stanceZ(p.rearElbowZ);
    r.rearHandZ = stanceZ(p.rearHandZ);
    r.frontHipZ = stanceZ(p.frontHipZ);
    r.frontKneeZ = stanceZ(p.frontKneeZ);
    r.frontFootZ = stanceZ(p.frontFootZ);
    r.rearHipZ = stanceZ(p.rearHipZ);
    r.rearKneeZ = stanceZ(p.rearKneeZ);
    r.rearFootZ = stanceZ(p.rearFootZ);

    const float abdomenLen = 0.145f;
    const float upperTorsoLen = 0.220f;
    const float neckLen = 0.085f;
    const float headOffsetLen = 0.118f;

    r.waist = r.pelvis + bodyOffset(0.0f, -abdomenLen, r.pelvisAngle * 0.72f, f.facing);
    r.chest = r.waist + bodyOffset(0.0f, -upperTorsoLen, r.torsoAngle, f.facing);
    r.neckBase = r.chest + bodyOffset(0.0f, -neckLen, r.headAngle, f.facing);
    r.head = r.neckBase + bodyOffset(0.0f, -headOffsetLen, r.headAngle, f.facing);

    r.rearShoulder = r.chest + bodyOffset(-d.shoulderHalf, 0.010f, r.torsoAngle, f.facing);
    r.frontShoulder = r.chest + bodyOffset(d.shoulderHalf, -0.008f, r.torsoAngle, f.facing);

    const float rearUpperAngle = r.torsoAngle + degToRad(p.rearShoulder);
    const float frontUpperAngle = r.torsoAngle + degToRad(p.frontShoulder);
    r.rearElbow = r.rearShoulder
        + hierarchicalLimb(p.rearShoulder, d.upperArmLen * 0.98f, r.torsoAngle, f.facing);
    r.rearHand = r.rearElbow
        + hierarchicalLimb(p.rearElbow, d.forearmLen * 0.98f, rearUpperAngle, f.facing);
    r.frontElbow = r.frontShoulder
        + hierarchicalLimb(p.frontShoulder, d.upperArmLen * 1.02f, r.torsoAngle, f.facing);
    r.frontHand = r.frontElbow
        + hierarchicalLimb(p.frontElbow, d.forearmLen * 1.02f, frontUpperAngle, f.facing);

    r.rearHip = r.pelvis + bodyOffset(-d.hipHalf, 0.014f, r.pelvisAngle, f.facing);
    r.frontHip = r.pelvis + bodyOffset(d.hipHalf, 0.006f, r.pelvisAngle, f.facing);

    const ofVec2f frontAnchor(f.frontFootX, f.frontFootY + f.z * 0.018f);
    const ofVec2f rearAnchor(f.rearFootX, f.rearFootY + f.z * 0.018f);
    const ofVec2f frontFree = r.pelvis
        + ofVec2f(static_cast<float>(f.facing) * p.frontFootForward, p.frontFootDown);
    const ofVec2f rearFree = r.pelvis
        + ofVec2f(static_cast<float>(f.facing) * p.rearFootForward, p.rearFootDown);

    const ofVec2f frontTarget = frontFree * (1.0f - p.frontPlant) + frontAnchor * p.frontPlant;
    const ofVec2f rearTarget = rearFree * (1.0f - p.rearPlant) + rearAnchor * p.rearPlant;

    solveTwoBone(r.frontHip, frontTarget,
                 d.thighLen * 1.02f, d.shinLen * 1.02f, f.facing,
                 r.frontKnee, r.frontFoot);
    solveTwoBone(r.rearHip, rearTarget,
                 d.thighLen * 0.98f, d.shinLen * 0.98f, f.facing,
                 r.rearKnee, r.rearFoot);

    return r;
}

void RobotFightGenerator::drawRobotSegment(
        const ofVec2f& a, const ofVec2f& b, float thickness,
        const ofColor& c, float endScale) const {
    ofVec2f d = b - a;
    float len = d.length();
    if (len < 0.0001f) return;
    d /= len;
    ofVec2f n(-d.y, d.x);

    const float r0 = thickness * 0.5f;
    const float r1 = r0 * endScale;

    ofSetColor(c);
    ofDrawTriangle(a + n * r0, a - n * r0, b - n * r1);
    ofDrawTriangle(a + n * r0, b - n * r1, b + n * r1);

    ofColor hi = c;
    hi.r = static_cast<unsigned char>(std::min(255, static_cast<int>(hi.r) + 28));
    hi.g = static_cast<unsigned char>(std::min(255, static_cast<int>(hi.g) + 28));
    hi.b = static_cast<unsigned char>(std::min(255, static_cast<int>(hi.b) + 28));
    ofSetColor(hi);
    ofDrawTriangle(a + n * r0, b + n * r1, b + n * (r1 * 0.20f));
}

void RobotFightGenerator::drawRobotJoint(
        const ofVec2f& p, float radius, const ofColor& c) const {
    ofSetColor(c);
    ofDrawCircle(p, radius);
}



void RobotFightGenerator::drawCombatScene2D() const {
    // V0.4.2: shared *2D* scene queue with finer crossing subdivision.
    //
    // Both fighters submit coarse drawable parts into one fixed-size array and
    // the whole scene is sorted by pseudo-Z once. This fixes cross-fighter
    // occlusion without a full-resolution hardware depth pass, dynamic mesh
    // construction, per-vertex colors, or heap allocation.
    enum PartType {
        PART_SEGMENT = 0,
        PART_JOINT,
        PART_ELLIPSE,
        PART_QUAD,
        PART_TRIANGLE
    };
    struct DrawPart {
        PartType type;
        float depth;
        ofVec2f p0, p1, p2, p3;
        float a;
        float b;
        float endScale;
        ofColor color;
    };

    std::array<DrawPart, 160> parts;
    int partCount = 0;

    const auto pushSimpleSegment = [&](const ofVec2f& a, const ofVec2f& b,
                                       float za, float zb, float thickness,
                                       const ofColor& color, float endScale) {
        if (partCount >= static_cast<int>(parts.size())) return;
        DrawPart& q = parts[partCount++];
        q.type = PART_SEGMENT;
        q.depth = (za + zb) * 0.5f;
        q.p0 = a; q.p1 = b;
        q.a = thickness; q.b = 0.0f; q.endScale = endScale; q.color = color;
    };

    const auto pushSegment = [&](const ofVec2f& a, const ofVec2f& b,
                                 float za, float zb, float torsoZ,
                                 float thickness, const ofColor& color,
                                 float endScale) {
        // Painter rendering can only sort whole pieces. V0.4.1 used two
        // pieces for a crossing limb; a fast strike could still put too much
        // of a forearm/shin on the wrong side for a frame because each half
        // was ordered by its average Z. Use three coarse pieces whenever Z
        // travel matters, with one boundary pinned to the torso plane when
        // possible. This stays tiny on the CM3 but greatly reduces flash-through.
        const float dz = zb - za;
        const bool crossesTorso =
            ((za - torsoZ) * (zb - torsoZ) < -0.000001f) &&
            std::fabs(dz) > 0.00001f;

        if ((crossesTorso || std::fabs(dz) > 0.038f) &&
            partCount + 3 <= static_cast<int>(parts.size())) {
            float t1 = 1.0f / 3.0f;
            float t2 = 2.0f / 3.0f;

            if (crossesTorso) {
                const float tc = clampRange((torsoZ - za) / dz, 0.14f, 0.86f);
                if (tc <= 0.5f) {
                    t1 = tc;
                    t2 = tc + (1.0f - tc) * 0.50f;
                } else {
                    t1 = tc * 0.50f;
                    t2 = tc;
                }
            }

            const ofVec2f m1 = a + (b - a) * t1;
            const ofVec2f m2 = a + (b - a) * t2;
            const float z1 = lerpf(za, zb, t1);
            const float z2 = lerpf(za, zb, t2);

            pushSimpleSegment(a,  m1, za, z1, thickness, color, endScale);
            pushSimpleSegment(m1, m2, z1, z2, thickness, color, endScale);
            pushSimpleSegment(m2, b,  z2, zb, thickness, color, endScale);
            return;
        }
        pushSimpleSegment(a, b, za, zb, thickness, color, endScale);
    };

    const auto pushJoint = [&](const ofVec2f& p0, float z, float radius,
                               const ofColor& color) {
        if (partCount >= static_cast<int>(parts.size())) return;
        DrawPart& q = parts[partCount++];
        q.type = PART_JOINT; q.depth = z; q.p0 = p0;
        q.a = radius; q.b = 0.0f; q.endScale = 1.0f; q.color = color;
    };

    const auto pushEllipse = [&](const ofVec2f& p0, float z, float w, float h,
                                 const ofColor& color) {
        if (partCount >= static_cast<int>(parts.size())) return;
        DrawPart& q = parts[partCount++];
        q.type = PART_ELLIPSE; q.depth = z; q.p0 = p0;
        q.a = w; q.b = h; q.endScale = 1.0f; q.color = color;
    };

    const auto pushQuad = [&](const ofVec2f& a, const ofVec2f& b,
                              const ofVec2f& c, const ofVec2f& d,
                              float z, const ofColor& color) {
        if (partCount >= static_cast<int>(parts.size())) return;
        DrawPart& q = parts[partCount++];
        q.type = PART_QUAD; q.depth = z;
        q.p0 = a; q.p1 = b; q.p2 = c; q.p3 = d;
        q.a = q.b = 0.0f; q.endScale = 1.0f; q.color = color;
    };

    const auto pushTriangle = [&](const ofVec2f& a, const ofVec2f& b,
                                  const ofVec2f& c, float z,
                                  const ofColor& color) {
        if (partCount >= static_cast<int>(parts.size())) return;
        DrawPart& q = parts[partCount++];
        q.type = PART_TRIANGLE; q.depth = z;
        q.p0 = a; q.p1 = b; q.p2 = c;
        q.a = q.b = 0.0f; q.endScale = 1.0f; q.color = color;
    };

    static const ofColor banks[6][4] = {
        {ofColor(34, 38, 44),  ofColor(112, 224, 238), ofColor(220, 246, 250), ofColor(15, 18, 22)},
        {ofColor(42, 30, 34),  ofColor(246, 92, 102),  ofColor(255, 208, 156), ofColor(20, 12, 16)},
        {ofColor(32, 38, 29),  ofColor(172, 232, 92),  ofColor(232, 255, 174), ofColor(14, 18, 12)},
        {ofColor(38, 31, 49),  ofColor(195, 113, 244), ofColor(243, 209, 255), ofColor(15, 10, 21)},
        {ofColor(44, 39, 28),  ofColor(246, 192, 72),  ofColor(255, 238, 177), ofColor(18, 15, 10)},
        {ofColor(30, 34, 42),  ofColor(104, 148, 255), ofColor(209, 224, 255), ofColor(11, 14, 22)}
    };

    const auto appendFighter = [&](const Fighter& f, int index) {
        const Pose p = samplePose(f);
        const RigPoints r = solveRig(f, p);
        const ChassisDims dims = chassisDims(f);
        const float coreZ = f.z * 0.85f;

        const int palette = std::min(5, static_cast<int>(kPalette * 5.999f));
        ofColor dark = banks[palette][0];
        ofColor accent = banks[palette][1];
        ofColor light = banks[palette][2];
        ofColor deepest = banks[palette][3];

        if (index == 1) {
            std::swap(accent.r, accent.b);
            light.r = static_cast<unsigned char>(
                std::min(255, static_cast<int>(light.r) + 12));
            dark.b = static_cast<unsigned char>(
                std::min(255, static_cast<int>(dark.b) + 16));
        }

        // Flash is the only per-frame palette modulation. Limb identity itself
        // stays stable through a strike, avoiding the V0.4 mid-swing hue flip.
        if (f.flash > 0.0f) {
            const float q = clamp01(f.flash);
            dark = dark.getLerped(ofColor::white, q * 0.30f);
            accent = accent.getLerped(ofColor::white, q * 0.72f);
            light = light.getLerped(ofColor::white, q * 0.88f);
        }

        // Stable near-side coloration is based on the fighter's mirrored stance,
        // not instantaneous hand Z. Left fighter's front side is camera-near;
        // right fighter's rear side is camera-near.
        const bool nearIsFront = (f.facing > 0);

        // V0.4.1 pushed the far side all the way to `deepest`, which read as
        // almost black on the projector. Preserve the near/far cue but keep
        // the recessed limbs readable. These tones are fixed by stance side,
        // so they still cannot flip hue during a strike.
        const ofColor farLimb = dark.getLerped(accent, 0.28f);
        const ofColor farExt  = dark.getLerped(light, 0.20f);
        const ofColor frontLimb = nearIsFront ? accent : farLimb;
        const ofColor rearLimb  = nearIsFront ? farLimb : accent;
        const ofColor frontExt  = nearIsFront ? light : farExt;
        const ofColor rearExt   = nearIsFront ? farExt : light;

        // Ground shadow is always behind the sortable body geometry.
        ofSetColor(0, 0, 0, 92);
        ofDrawEllipse(r.pelvis.x, 0.025f + f.z * 0.018f,
                      0.43f * dims.limbScale, 0.075f);

        pushSegment(r.rearHip, r.rearKnee, r.rearHipZ, r.rearKneeZ, coreZ,
                    0.105f * dims.limbScale, rearLimb, 0.78f);
        pushSegment(r.rearKnee, r.rearFoot, r.rearKneeZ, r.rearFootZ, coreZ,
                    0.090f * dims.limbScale, rearLimb, 0.78f);
        pushSegment(r.rearShoulder, r.rearElbow,
                    r.rearShoulderZ, r.rearElbowZ, coreZ,
                    0.100f * dims.limbScale, rearLimb, 0.78f);
        pushSegment(r.rearElbow, r.rearHand, r.rearElbowZ, r.rearHandZ, coreZ,
                    0.082f * dims.limbScale, rearLimb, 0.78f);
        pushJoint(r.rearKnee, r.rearKneeZ, 0.049f * dims.limbScale, rearLimb);
        pushJoint(r.rearElbow, r.rearElbowZ, 0.043f * dims.limbScale, rearLimb);
        pushJoint(r.rearShoulder, r.rearShoulderZ, 0.047f * dims.limbScale, rearLimb);
        pushJoint(r.rearHip, r.rearHipZ, 0.046f * dims.limbScale, rearLimb);

        const ofVec2f p0 = r.pelvis + bodyOffset(-0.105f, -0.025f, r.pelvisAngle, f.facing);
        const ofVec2f p1 = r.pelvis + bodyOffset( 0.105f, -0.025f, r.pelvisAngle, f.facing);
        const ofVec2f p2 = r.pelvis + bodyOffset( 0.082f,  0.080f, r.pelvisAngle, f.facing);
        const ofVec2f p3 = r.pelvis + bodyOffset(-0.082f,  0.080f, r.pelvisAngle, f.facing);
        pushQuad(p0, p1, p2, p3, coreZ - 0.010f, deepest);

        pushSegment(r.pelvis + bodyOffset(0.0f, -0.020f, r.pelvisAngle, f.facing),
                    r.waist, coreZ - 0.010f, coreZ, coreZ,
                    0.145f * dims.limbScale, dark, 0.90f);
        pushSegment(r.waist, r.chest, coreZ, coreZ, coreZ,
                    0.170f * dims.limbScale, dark, 1.04f);

        const ofVec2f cL = r.chest + bodyOffset(-dims.torsoWidth, 0.050f, r.torsoAngle, f.facing);
        const ofVec2f cR = r.chest + bodyOffset( dims.torsoWidth, 0.050f, r.torsoAngle, f.facing);
        const ofVec2f wR = r.waist + bodyOffset( 0.095f, 0.035f, r.torsoAngle, f.facing);
        const ofVec2f wL = r.waist + bodyOffset(-0.095f, 0.035f, r.torsoAngle, f.facing);
        pushQuad(cL, cR, wR, wL, coreZ, dark);
        pushTriangle(cL, cR,
                     r.waist + bodyOffset(0.075f, 0.025f, r.torsoAngle, f.facing),
                     coreZ + 0.008f, accent);

        pushJoint(r.rearShoulder, r.rearShoulderZ,
                  0.050f * dims.limbScale, rearLimb);
        pushJoint(r.frontShoulder, r.frontShoulderZ,
                  0.054f * dims.limbScale, frontLimb);
        pushSegment(r.chest + bodyOffset(0.0f, -0.020f, r.torsoAngle, f.facing),
                    r.neckBase, coreZ + 0.010f, coreZ + 0.020f, coreZ,
                    0.072f * dims.limbScale, deepest, 0.82f);

        const float hx = 0.085f * dims.headScale;
        const float hy = 0.062f * dims.headScale;
        const ofVec2f h0 = r.head + bodyOffset(-hx, -hy * 0.88f, r.headAngle, f.facing);
        const ofVec2f h1 = r.head + bodyOffset( hx, -hy * 0.56f, r.headAngle, f.facing);
        const ofVec2f h2 = r.head + bodyOffset( hx * 0.88f, hy, r.headAngle, f.facing);
        const ofVec2f h3 = r.head + bodyOffset(-hx * 0.82f, hy * 1.12f, r.headAngle, f.facing);
        pushQuad(h0, h1, h2, h3, coreZ + 0.025f, light);

        const ofVec2f visorA = r.head + bodyOffset(0.010f, -0.012f, r.headAngle, f.facing);
        const ofVec2f visorB = r.head + bodyOffset(0.074f,  0.012f, r.headAngle, f.facing);
        pushSegment(visorA, visorB, coreZ + 0.038f, coreZ + 0.038f, coreZ,
                    0.024f * dims.headScale, deepest, 0.75f);

        pushSegment(r.frontHip, r.frontKnee, r.frontHipZ, r.frontKneeZ, coreZ,
                    0.115f * dims.limbScale, frontLimb, 0.78f);
        pushSegment(r.frontKnee, r.frontFoot, r.frontKneeZ, r.frontFootZ, coreZ,
                    0.095f * dims.limbScale, frontLimb, 0.78f);
        pushSegment(r.frontShoulder, r.frontElbow,
                    r.frontShoulderZ, r.frontElbowZ, coreZ,
                    0.108f * dims.limbScale, frontLimb, 0.78f);
        pushSegment(r.frontElbow, r.frontHand, r.frontElbowZ, r.frontHandZ, coreZ,
                    0.090f * dims.limbScale, frontLimb, 0.78f);
        pushJoint(r.frontHip, r.frontHipZ, 0.050f * dims.limbScale, frontLimb);
        pushJoint(r.frontKnee, r.frontKneeZ, 0.052f * dims.limbScale, frontLimb);
        pushJoint(r.frontElbow, r.frontElbowZ, 0.046f * dims.limbScale, frontLimb);

        pushEllipse(r.frontHand, r.frontHandZ,
                    0.105f * dims.limbScale, 0.075f * dims.limbScale, frontExt);
        pushEllipse(r.rearHand, r.rearHandZ,
                    0.092f * dims.limbScale, 0.067f * dims.limbScale, rearExt);
        pushSegment(r.frontFoot + ofVec2f(-0.065f * dims.limbScale, 0.0f),
                    r.frontFoot + ofVec2f( 0.065f * dims.limbScale, 0.0f),
                    r.frontFootZ, r.frontFootZ, coreZ,
                    0.050f * dims.limbScale, frontExt, 1.0f);
        pushSegment(r.rearFoot + ofVec2f(-0.055f * dims.limbScale, 0.0f),
                    r.rearFoot + ofVec2f( 0.055f * dims.limbScale, 0.0f),
                    r.rearFootZ, r.rearFootZ, coreZ,
                    0.045f * dims.limbScale, rearExt, 1.0f);
    };

    appendFighter(fighters[0], 0);
    appendFighter(fighters[1], 1);

    // Quantize tiny depth differences before sorting. At painter-scale,
    // sub-millimetric Z crossings are visually ambiguous anyway; letting them
    // reverse order every frame produces distracting one-frame flashes. A
    // stable sort inside a small depth bin keeps the image coherent, while
    // meaningful crossings still change order normally.
    const float sortBin = 0.006f;
    std::stable_sort(parts.begin(), parts.begin() + partCount,
              [sortBin](const DrawPart& a, const DrawPart& b) {
                  const int da = static_cast<int>(std::floor(a.depth / sortBin + 0.5f));
                  const int db = static_cast<int>(std::floor(b.depth / sortBin + 0.5f));
                  return da < db;
              });

    // Draw depth only affects ordering and a small size cue. Hue is stable.
    for (int i = 0; i < partCount; ++i) {
        const DrawPart& q = parts[i];
        const float depthScale =
            clampRange(1.0f + q.depth * 0.16f, 0.93f, 1.07f);

        switch (q.type) {
            case PART_SEGMENT:
                drawRobotSegment(q.p0, q.p1, q.a * depthScale, q.color, q.endScale);
                break;
            case PART_JOINT:
                drawRobotJoint(q.p0, q.a * depthScale, q.color);
                break;
            case PART_ELLIPSE:
                ofSetColor(q.color);
                ofDrawEllipse(q.p0.x, q.p0.y,
                              q.a * depthScale, q.b * depthScale);
                break;
            case PART_QUAD:
                ofSetColor(q.color);
                ofDrawTriangle(q.p0, q.p1, q.p2);
                ofDrawTriangle(q.p0, q.p2, q.p3);
                break;
            case PART_TRIANGLE:
                ofSetColor(q.color);
                ofDrawTriangle(q.p0, q.p1, q.p2);
                break;
        }
    }
}


void RobotFightGenerator::drawArena(float width, float height) const {
    const float arenaMix = kArena;
    ofColor bgTop(6, 8, 12);
    ofColor bgBottom(15, 18, 24);

    ofSetColor(bgTop);
    ofDrawRectangle(0, 0, width, height);

    for (int i = 0; i < 8; ++i) {
        const float t = static_cast<float>(i) / 7.0f;
        ofColor c = bgTop.getLerped(bgBottom, t);
        c.a = 255;
        ofSetColor(c);
        ofDrawRectangle(0, height * (0.33f + t * 0.067f), width, height * 0.075f);
    }

    const float horizon = height * 0.52f;
    ofSetColor(42, 49, 60, static_cast<int>(50 + arenaMix * 80));
    ofDrawLine(0, horizon, width, horizon);

    for (int i = -9; i <= 9; ++i) {
        const float bottomX = width * 0.5f + i * width * 0.085f;
        const float topX = width * 0.5f + i * width * 0.018f;
        ofSetColor(53, 67, 82, static_cast<int>(30 + arenaMix * 72));
        ofDrawLine(topX, horizon, bottomX, height * 1.02f);
    }

    for (int i = 0; i < 9; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        const float curved = t * t;
        const float y = lerpf(horizon, height * 1.02f, curved);
        ofSetColor(53, 67, 82, static_cast<int>(24 + arenaMix * 68));
        ofDrawLine(0, y, width, y);
    }

    ofSetColor(15, 20, 27, 255);
    ofDrawRectangle(width * 0.04f, horizon - height * 0.17f, width * 0.09f, height * 0.17f);
    ofDrawRectangle(width * 0.15f, horizon - height * 0.11f, width * 0.05f, height * 0.11f);
    ofDrawRectangle(width * 0.78f, horizon - height * 0.15f, width * 0.08f, height * 0.15f);
    ofDrawRectangle(width * 0.88f, horizon - height * 0.09f, width * 0.05f, height * 0.09f);
}

void RobotFightGenerator::drawSparks() const {
    ofPushStyle();
    for (int i = 0; i < 48; ++i) {
        const Spark& s = sparks[i];
        if (!s.active) continue;
        const float life = 1.0f - clamp01(s.age / std::max(0.001f, s.life));
        ofSetColor(255, 235, 170, static_cast<int>(255.0f * life));
        ofDrawLine(s.pos, s.pos - s.vel * (0.045f + s.size * 1.5f));
        ofDrawCircle(s.pos, s.size * life);
    }
    ofPopStyle();
}

void RobotFightGenerator::drawHud(float width, float height) const {
    const float margin = width * 0.055f;
    const float barW = width * 0.34f;
    const float barH = std::max(8.0f, height * 0.018f);
    const float y = height * 0.065f;

    ofSetColor(6, 8, 12, 210);
    ofDrawRectangle(margin - 3.0f, y - 3.0f, barW + 6.0f, barH + 6.0f);
    ofDrawRectangle(width - margin - barW - 3.0f, y - 3.0f, barW + 6.0f, barH + 6.0f);

    ofSetColor(190, 222, 228, 225);
    ofDrawRectangle(margin, y, barW * clamp01(fighters[0].health / 100.0f), barH);
    const float rightHealth = barW * clamp01(fighters[1].health / 100.0f);
    ofDrawRectangle(width - margin - rightHealth, y, rightHealth, barH);

    ofSetColor(150, 168, 176, 190);
    ofDrawBitmapString("UNIT A", margin, y - 10.0f);
    ofDrawBitmapString("UNIT B", width - margin - 48.0f, y - 10.0f);
    ofSetColor(108, 124, 132, 175);
    ofDrawBitmapString(styleName(fighters[0].style), margin, y - 23.0f);
    ofDrawBitmapString(styleName(fighters[1].style), width - margin - 72.0f, y - 23.0f);
    ofSetColor(150, 168, 176, 190);
    ofDrawBitmapString("ROUND " + ofToString(roundNumber), width * 0.47f, y + 4.0f);

    // Tiny initiative mark: useful game-language feedback without another text panel.
    ofSetColor(120, 150, 160, 150);
    const float markerX = (initiativeOwner == 0) ? (margin + barW + 12.0f)
                                                 : (width - margin - barW - 18.0f);
    ofDrawTriangle(markerX, y + barH * 0.5f,
                   markerX + ((initiativeOwner == 0) ? 7.0f : -7.0f), y + 1.0f,
                   markerX + ((initiativeOwner == 0) ? 7.0f : -7.0f), y + barH - 1.0f);

    ofSetColor(108, 124, 132, 160);
    ofDrawBitmapString("COMBAT SIGNAL", width * 0.44f, height * 0.955f);

    if (specialBanner > 0.001f && specialBannerActor >= 0 && specialBannerActor < 2) {
        const float pulse = clamp01(specialBanner / 0.92f);
        ofSetColor(214, 232, 236, static_cast<int>(215.0f * pulse));
        const std::string label = std::string("SIGNATURE // ")
                                + specialName(fighters[specialBannerActor].style);
        ofDrawBitmapString(label, width * 0.405f, height * 0.122f);
    }

    if (roundOver) {
        ofSetColor(235, 244, 246, 235);
        ofDrawBitmapString("SYSTEM FAILURE", width * 0.44f, height * 0.44f);
    }
}

void RobotFightGenerator::draw(float width, float height) {
    if (!ready || width <= 1.0f || height <= 1.0f) return;

    ofPushStyle();
    ofDisableDepthTest();
    ofEnableAlphaBlending();

    drawArena(width, height);

    const float baseScale = std::min(width * 0.46f, height * 0.58f);
    const float zoom = std::max(0.84f, cameraFramingZoom + cameraZoomImpulse);
    const float shakePxX = cameraShakeX * baseScale;
    const float shakePxY = cameraShakeY * baseScale;

    ofPushMatrix();
    ofTranslate(width * 0.5f + shakePxX, height * 0.82f + shakePxY);
    ofScale(baseScale * zoom, baseScale * zoom);
    ofTranslate(-cameraCenterX, 0.0f);

    drawCombatScene2D();
    drawSparks();

    ofPopMatrix();

    if (screenFlash > 0.01f) {
        ofSetColor(235, 248, 255, static_cast<int>(90.0f * clamp01(screenFlash)));
        ofDrawRectangle(0, 0, width, height);
    }

    drawHud(width, height);
    ofPopStyle();
}
