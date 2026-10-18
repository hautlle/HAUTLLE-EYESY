-- HAUTLLE_ROBOT_FIGHT_01
-- V0.8 style signatures + momentum-gated specials.
-- Keeps the fast V0.4.2 painter renderer and all V0.5-V0.7 combat/air systems.

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle   = "HAUTLLE ROBOT FIGHT 08"
modeExplain = "Style signatures / rare momentum-gated specials / authored finishers"
titleK1 = "Combat"
titleK2 = "Music Lock"
titleK3 = "Cinema"
titleK4 = "Palette"
titleK5 = "Arena"

local available = false

local function clamp01(x)
    x = tonumber(x) or 0
    if x < 0 then return 0 end
    if x > 1 then return 1 end
    return x
end

local function getv(name, fallback)
    local v = _G[name]
    if type(v) == "number" then return v end
    if type(v) == "function" then
        local ok, result = pcall(v)
        if ok and type(result) == "number" then return result end
    end
    return fallback
end

local function trig_now()
    if type(trig) == "boolean" then return trig end
    return (tonumber(trig) or 0) > 0
end

local function audio_energy()
    local raw = clamp01(getv("peak", getv("audio_peak", 0.0)))
    raw = clamp01(raw * 2.35)
    return math.sqrt(raw)
end

function setup()
    available =
        type(fight_reset) == "function" and
        type(fight_step) == "function" and
        type(fight_draw) == "function" and
        type(fight_is_ready) == "function"

    if not available then
        print("HAUTLLE_ROBOT_FIGHT_01 ERROR: native fight_* API missing")
        return
    end

    fight_reset()
    print("HAUTLLE_ROBOT_FIGHT_01 V0.8 signatures + specials ready")
end

function update()
end

function draw()
    local W = renderW or 1280
    local H = renderH or 720

    if not available or not fight_is_ready() then
        of.setColor(40, 0, 0, 255)
        of.drawRectangle(0, 0, W, H)
        return
    end

    local dt = 0.0 -- native engine uses real elapsed wall time

    local k1 = clamp01(getv("knob1", 0.48))
    local k2 = clamp01(getv("knob2", 0.48))
    local k3 = clamp01(getv("knob3", 0.48))
    local k4 = clamp01(getv("knob4", 0.08))
    local k5 = clamp01(getv("knob5", 0.42))

    fight_step(dt, audio_energy(), trig_now(), k1, k2, k3, k4, k5)
    fight_draw(W, H)
end

function exit()
    print("HAUTLLE_ROBOT_FIGHT_01 exit")
end
