-- HAUTLLE_MASST_WALK
-- Vector-wireframe walk down Massachusetts St, Lawrence KS (7th to 10th).
-- Fixed 3 mph centreline glide; no bob, sway or yaw.
-- See-through 90s wireframe; loops from 10th St straight back into 7th.
-- Map data (c) OpenStreetMap contributors, ODbL.
--
-- K1 = Depth           (near/misted -> long/clear, facade detail exposure)
-- K2 = Structure Pulse (rotating building groups + occasional travelling wave)
-- K3 = Occupancy       (lit storefront/window density + high-frequency flicker)
-- K4 = Signal Age      (fresh green -> warm/faded, echoes + PERSIST trails)
-- K5 = Field Error     (dropout, phase-slip, registration echo, jitter/tears)
--
-- Audio: kick / TRIG = spaced building-group pulse / occasional wave; mids = more lit windows (slowly);
--        highs = window flicker; overall energy = line brightness.
-- PERSIST: phosphor trails (the background becomes a translucent wash).

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle = "HAUTLLE Mass St Walk"
modeExplain = "Centreline glide at 3 mph, 7th to 10th"
titleK1 = "Depth"
titleK2 = "Structure Pulse"
titleK3 = "Occupancy"
titleK4 = "Signal Age"
titleK5 = "Field Error"

local DATA_PATH = "/sdcard/Modes/oFLua/HAUTLLE_MASST_WALK/masst.dat"

print(modeTitle)

local ready = false
local lastClock = 0.0

local lowPassState, highPassBaseState = 0.0, 0.0
local smoothLow, smoothMid, smoothHigh, smoothEnergy = 0.0, 0.0, 0.0, 0.0
local LOW_ALPHA = 0.0253
local HIGH_ALPHA = 0.2690

local PERF_INTERVAL = 5.0
local perfTimer, perfFrames, perfWorstDt, perfHitches = 0.0, 0, 0.0, 0

local function clamp01(x)
    x = tonumber(x) or 0.0
    if x < 0.0 then return 0.0 end
    if x > 1.0 then return 1.0 end
    return x
end

local function getv(name, fallback)
    local v = _G[name]
    if type(v) == "number" then return v end
    if type(v) == "function" then
        local ok, r = pcall(v)
        if ok and type(r) == "number" then return r end
    end
    return fallback
end

local function now_seconds()
    if of ~= nil and type(of.getElapsedTimef) == "function" then
        return of.getElapsedTimef()
    end
    return os.clock()
end

local function trig_now()
    if type(trig) == "boolean" then return trig end
    return (tonumber(trig) or 0) > 0
end

local function compress_level(rms, gain)
    return clamp01(1.0 - math.exp(-math.max(0.0, rms) * gain))
end

local function smooth_value(current, target, attack, release)
    local amount = target > current and attack or release
    return current + (target - current) * amount
end

local function compute_audio_bands()
    local lowPower, midPower, highPower = 0.0, 0.0, 0.0
    local sampleCount = math.max(#inL, #inR)
    if sampleCount < 1 then sampleCount = 100 end
    if sampleCount > 256 then sampleCount = 256 end

    for i = 1, sampleCount do
        local left = tonumber(inL[i]) or 0.0
        local right = tonumber(inR[i]) or left
        local sample = (left + right) * 0.5
        lowPassState = lowPassState + LOW_ALPHA * (sample - lowPassState)
        highPassBaseState = highPassBaseState + HIGH_ALPHA * (sample - highPassBaseState)
        local low = lowPassState
        local mid = highPassBaseState - lowPassState
        local high = sample - highPassBaseState
        lowPower = lowPower + low * low
        midPower = midPower + mid * mid
        highPower = highPower + high * high
    end

    local low = compress_level(math.sqrt(lowPower / sampleCount), 8.0)
    local mid = compress_level(math.sqrt(midPower / sampleCount), 5.4)
    local high = compress_level(math.sqrt(highPower / sampleCount), 3.8)

    smoothLow = smooth_value(smoothLow, low, 0.42, 0.13)
    smoothMid = smooth_value(smoothMid, mid, 0.48, 0.16)
    smoothHigh = smooth_value(smoothHigh, high, 0.56, 0.21)

    local spectralEnergy = clamp01(smoothLow * 0.43 + smoothMid * 0.37 + smoothHigh * 0.20)
    local enginePeak = clamp01(getv("peak", getv("audio_peak", 0.0)) * 1.08)
    local energy = math.max(spectralEnergy, enginePeak * 0.80)
    smoothEnergy = smooth_value(smoothEnergy, energy, 0.50, 0.10)
    return smoothLow, smoothMid, smoothHigh, smoothEnergy
end

function setup()
    print("HAUTLLE_MASST_WALK setup")
    if type(masst_load) ~= "function" or type(masst_step) ~= "function"
        or type(masst_draw) ~= "function" then
        print("HAUTLLE_MASST_WALK ERROR: native masst_* API missing")
        return
    end
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    local ok, count = masst_load(DATA_PATH)
    if not ok then
        print("HAUTLLE_MASST_WALK ERROR: could not load " .. DATA_PATH)
        return
    end
    masst_reset()   -- start at 7th St every time the mode is selected
    lowPassState, highPassBaseState = 0.0, 0.0
    smoothLow, smoothMid, smoothHigh, smoothEnergy = 0.0, 0.0, 0.0, 0.0
    lastClock = now_seconds()
    ready = true
    print("HAUTLLE_MASST_WALK ready: " .. tostring(count) .. " buildings")
end

function update()
end

function draw()
    local W = renderW or 1280
    local H = renderH or 720

    if not ready then
        of.setColor(48, 0, 8, 255)
        of.drawRectangle(0, 0, W, H)
        return
    end

    local now = now_seconds()
    local dt = now - lastClock
    lastClock = now
    local rawDt = dt
    if dt <= 0.0 or dt > 0.10 then dt = 1.0 / 60.0 end

    perfFrames = perfFrames + 1
    perfTimer = perfTimer + math.max(0.0, rawDt)
    if rawDt > perfWorstDt then perfWorstDt = rawDt end
    if rawDt > 0.025 then perfHitches = perfHitches + 1 end
    if perfTimer >= PERF_INTERVAL then
        local segs, simAvg, simMax, drawAvg, drawMax = 0, 0, 0, 0, 0
        if type(masst_stats) == "function" then
            segs, simAvg, simMax, drawAvg, drawMax = masst_stats()
        end
        print(string.format(
            "MASST fps=%.1f worst=%.1fms hitches=%d sim=%.2f/%.2fms draw=%.2f/%.2fms segs=%d luaKB=%d",
            perfFrames / perfTimer, perfWorstDt * 1000.0, perfHitches,
            simAvg or 0, simMax or 0, drawAvg or 0, drawMax or 0, segs or 0,
            math.floor(collectgarbage("count"))))
        perfTimer, perfFrames, perfWorstDt, perfHitches = 0.0, 0, 0.0, 0
    end

    local low, mid, high, energy = compute_audio_bands()
    masst_step(
        dt, low, mid, high, energy, trig_now(),
        clamp01(getv("knob1", 0.4)),
        clamp01(getv("knob2", 0.5)),
        clamp01(getv("knob3", 0.5)),
        clamp01(getv("knob4", 0.33)),
        clamp01(getv("knob5", 0.1))
    )

    local persistOn = persist == true or persist == 1
    local clearNow = persistClear == true or persistClear == 1
    masst_draw(W, H, persistOn, clearNow, persistFade or 12)
end

function exit()
    print("HAUTLLE_MASST_WALK exit")
end
