#include "KnobRecorder.h"
#include <cmath>
#include <cstdio>

#define KR_LOG(...) do { } while(0)

static inline float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float KnobRecorderLane::sample(uint64_t nowMs) const {
    if (!active || points.empty() || loopDurationMs == 0) return 0.0f;

    uint64_t t = (nowMs - loopEpochMs) % loopDurationMs;

    if (points.size() == 1) return points[0].value;

    for (size_t i = 0; i + 1 < points.size(); i++) {
        if (t >= points[i].tMs && t <= points[i + 1].tMs) {
            uint64_t span = points[i + 1].tMs - points[i].tMs;
            if (span == 0) return points[i].value;
            float frac = (float)(t - points[i].tMs) / (float)span;
            return points[i].value + frac * (points[i + 1].value - points[i].value);
        }
    }

    uint64_t lastT = points.back().tMs;
    uint64_t wrapSpan = loopDurationMs - lastT;
    if (t >= lastT && wrapSpan > 0) {
        float frac = (float)(t - lastT) / (float)wrapSpan;
        return points.back().value + frac * (points.front().value - points.back().value);
    }

    return points.back().value;
}

void KnobRecorderLane::clear() {
    active = false;
    points.clear();
    loopDurationMs = 0;
    loopEpochMs = 0;
    armed = false;
    scratch.clear();
}

void KnobRecorderManager::shiftDown(uint64_t nowMs) {
    shiftHeld_ = true;
    shiftDownMs_ = nowMs;
    haveBaseline_.fill(false);
    suppressRecord_.fill(false);

    for (auto &lane : lanes_) {
        lane.armed = false;
        lane.scratch.clear();
    }

    KR_LOG("[KnobRecorder] shift down\n");
}

std::vector<int> KnobRecorderManager::shiftUp(uint64_t nowMs) {
    shiftHeld_ = false;
    std::vector<int> committed;
    uint64_t heldMs = (nowMs > shiftDownMs_) ? (nowMs - shiftDownMs_) : 0;

    for (int i = 0; i < kLanes; i++) {
        auto &lane = lanes_[i];
        if (!lane.armed) continue;

        if (heldMs < kMinHoldMs || lane.scratch.size() < 2) {
            lane.armed = false;
            lane.scratch.clear();
            continue;
        }

        KRPoint last = lane.scratch.back();
        if (last.tMs < heldMs) {
            lane.scratch.push_back({heldMs, last.value});
        }

        lane.points = lane.scratch;
        lane.loopDurationMs = heldMs;
        lane.loopEpochMs = nowMs;
        lane.active = true;
        lane.armed = false;
        lane.scratch.clear();
        committed.push_back(i);

        KR_LOG("[KnobRecorder] lane %d committed loop=%llums points=%zu\n",
               i, (unsigned long long)heldMs, lane.points.size());
    }

    KR_LOG("[KnobRecorder] shift up (held=%llums, %zu lane(s) committed)\n",
           (unsigned long long)heldMs, committed.size());

    return committed;
}

float KnobRecorderManager::effectiveValue(int lane, float raw01, uint64_t nowMs, bool* outJustArmed) {
    if (outJustArmed) *outJustArmed = false;
    raw01 = clamp01(raw01);

    if (lane < 0 || lane >= kLanes) return raw01;

    auto &L = lanes_[lane];

    if (shiftHeld_) {
        if (!haveBaseline_[lane]) {
            baselineRaw_[lane] = raw01;
            haveBaseline_[lane] = true;
        }

        if (suppressRecord_[lane]) return raw01;

        if (!L.armed && std::fabs(raw01 - baselineRaw_[lane]) > kDeadband) {
            L.armed = true;
            L.scratch.clear();

            uint64_t t = (nowMs > shiftDownMs_) ? (nowMs - shiftDownMs_) : 0;

            L.scratch.push_back({0, baselineRaw_[lane]});
            L.scratch.push_back({t, raw01});

            if (outJustArmed) *outJustArmed = true;

            KR_LOG("[KnobRecorder] lane %d armed (baseline=%.3f, t=%llums)\n",
                   lane, baselineRaw_[lane], (unsigned long long)t);
        } else if (L.armed) {
            uint64_t t = (nowMs > shiftDownMs_) ? (nowMs - shiftDownMs_) : 0;
            L.scratch.push_back({t, raw01});
        }

        if (L.armed) return raw01;
    }

    if (L.active) return L.sample(nowMs);
    return raw01;
}

float KnobRecorderManager::handleClearGesture(int lane, float raw01, uint64_t nowMs, bool* outCleared) {
    if (outCleared) *outCleared = false;
    (void)nowMs;

    raw01 = clamp01(raw01);
    if (lane < 0 || lane >= kLanes) return raw01;

    if (!haveBaseline_[lane]) {
        baselineRaw_[lane] = raw01;
        haveBaseline_[lane] = true;
    }

    auto &L = lanes_[lane];

    if (std::fabs(raw01 - baselineRaw_[lane]) > kDeadband) {
        // Critical: once a knob has been moved in Shift+SceneA clear mode,
        // do not let that same movement become a new recording if Scene A is
        // released before Shift. Suppression lasts until the next Shift-down.
        suppressRecord_[lane] = true;

        if (L.active) {
            L.clear();
            if (outCleared) *outCleared = true;
            KR_LOG("[KnobRecorder] lane %d cleared via Shift+SceneA gesture\n", lane);
        } else {
            L.armed = false;
            L.scratch.clear();
        }
    }

    return raw01;
}

void KnobRecorderManager::clearLane(int lane) {
    if (lane < 0 || lane >= kLanes) return;
    lanes_[lane].clear();
    KR_LOG("[KnobRecorder] lane %d cleared\n", lane);
}

void KnobRecorderManager::suppressLaneForCurrentShift(int lane) {
    if (lane < 0 || lane >= kLanes) return;
    suppressRecord_[lane] = true;
    lanes_[lane].armed = false;
    lanes_[lane].scratch.clear();
    KR_LOG("[KnobRecorder] lane %d suppressed until Shift release\n", lane);
}

void KnobRecorderManager::clearAll() {
    for (int i = 0; i < kLanes; i++) {
        lanes_[i].clear();
        if (shiftHeld_) suppressRecord_[i] = true;
    }
    KR_LOG("[KnobRecorder] all lanes cleared\n");
}

bool KnobRecorderManager::laneActive(int lane) const {
    if (lane < 0 || lane >= kLanes) return false;
    return lanes_[lane].active;
}

bool KnobRecorderManager::laneArmed(int lane) const {
    if (lane < 0 || lane >= kLanes) return false;
    return lanes_[lane].armed;
}

uint64_t KnobRecorderManager::laneLoopDurationMs(int lane) const {
    if (lane < 0 || lane >= kLanes) return 0;
    return lanes_[lane].loopDurationMs;
}

size_t KnobRecorderManager::lanePointCount(int lane) const {
    if (lane < 0 || lane >= kLanes) return 0;
    return lanes_[lane].points.size();
}
