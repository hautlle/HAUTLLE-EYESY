#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>

// Pure C++ — no OpenFrameworks, Lua, or OSC dependencies.
// ofApp feeds clamped raw knob values in and reports state out over OSC.

struct KRPoint {
    uint64_t tMs;   // offset from loop start, 0..loopDurationMs
    float value;
};

struct KnobRecorderLane {
    bool active = false;
    std::vector<KRPoint> points;
    uint64_t loopDurationMs = 0;
    uint64_t loopEpochMs = 0;

    bool armed = false;
    std::vector<KRPoint> scratch;

    float sample(uint64_t nowMs) const;
    void clear();
};

class KnobRecorderManager {
public:
    static constexpr int      kLanes = 5;
    static constexpr float    kDeadband = 0.012f;
    static constexpr uint64_t kMinHoldMs = 20;

    void shiftDown(uint64_t nowMs);
    std::vector<int> shiftUp(uint64_t nowMs);

    float effectiveValue(int lane, float raw01, uint64_t nowMs, bool* outJustArmed = nullptr);
    float handleClearGesture(int lane, float raw01, uint64_t nowMs, bool* outCleared = nullptr);

    void clearLane(int lane);
    void suppressLaneForCurrentShift(int lane);
    void clearAll();

    bool     laneActive(int lane) const;
    bool     laneArmed(int lane) const;
    uint64_t laneLoopDurationMs(int lane) const;
    size_t   lanePointCount(int lane) const;

private:
    std::array<KnobRecorderLane, kLanes> lanes_;
    bool shiftHeld_ = false;
    uint64_t shiftDownMs_ = 0;
    std::array<float, kLanes> baselineRaw_{};
    std::array<bool, kLanes>  haveBaseline_{};
    std::array<bool, kLanes>  suppressRecord_{}; // true after Shift+SceneA clear gesture until Shift release
};
