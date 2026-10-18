-- HAUTLLE_ARCHITECTURE_02_STAGE4
-- Living single-texture architecture: discs, planes, anchors, parallax, mutation, and impossible connections.
-- No ShaderHost yet. Normal EYESY Persist is supported explicitly.

require("eyesy")

modeTitle = "HAUTLLE Architecture 02 Stage 4"
titleK1 = "Density"
titleK2 = "Build Rate"
titleK3 = "Structural Stress"
titleK4 = "Architectural Character"
titleK5 = "Depth / Atmosphere"
print(modeTitle)

local generatorAvailable = false
local lastClock = 0.0

local function clamp01(v)
    if v == nil then return 0.0 end
    if v < 0.0 then return 0.0 end
    if v > 1.0 then return 1.0 end
    return v
end

local function now_seconds()
    if of ~= nil and type(of.getElapsedTimef) == "function" then
        return of.getElapsedTimef()
    end
    return os.clock()
end

function setup()
    generatorAvailable =
        type(architecture_reset) == "function" and
        type(architecture_step) == "function" and
        type(architecture_draw) == "function" and
        type(architecture_is_ready) == "function"

    if not generatorAvailable then
        print("ARCHITECTURE_STAGE4: native generator API missing")
        return
    end

    architecture_reset()

    if not architecture_is_ready() then
        print("ARCHITECTURE_STAGE4: native texture allocation failed")
        generatorAvailable = false
        return
    end

    lastClock = now_seconds()
    print("ARCHITECTURE_STAGE4: native generator ready")
    print("ARCHITECTURE_STAGE4: normal Persist supported; discs/planes/anchors active; ShaderHost still disabled")
end

function update()
end

function draw()
    if not generatorAvailable then
        return
    end

    local w = renderW or 1280
    local h = renderH or 720

    local nowClock = now_seconds()
    local dt = nowClock - lastClock
    lastClock = nowClock

    if dt <= 0.0 or dt > 0.20 then
        dt = 1.0 / 60.0
    end

    local k1 = clamp01(knob1 or 0.40)
    local k2 = clamp01(knob2 or 0.25)
    local k3 = clamp01(knob3 or 0.72)
    local k4 = clamp01(knob4 or 0.42)
    local k5 = clamp01(knob5 or 0.45)

    local peakNow = clamp01(peak or 0.0)
    local trigNow = trig == true or trig == 1

    architecture_step(dt, peakNow, trigNow,
                      k1, k2, k3, k4, k5)

    local persistOn = persist == true or persist == 1
    local clearNow = persistClear == true or persistClear == 1
    local fade = persistFade or 12

    architecture_draw(w, h, persistOn, clearNow, fade)
end

function exit()
    print("HAUTLLE_ARCHITECTURE_02_STAGE4 finished")
end
