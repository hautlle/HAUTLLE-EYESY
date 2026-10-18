#pragma once

#include "../bridge/NativeGenerator.h"
#include "ofMain.h"
#include <cstdint>

class RobotFightGenerator : public NativeGenerator {
public:
    void registerFunctions(lua_State* L) override;
    void reset() override;

private:
    enum MoveType {
        MOVE_IDLE = 0,
        MOVE_JAB,
        MOVE_CROSS,
        MOVE_BODY,
        MOVE_LOW_KICK,
        MOVE_ROUNDHOUSE,
        MOVE_HEAVY,
        // V0.8: one style-dependent authored special strike. The style chooses
        // the pose/timing; the shared combat system still owns hit resolution.
        MOVE_SPECIAL,
        MOVE_BLOCK_HIGH,
        MOVE_BLOCK_LOW,
        MOVE_EVADE,
        MOVE_HIT_HIGH,
        MOVE_HIT_LOW,
        MOVE_HIT_HEAVY,
        MOVE_KNOCKDOWN,
        MOVE_GETUP,
        // V0.7 airborne commands. These are not grounded move clips; they are
        // routed through the lightweight authored air-state system.
        MOVE_JUMP_KICK,
        MOVE_HOP_BACK,
        MOVE_JUMP_PUNCH
    };

    enum Outcome {
        OUTCOME_HIT = 0,
        OUTCOME_BLOCK,
        OUTCOME_MISS,
        OUTCOME_CLASH
    };

    enum ActionType {
        ACTION_MOVE = 0,
        ACTION_STEP,
        ACTION_AIR
    };

    enum AirMode {
        AIR_GROUNDED = 0,
        AIR_JUMP_KICK,
        AIR_HOP_BACK,
        AIR_JUMP_PUNCH
    };

    // V0.5: neutral is an active fighting state, not a limp idle pose.
    enum NeutralBehavior {
        NEUTRAL_SETTLE = 0,
        NEUTRAL_WEIGHT_LEAD,
        NEUTRAL_WEIGHT_REAR,
        NEUTRAL_SLIP_INSIDE,
        NEUTRAL_SLIP_OUTSIDE,
        NEUTRAL_GUARD_TWITCH,
        NEUTRAL_FEINT
    };

    // V0.6: persistent per-round fighter personality. Styles only bias the
    // shared combat vocabulary; they do not hard-lock moves or outcomes.
    enum FighterStyle {
        STYLE_PRESSURE = 0,
        STYLE_COUNTER,
        STYLE_KICKER,
        STYLE_HEAVY,
        STYLE_AGILE,
        STYLE_BRAWLER,
        STYLE_COUNT
    };

    struct StyleProfile {
        float preferredSep;
        float stanceScale;
        float stepScale;
        float stepDurationScale;
        float neutralDurationScale;
        float recoilScale;
        float initiativeHold;
        float guardRootDown;
        float guardTorsoLean;
        float frontShoulderDelta;
        float frontElbowDelta;
        float rearShoulderDelta;
        float rearElbowDelta;
        // Air behavior biases. Values are deliberately small because aerial
        // phrases are exceptional punctuation, not the default locomotion.
        float jumpKickBias;
        float hopBackBias;
        float jumpPunchBias;
    };

    // V0.3 movement pose.
    // Arms are hierarchical: shoulder is local to torso, elbow is local to upper arm.
    // Legs are not authored as angles. They are desired foot targets plus plant weights,
    // solved at draw time with analytic two-bone IK.
    struct Pose {
        float rootForward;
        float rootDown;
        float pelvisTilt;
        float torsoLean;
        float headLean;

        float frontShoulder;
        float frontElbow;
        float rearShoulder;
        float rearElbow;

        // Pseudo-Z is presentation depth only. It never changes hit logic or IK.
        // Each arm can cross the torso plane during a move instead of being
        // permanently labeled front/rear by stance.
        float frontShoulderZ;
        float frontElbowZ;
        float frontHandZ;
        float rearShoulderZ;
        float rearElbowZ;
        float rearHandZ;

        float frontFootForward;
        float frontFootDown;
        float frontPlant;
        float rearFootForward;
        float rearFootDown;
        float rearPlant;

        float frontHipZ;
        float frontKneeZ;
        float frontFootZ;
        float rearHipZ;
        float rearKneeZ;
        float rearFootZ;
    };

    struct MoveDef {
        float duration;
        float hitFrac;
        float damage;
        float knockback;
        float hitStop;
        float cameraPunch;
    };

    struct Fighter {
        float x;
        float z;
        int facing;
        float health;
        MoveType move;
        float moveTime;
        Outcome outcome;
        bool hitResolved;
        float flash;
        float idlePhase;

        // Short authored neutral intentions keep the guard alive without random
        // per-frame noise. Each behavior eases in/out and returns to guard.
        NeutralBehavior neutralBehavior;
        float neutralBehaviorTime;
        float neutralBehaviorDuration;

        // Root-to-ground stance state.
        float frontFootX;
        float frontFootY;
        float rearFootX;
        float rearFootY;
        bool nextSwingFront;

        // One deliberate fighting-game step at a time. One foot stays planted,
        // one foot swings, and the root travels between them.
        bool stepActive;
        bool stepSwingFront;
        float stepStartX;
        float stepTargetX;
        float stepTime;
        float stepDuration;
        float stepDirection;
        float swingStartX;
        float swingTargetX;
        float trailStartX;
        float trailTargetX;

        // V0.7 authored airborne phrase. X follows a known start/target path;
        // Y is expressed as a pose/root arc rather than free physics.
        AirMode airMode;
        float airTime;
        float airDuration;
        float airStartX;
        float airTargetX;
        float airHeight;
        float airHitFrac;
        Outcome airOutcome;
        bool airHitResolved;

        // Landing compression is a short post-air pose envelope, separate from
        // the air state so feet can be planted immediately on touchdown.
        float landingTime;
        float landingDuration;
        float landingStrength;

        // Impact displacement is a critically damped COM offset, not permanent
        // fighter translation. Feet stay grounded and legs absorb the impulse.
        float impactX;
        float impactVX;
        float impactY;
        float impactVY;

        int chassis;
        FighterStyle style;
        float momentum;
    };

    struct ExchangeAction {
        float at;
        int actor;
        ActionType type;
        MoveType move;
        Outcome outcome;
        float amount;
        float duration;
    };

    struct Spark {
        bool active;
        ofVec2f pos;
        ofVec2f vel;
        float age;
        float life;
        float size;
    };

    struct ChassisDims {
        float shoulderHalf;
        float hipHalf;
        float torsoWidth;
        float upperArmLen;
        float forearmLen;
        float thighLen;
        float shinLen;
        float limbScale;
        float headScale;
    };

    struct RigPoints {
        ofVec2f pelvis;
        ofVec2f waist;
        ofVec2f chest;
        ofVec2f neckBase;
        ofVec2f head;
        ofVec2f rearShoulder;
        ofVec2f frontShoulder;
        ofVec2f rearElbow;
        ofVec2f rearHand;
        ofVec2f frontElbow;
        ofVec2f frontHand;
        ofVec2f rearHip;
        ofVec2f frontHip;
        ofVec2f rearKnee;
        ofVec2f rearFoot;
        ofVec2f frontKnee;
        ofVec2f frontFoot;
        float torsoAngle;
        float headAngle;
        float pelvisAngle;

        float rearShoulderZ;
        float rearElbowZ;
        float rearHandZ;
        float frontShoulderZ;
        float frontElbowZ;
        float frontHandZ;
        float rearHipZ;
        float rearKneeZ;
        float rearFootZ;
        float frontHipZ;
        float frontKneeZ;
        float frontFootZ;
    };

    static int trampolineReset(lua_State* L);
    static int trampolineStep(lua_State* L);
    static int trampolineDraw(lua_State* L);
    static int trampolineReady(lua_State* L);

    void step(float dt, float energy, bool trigFired,
              float k1, float k2, float k3, float k4, float k5);
    void draw(float width, float height);

    static float clamp01(float v);
    static float smooth01(float v);
    static float smoother01(float v);
    static float lerpf(float a, float b, float t);
    static float degToRad(float d);
    static Pose blendPose(const Pose& a, const Pose& b, float t);
    static Pose sanitizePose(const Pose& p);
    static void criticalSpringZero(float& x, float& v, float omega, float dt);

    uint32_t nextRand();
    float rand01();
    int randInt(int lo, int hi);

    MoveDef moveDef(MoveType move) const;
    MoveDef moveDefForStyle(MoveType move, FighterStyle style) const;
    StyleProfile styleProfile(FighterStyle style) const;
    const char* styleName(FighterStyle style) const;
    const char* specialName(FighterStyle style) const;
    int signatureExchangeId(FighterStyle style) const;
    int specialExchangeId(FighterStyle style) const;
    float styleExchangeWeight(FighterStyle style, int exchangeId) const;
    Pose guardPose(const Fighter& f) const;
    Pose samplePose(const Fighter& f) const;
    Pose attackPose(MoveType move, float u, const Pose& base) const;
    Pose specialPose(const Fighter& f, float u, const Pose& base) const;
    Pose reactionPose(MoveType move, float u, const Pose& base) const;
    Pose airPose(const Fighter& f, const Pose& base) const;

    void updateFighter(Fighter& f, float dt);
    void updateNeutralBehavior(Fighter& f, float dt);
    void chooseNeutralBehavior(Fighter& f);
    void startMove(Fighter& f, MoveType move, Outcome outcome);
    void startStep(Fighter& f, float forwardAmount, float duration);
    void cancelStep(Fighter& f);
    void updateStep(Fighter& f, float dt);
    void startAir(Fighter& f, MoveType command, Outcome outcome,
                  float forwardAmount, float duration);
    void finishAir(Fighter& f, bool compressedLanding = true);
    void updateAir(Fighter& f, float dt);
    bool anyStepActive() const;
    bool anyAirborne() const;
    void maintainNeutralSpacing(float dt);
    void enforceSpacingBounds();
    void shiftFighterWorld(Fighter& f, float dx);
    void plantDefaultStance(Fighter& f);

    void resolveAttack(int attackerIndex);
    void applyHit(int attackerIndex, int defenderIndex, MoveType move, Outcome outcome);

    void startNextExchange();
    int chooseStyledExchange(int leadIndex);
    void loadExchange(int id, bool flipActors);
    void updateDirector(float dt, bool trigFired);
    void finishExchange();
    void resetRound();

    void spawnSparks(const ofVec2f& pos, int count, float force);
    void updateSparks(float dt);
    ofVec2f impactPoint(const Fighter& attacker, MoveType move) const;

    void updateCamera(float dt);
    void drawArena(float width, float height) const;
    void drawCombatScene2D() const;
    void drawRobotSegment(const ofVec2f& a, const ofVec2f& b,
                          float thickness, const ofColor& c,
                          float endScale = 0.78f) const;
    void drawRobotJoint(const ofVec2f& p, float radius, const ofColor& c) const;
    void drawSparks() const;
    void drawHud(float width, float height) const;

    ChassisDims chassisDims(const Fighter& f) const;
    RigPoints solveRig(const Fighter& f, const Pose& pose) const;
    ofVec2f bodyOffset(float forward, float down, float angle, int facing) const;
    ofVec2f hierarchicalLimb(float localDegrees, float length,
                             float parentAngle, int facing) const;
    void solveTwoBone(const ofVec2f& hip, const ofVec2f& desiredFoot,
                      float upperLen, float lowerLen, int facing,
                      ofVec2f& outKnee, ofVec2f& outFoot) const;

    Fighter fighters[2];
    ExchangeAction actions[16];
    Spark sparks[48];

    int actionCount = 0;
    int nextAction = 0;
    int currentExchangeId = -1;
    float exchangeTime = 0.0f;
    float exchangeDuration = 0.0f;
    float neutralTimer = 0.0f;
    bool exchangeActive = false;
    int initiativeOwner = 0;
    int exchangeNextInitiative = 0;
    float clashCooldown = 0.0f;
    float airCooldown = 0.0f;
    // Per-fighter pacing gates. Signature combos recur often enough to define
    // personality; specials are rarer punctuation and require momentum/audio.
    float signatureCooldown[2] = {0.0f, 0.0f};
    float specialCooldown[2] = {0.0f, 0.0f};
    float specialBanner = 0.0f;
    int specialBannerActor = -1;

    bool ready = false;
    bool roundOver = false;
    float roundTimer = 0.0f;
    int roundNumber = 1;

    float energySmooth = 0.0f;
    float triggerBoost = 0.0f;
    float triggerDensity = 0.0f;
    float timeSinceTrigger = 10.0f;
    float hitStopRemaining = 0.0f;
    float cameraShakeX = 0.0f;
    float cameraShakeY = 0.0f;
    float cameraZoomImpulse = 0.0f;
    float cameraCenterX = 0.0f;
    float cameraFramingZoom = 1.0f;
    float screenFlash = 0.0f;

    float kCombat = 0.5f;
    float kMusicLock = 0.5f;
    float kCinema = 0.5f;
    float kPalette = 0.0f;
    float kArena = 0.4f;

    float lastWallTime = -1.0f;
    uint32_t rngState = 0x6D2B79F5u;
};
