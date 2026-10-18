-- HAUTLLE_MURMURATION_05_FIELD
-- Claude's 2.5D flock dynamics, with a HAUTLLE rendering layer.
-- Distance controls legibility: far birds are glyphs, near birds resolve into wings.
--
-- K1 = Flock Energy      (speed, agility, flap rate; shortens predator cooldown)
-- K2 = Cohesion / Split  (one cloud -> sub-flocks that drift apart and rejoin)
-- K3 = Population        (350 -> 1600 birds; flock volume grows with it)
-- K4 = Palette           (base hue + autonomous ~45 s chromatic drift)
-- K5 = FIELD             (signal -> liminal/habitat -> unstable observation)
--
-- Audio: bass hit / TRIG = predator dive (cooldown 4-12 s, shorter with K1)
--        mids = slow agitation + more frequent turning waves
--        highs = fine wing shimmer
-- PERSIST: optional smear. The field washes over trails; habitat remains spatially anchored.

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle = "HAUTLLE Murmuration 05 FIELD"
modeExplain = "Natural flock / synthetic observation field"
titleK1 = "Flock Energy"
titleK2 = "Cohesion / Split"
titleK3 = "Population"
titleK4 = "Palette"
titleK5 = "FIELD"

print(modeTitle)

local generatorAvailable = false
local initialized = false
local lastClock = 0.0

local lowPassState = 0.0
local highPassBaseState = 0.0
local smoothLow = 0.0
local smoothMid = 0.0
local smoothHigh = 0.0
local smoothEnergy = 0.0

-- Performance log: one line every PERF_INTERVAL seconds in the service journal.
-- journalctl -u eyesyof.service -f | grep MURMFLD
local PERF_INTERVAL = 5.0
local perfTimer = 0.0
local perfFrames = 0
local perfWorstDt = 0.0
local perfHitches = 0

local LOW_ALPHA = 0.0253
local HIGH_ALPHA = 0.2690

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
    print("HAUTLLE_MURMURATION_05_FIELD setup")

    generatorAvailable =
        type(murmfield_prepare) == "function" and
        type(murmfield_reset) == "function" and
        type(murmfield_step) == "function" and
        type(murmfield_draw) == "function" and
        type(murmfield_is_ready) == "function"

    if not generatorAvailable then
        print("HAUTLLE_MURMURATION_05_FIELD ERROR: native murmfield_* API missing")
        return
    end

    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    murmfield_prepare()
    murmfield_reset()   -- fresh flock every time the mode is selected
    if not murmfield_is_ready() then
        print("HAUTLLE_MURMURATION_05_FIELD ERROR: native generator not ready")
        generatorAvailable = false
        return
    end

    lowPassState, highPassBaseState = 0.0, 0.0
    smoothLow, smoothMid, smoothHigh, smoothEnergy = 0.0, 0.0, 0.0, 0.0
    lastClock = now_seconds()
    initialized = true
    print("HAUTLLE_MURMURATION_05_FIELD ready")
end

function update()
end

function draw()
    local W = renderW or 1280
    local H = renderH or 720

    if not generatorAvailable or not initialized then
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
        local birds, _, simAvg, simMax, drawAvg, drawMax = 0, false, 0, 0, 0, 0
        if type(murmfield_stats) == "function" then
            birds, _, simAvg, simMax, drawAvg, drawMax = murmfield_stats()
        end
        print(string.format(
            "MURMFLD fps=%.1f worst=%.1fms hitches=%d sim=%.2f/%.2fms draw=%.2f/%.2fms birds=%d luaKB=%d",
            perfFrames / perfTimer, perfWorstDt * 1000.0, perfHitches,
            simAvg or 0, simMax or 0, drawAvg or 0, drawMax or 0,
            birds or 0, math.floor(collectgarbage("count"))))
        perfTimer, perfFrames, perfWorstDt, perfHitches = 0.0, 0, 0.0, 0
    end

    local low, mid, high, energy = compute_audio_bands()

    murmfield_step(
        dt, low, mid, high, energy, trig_now(),
        clamp01(getv("knob1", 0.45)),
        clamp01(getv("knob2", 0.35)),
        clamp01(getv("knob3", 0.55)),
        clamp01(getv("knob4", 0.58)),
        clamp01(getv("knob5", 0.45))
    )

    local persistOn = persist == true or persist == 1
    local clearNow = persistClear == true or persistClear == 1
    murmfield_draw(W, H, persistOn, clearNow, persistFade or 12)
end

function exit()
    if type(murmfield_release) == "function" then
        murmfield_release()   -- free the VBO ring before the next mode allocates
    end
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end
    print("HAUTLLE_MURMURATION_05_FIELD exit")
end
