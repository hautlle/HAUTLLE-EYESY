-- HAUTLLE_VECTOR_IMPACT_02_1
-- Native sacred-geometry vector instrument for the custom EYESY OF/Lua engine.
--
-- Three independently transformed layers:
--   CORE   = rigid structural mass, driven mainly by low frequencies
--   SHELL  = hinged/sheared articulation, driven mainly by mid frequencies
--   IMPACT = spikes, edge extensions, and vertex accents, driven by highs
--
-- K1 = Topology      : bias the current phase toward alternate solids
-- K2 = Radial/Zoom   : separation plus deliberate off-screen overscan
-- K3 = Hinge         : bias shell articulation and shear
-- K4 = Axis          : set the center of the autonomous axis motion
-- K5 = Impact/Evolve : audio sensitivity, event density, phase intensity
--
-- The five knobs remain playable macro controls, but they now feed a small
-- modular control system: asymmetric slew generators, Schmitt gates,
-- leaky event accumulators, sample-and-hold voltages, impact envelopes, and
-- an audio-clocked five-phase state machine.
--
-- IMPORTANT:
-- This mode deliberately owns the complete frame.
--
-- No ShaderHost texture pass.
-- No private feedback.
-- No normal EYESY Persist trails.
--
-- Both the outer EYESY canvas and the native VectorImpact FBO are forced
-- into a deterministic clean state so no previous-mode framebuffer can leak
-- through during a mode transition.

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle   = "HAUTLLE VECTOR IMPACT 02.1"
modeExplain = "Evolving sacred geometry with modular audio control"

titleK1 = "Topology"
titleK2 = "Radial/Zoom"
titleK3 = "Hinge"
titleK4 = "Axis"
titleK5 = "Impact/Evolve"

print(modeTitle)

local generatorAvailable = false
local initialized = false

-- Always start this instrument from a clean native state.
local RESET_ON_SETUP = true

local lowPassState = 0.0
local highPassBaseState = 0.0

local smoothLow = 0.0
local smoothMid = 0.0
local smoothHigh = 0.0
local smoothEnergy = 0.0

-- Modular control state. These live entirely in Lua, so V2 uses the same
-- native VectorImpact generator and does not allocate another framebuffer.
local controlTopology = 0.0
local controlRadial = 0.0
local controlHinge = 0.0
local controlImpact = 0.0
local axisPhase = 0.0
local slowPhase = 0.0

local impactEnvelope = 0.0
local eventCharge = 0.0
local eventCooldown = 0.0
local holdA = 0.0
local holdB = 0.0
local trigWasHigh = false

local energyGate = { high = false }
local lowGate = { high = false }
local highGate = { high = false }

local phaseIndex = 1
local phaseTime = 0.0
local phaseDuration = 12.0
local phaseFade = 0.0
local randomState = 17357

-- Each phase is a patch rather than a hard scene. Knobs bias these values,
-- audio animates them, and slew prevents discontinuous jumps between states.
local PHASES = {
    {
        name = "SEED",
        minTime = 7.0,
        maxTime = 15.0,
        topology = 0.10,
        radial = 0.12,
        hinge = 0.08,
        axisRate = 0.010,
        impact = 0.16,
        lowPush = 0.10,
        midPush = 0.08,
        highPush = 0.10
    },
    {
        name = "ALIGN",
        minTime = 9.0,
        maxTime = 19.0,
        topology = 0.34,
        radial = 0.32,
        hinge = 0.25,
        axisRate = 0.018,
        impact = 0.30,
        lowPush = 0.18,
        midPush = 0.18,
        highPush = 0.16
    },
    {
        name = "ORBIT",
        minTime = 11.0,
        maxTime = 23.0,
        topology = 0.57,
        radial = 0.50,
        hinge = 0.40,
        axisRate = 0.033,
        impact = 0.46,
        lowPush = 0.24,
        midPush = 0.26,
        highPush = 0.22
    },
    {
        name = "FRACTURE",
        minTime = 6.0,
        maxTime = 13.0,
        topology = 0.86,
        radial = 0.80,
        hinge = 0.82,
        axisRate = 0.058,
        impact = 0.86,
        lowPush = 0.32,
        midPush = 0.38,
        highPush = 0.46
    },
    {
        name = "AFTERSHOCK",
        minTime = 8.0,
        maxTime = 18.0,
        topology = 0.66,
        radial = 0.42,
        hinge = 0.58,
        axisRate = 0.021,
        impact = 0.54,
        lowPush = 0.20,
        midPush = 0.22,
        highPush = 0.30
    }
}

local LOW_ALPHA = 0.0253
local HIGH_ALPHA = 0.2690


local function clamp01(x)

    x = tonumber(x) or 0.0

    if x < 0.0 then
        return 0.0
    end

    if x > 1.0 then
        return 1.0
    end

    return x
end


local function getv(name, fallback)

    local v = _G[name]

    if type(v) == "number" then
        return v
    end

    if type(v) == "function" then

        local ok, result =
            pcall(v)

        if
            ok and
            type(result) == "number"
        then
            return result
        end
    end

    return fallback
end


local function trig_now()

    if type(trig) == "boolean" then
        return trig
    end

    return
        (tonumber(trig) or 0) > 0
end


local function compress_level(rms, gain)

    return clamp01(
        1.0 -
        math.exp(
            -math.max(
                0.0,
                rms
            ) *
            gain
        )
    )
end


local function smooth_value(
    current,
    target,
    attack,
    release
)

    local amount =
        target > current and
        attack or
        release

    return
        current +
        (
            target -
            current
        ) *
        amount
end


-- Linear attack/release slew. Rates are normalized units per second.
local function slew_value(
    current,
    target,
    riseRate,
    fallRate,
    dt
)

    local delta = target - current
    local rate = delta >= 0.0 and riseRate or fallRate
    local limit = math.max(0.0, rate) * dt

    if math.abs(delta) <= limit then
        return target
    end

    if delta > 0.0 then
        return current + limit
    end

    return current - limit
end


-- Hysteresis avoids a threshold chattering when audio sits near its edge.
local function schmitt(
    gate,
    value,
    onLevel,
    offLevel
)

    local rose = false
    local fell = false

    if
        not gate.high and
        value >= onLevel
    then
        gate.high = true
        rose = true
    elseif
        gate.high and
        value <= offLevel
    then
        gate.high = false
        fell = true
    end

    return gate.high, rose, fell
end


-- Small deterministic random source for event-driven sample-and-hold.
-- It does not disturb Lua's global random state or require os.time().
local function random01()

    randomState =
        (randomState * 16807) %
        2147483647

    return
        randomState /
        2147483647
end


local function bipolar_random()

    return random01() * 2.0 - 1.0
end


local function choose_phase_duration(index)

    local phase = PHASES[index]

    return
        phase.minTime +
        random01() *
        (phase.maxTime - phase.minTime)
end


local function enter_phase(index)

    phaseIndex = index

    if phaseIndex > #PHASES then
        phaseIndex = 1
    end

    phaseTime = 0.0
    phaseFade = 0.0
    phaseDuration =
        choose_phase_duration(
            phaseIndex
        )

    -- A phase boundary is also a patch-change event. New held voltages make
    -- repeated cycles related, but not identical.
    holdA = bipolar_random()
    holdB = bipolar_random()
    impactEnvelope =
        math.max(
            impactEnvelope,
            0.38
        )

    print(
        "VECTOR IMPACT phase " ..
        PHASES[phaseIndex].name
    )
end


local function reset_modular_state()

    controlTopology = PHASES[1].topology
    controlRadial = PHASES[1].radial
    controlHinge = PHASES[1].hinge
    controlImpact = PHASES[1].impact
    axisPhase = 0.0
    slowPhase = 0.0

    impactEnvelope = 0.0
    eventCharge = 0.0
    eventCooldown = 0.0
    holdA = 0.0
    holdB = 0.0
    trigWasHigh = false

    energyGate.high = false
    lowGate.high = false
    highGate.high = false

    phaseIndex = 1
    phaseTime = 0.0
    phaseFade = 0.0
    randomState = 17357
    phaseDuration =
        choose_phase_duration(1)
end


local function update_modular_control(
    dt,
    low,
    mid,
    high,
    energy,
    triggerSignal,
    k1,
    k2,
    k3,
    k4,
    k5
)

    -- Curve K5 toward useful response earlier in its travel. At noon this is
    -- already about 73% open; the last quarter now adds violence rather than
    -- unlocking basic audio reactivity.
    local response =
        k5 ^ 0.45

    local sensitivity =
        0.95 +
        response * 1.35

    local energyOn =
        0.47 -
        response * 0.17

    local lowOn =
        0.50 -
        response * 0.16

    local highOn =
        0.43 -
        response * 0.15

    local _, energyRose =
        schmitt(
            energyGate,
            energy,
            energyOn,
            energyOn - 0.11
        )

    local _, lowRose =
        schmitt(
            lowGate,
            low,
            lowOn,
            lowOn - 0.12
        )

    local _, highRose =
        schmitt(
            highGate,
            high,
            highOn,
            highOn - 0.10
        )

    local triggerRose =
        triggerSignal and
        not trigWasHigh

    trigWasHigh = triggerSignal

    eventCooldown =
        math.max(
            0.0,
            eventCooldown - dt
        )

    -- A leaky accumulator converts sustained activity into discrete events.
    -- Quiet material therefore evolves slowly; dense material charges it.
    local chargeInput =
        math.max(
            0.0,
            energy -
            (0.18 + (1.0 - response) * 0.06)
        )

    eventCharge =
        eventCharge +
        chargeInput *
        dt *
        (2.8 + sensitivity * 2.0) -
        dt *
        (0.075 + (1.0 - energy) * 0.055)

    eventCharge =
        math.max(
            0.0,
            math.min(
                1.25,
                eventCharge
            )
        )

    local accumulatedEvent =
        eventCharge >=
        (0.78 - response * 0.18)

    local eventPulse = false

    if
        eventCooldown <= 0.0 and
        (
            triggerRose or
            highRose or
            accumulatedEvent or
            (energyRose and mid > 0.18)
        )
    then
        eventPulse = true
        eventCooldown =
            0.68 -
            response * 0.38

        if accumulatedEvent then
            eventCharge =
                eventCharge * 0.22
        end

        holdA = bipolar_random()
        holdB = bipolar_random()

        impactEnvelope =
            math.max(
                impactEnvelope,
                0.58 +
                high * 0.55 +
                energy * 0.25
            )
    elseif lowRose then
        -- Low gates make weight without firing the sharper native trigger.
        impactEnvelope =
            math.max(
                impactEnvelope,
                0.32 + low * 0.45
            )
    end

    impactEnvelope =
        slew_value(
            impactEnvelope,
            0.0,
            4.0,
            0.22 + (1.0 - response) * 0.14,
            dt
        )

    phaseTime =
        phaseTime +
        dt *
        (
            0.82 +
            response * 0.30 +
            energy * 0.26
        )

    phaseFade =
        slew_value(
            phaseFade,
            1.0,
            0.32,
            0.32,
            dt
        )

    local phase = PHASES[phaseIndex]

    if
        phaseTime >= phaseDuration or
        (
            phaseTime >= phase.minTime and
            eventPulse
        )
    then
        enter_phase(
            phaseIndex + 1
        )

        phase = PHASES[phaseIndex]
    end

    slowPhase =
        slowPhase +
        dt *
        (
            0.16 +
            response * 0.14 +
            energy * 0.09
        )

    local slowA =
        math.sin(
            slowPhase
        )

    local slowB =
        math.sin(
            slowPhase * 0.617 +
            1.7
        )

    local depth =
        0.55 +
        response * 0.75

    local reactiveGain =
        0.85 +
        response * 1.35

    local evolutionFade =
        0.35 +
        phaseFade * 0.65

    local topologyTarget =
        clamp01(
            phase.topology +
            (k1 - 0.5) * 0.70 +
            slowA * (0.025 + depth * 0.040) * evolutionFade +
            holdA * depth * 0.065 * evolutionFade
        )

    local radialTarget =
        clamp01(
            phase.radial +
            (k2 - 0.5) * 0.68 +
            low * phase.lowPush * depth * reactiveGain +
            impactEnvelope * 0.18 +
            slowB * 0.045 * evolutionFade
        )

    local hingeTarget =
        clamp01(
            phase.hinge +
            (k3 - 0.5) * 0.68 +
            mid * phase.midPush * depth * reactiveGain +
            impactEnvelope * 0.15 +
            holdB * depth * 0.075 * evolutionFade
        )

    local impactTarget =
        clamp01(
            phase.impact +
            (k5 - 0.5) * 0.40 +
            high * phase.highPush * depth * reactiveGain +
            impactEnvelope * 0.44
        )

    -- Four independent function generators. Impact rises fast and falls
    -- slowly; topology moves like a heavy mechanism.
    controlTopology =
        slew_value(
            controlTopology,
            topologyTarget,
            0.095,
            0.060,
            dt
        )

    controlRadial =
        slew_value(
            controlRadial,
            radialTarget,
            0.42 + response * 0.30,
            0.16 + response * 0.09,
            dt
        )

    controlHinge =
        slew_value(
            controlHinge,
            hingeTarget,
            0.50 + response * 0.40,
            0.18 + response * 0.10,
            dt
        )

    controlImpact =
        slew_value(
            controlImpact,
            impactTarget,
            1.20 + response * 1.00,
            0.28 + response * 0.12,
            dt
        )

    axisPhase =
        (
            axisPhase +
            dt *
            (
                phase.axisRate *
                (0.58 + response * 1.12) +
                mid * (0.025 + response * 0.025) +
                impactEnvelope * 0.035
            )
        ) % 1.0

    local controlAxis =
        (
            k4 +
            axisPhase +
            slowA * (0.035 + response * 0.060) * evolutionFade +
            holdB * depth * 0.035 * evolutionFade
        ) % 1.0

    -- Phase-specific gain staging alters how the unchanged native generator
    -- hears the three classified bands.
    local audioDrive =
        0.80 +
        response * 1.40

    local outLow =
        clamp01(
            low *
            audioDrive *
            (0.72 + phase.lowPush)
        )

    local outMid =
        clamp01(
            mid *
            audioDrive *
            (0.72 + phase.midPush)
        )

    local outHigh =
        clamp01(
            high *
            audioDrive *
            (0.68 + phase.highPush) +
            impactEnvelope * 0.18
        )

    local outEnergy =
        clamp01(
            energy *
            (0.88 + response * 0.75) +
            impactEnvelope * 0.12
        )

    return
        outLow,
        outMid,
        outHigh,
        outEnergy,
        eventPulse,
        controlTopology,
        controlRadial,
        controlHinge,
        controlAxis,
        controlImpact
end


local function compute_audio_bands()

    local lowPower = 0.0
    local midPower = 0.0
    local highPower = 0.0

    local sampleCount =
        math.max(
            #inL,
            #inR
        )

    if sampleCount < 1 then
        sampleCount = 100
    end

    if sampleCount > 256 then
        sampleCount = 256
    end


    for i = 1, sampleCount do

        local left =
            tonumber(inL[i]) or
            0.0

        local right =
            tonumber(inR[i]) or
            left

        local sample =
            (
                left +
                right
            ) *
            0.5


        lowPassState =
            lowPassState +
            LOW_ALPHA *
            (
                sample -
                lowPassState
            )


        highPassBaseState =
            highPassBaseState +
            HIGH_ALPHA *
            (
                sample -
                highPassBaseState
            )


        local low =
            lowPassState


        local mid =
            highPassBaseState -
            lowPassState


        local high =
            sample -
            highPassBaseState


        lowPower =
            lowPower +
            low *
            low


        midPower =
            midPower +
            mid *
            mid


        highPower =
            highPower +
            high *
            high
    end


    local lowRms =
        math.sqrt(
            lowPower /
            sampleCount
        )


    local midRms =
        math.sqrt(
            midPower /
            sampleCount
        )


    local highRms =
        math.sqrt(
            highPower /
            sampleCount
        )


    local low =
        compress_level(
            lowRms,
            8.2
        )


    local mid =
        compress_level(
            midRms,
            5.6
        )


    local high =
        compress_level(
            highRms,
            3.8
        )


    smoothLow =
        smooth_value(
            smoothLow,
            low,
            0.42,
            0.13
        )


    smoothMid =
        smooth_value(
            smoothMid,
            mid,
            0.48,
            0.16
        )


    smoothHigh =
        smooth_value(
            smoothHigh,
            high,
            0.56,
            0.21
        )


    local spectralEnergy =
        clamp01(
            smoothLow *
            0.43 +

            smoothMid *
            0.37 +

            smoothHigh *
            0.20
        )


    local enginePeak =
        clamp01(
            getv(
                "peak",
                getv(
                    "audio_peak",
                    0.0
                )
            ) *
            1.10
        )


    local energy =
        math.max(
            spectralEnergy,
            enginePeak *
            0.82
        )


    smoothEnergy =
        smooth_value(
            smoothEnergy,
            energy,
            0.52,
            0.11
        )


    return
        smoothLow,
        smoothMid,
        smoothHigh,
        smoothEnergy
end


local function clear_full_frame()

    local W =
        renderW or
        1280

    local H =
        renderH or
        720


    -- Explicitly own the outer EYESY render canvas.
    --
    -- This means that even if VectorImpact fails to present a frame for one
    -- draw cycle, the outgoing mode cannot remain visible.

    of.setColor(
        0,
        0,
        0,
        255
    )

    of.drawRectangle(
        0,
        0,
        W,
        H
    )


    -- CRITICAL:
    -- Restore white before the native FBO texture is blitted.
    --
    -- ofFbo::draw() is affected by current OF draw color. A dirty/tinted
    -- color inherited from another mode can otherwise suppress the native
    -- framebuffer.

    of.setColor(
        255,
        255,
        255,
        255
    )
end


local function clear_error_screen()

    local W =
        renderW or
        1280

    local H =
        renderH or
        720


    of.setColor(
        48,
        0,
        8,
        255
    )

    of.drawRectangle(
        0,
        0,
        W,
        H
    )
end


local overscanAvailable = true
local overscanWarningPrinted = false


local function draw_vectorimpact_overscan(
    W,
    H,
    k2
)

    -- The upper K2 range becomes a centered post-render zoom. This scales the
    -- existing native FBO instead of requesting a larger one, so nearly 2x
    -- overscan costs no additional framebuffer memory.
    local zoomControl =
        clamp01(
            (k2 - 0.42) /
            0.58
        )

    local zoom =
        1.0 +
        (zoomControl ^ 1.35) *
        0.95

    if zoom <= 1.001 then
        vectorimpact_draw(
            W,
            H
        )

        return
    end

    overscanAvailable =
        overscanAvailable and
        type(of.pushMatrix) == "function" and
        type(of.popMatrix) == "function" and
        type(of.translate) == "function" and
        type(of.scale) == "function"

    if not overscanAvailable then
        if not overscanWarningPrinted then
            print(
                "VECTOR IMPACT overscan unavailable; using native scale"
            )

            overscanWarningPrinted = true
        end

        vectorimpact_draw(
            W,
            H
        )

        return
    end

    of.pushMatrix()

    of.translate(
        W * 0.5,
        H * 0.5,
        0.0
    )

    of.scale(
        zoom,
        zoom,
        1.0
    )

    of.translate(
        W * -0.5,
        H * -0.5,
        0.0
    )

    vectorimpact_draw(
        W,
        H
    )

    of.popMatrix()
end


function setup()

    print(
        "HAUTLLE_VECTOR_IMPACT_02_1 setup"
    )


    generatorAvailable =
        type(vectorimpact_prepare) ==
            "function" and

        type(vectorimpact_step) ==
            "function" and

        type(vectorimpact_draw) ==
            "function"


    if not generatorAvailable then

        print(
            "HAUTLLE_VECTOR_IMPACT_02_1 ERROR: native vectorimpact_* API missing"
        )

        return
    end


    -- Explicitly quarantine ShaderHost's private feedback state.

    if
        type(shaderhost_feedback_enable) ==
        "function"
    then

        shaderhost_feedback_enable(
            false
        )
    end


    -- First ensure the native renderer/FBO exists.

    vectorimpact_prepare()


    -- Then force the persistent generator back to a known-clean state.
    --
    -- VectorImpactGenerator intentionally survives Lua reloads, so merely
    -- calling prepare() can leave us with state from the previous visit.

    if
        RESET_ON_SETUP and
        type(vectorimpact_reset) ==
        "function"
    then

        vectorimpact_reset()

        print(
            "HAUTLLE_VECTOR_IMPACT_02_1 native state reset"
        )
    end


    lowPassState =
        0.0

    highPassBaseState =
        0.0

    smoothLow =
        0.0

    smoothMid =
        0.0

    smoothHigh =
        0.0

    smoothEnergy =
        0.0

    reset_modular_state()


    initialized =
        true


    print(
        "HAUTLLE_VECTOR_IMPACT_02_1 native vector renderer ready"
    )

    print(
        "HAUTLLE_VECTOR_IMPACT_02_1 modular phase system ready: SEED"
    )
end


function update()
end


function draw()

    if
        not generatorAvailable or
        not initialized
    then

        clear_error_screen()

        return
    end


    local W =
        renderW or
        1280

    local H =
        renderH or
        720


    -- ================================================================
    -- HARD MODE-LOCAL FRAME OWNERSHIP
    -- ================================================================
    --
    -- Do this BEFORE any native rendering.
    --
    -- This removes the outgoing mode's image from the normal engine
    -- render canvas regardless of normal Persist state.

    clear_full_frame()


    local dt =
        1.0 /
        30.0


    local low,
          mid,
          high,
          energy =
        compute_audio_bands()


    local k1 =
        clamp01(
            getv(
                "knob1",
                0.50
            )
        )


    local k2 =
        clamp01(
            getv(
                "knob2",
                0.44
            )
        )


    local k3 =
        clamp01(
            getv(
                "knob3",
                0.52
            )
        )


    local k4 =
        clamp01(
            getv(
                "knob4",
                0.50
            )
        )


    local k5 =
        clamp01(
            getv(
                "knob5",
                0.64
            )
        )


    local triggerSignal =
        trig_now()


    local outLow,
          outMid,
          outHigh,
          outEnergy,
          eventPulse,
          outK1,
          outK2,
          outK3,
          outK4,
          outK5 =
        update_modular_control(
            dt,
            low,
            mid,
            high,
            energy,
            triggerSignal,
            k1,
            k2,
            k3,
            k4,
            k5
        )


    vectorimpact_step(
        dt,
        outLow,
        outMid,
        outHigh,
        outEnergy,
        eventPulse,
        outK1,
        outK2,
        outK3,
        outK4,
        outK5
    )


    -- Restore white AGAIN immediately before the native texture/FBO blit.
    --
    -- Cheap insurance against any native geometry call or Lua drawing
    -- contaminating the global OF color state.

    of.setColor(
        255,
        255,
        255,
        255
    )


    draw_vectorimpact_overscan(
        W,
        H,
        k2
    )
end


function exit()

    if
        type(shaderhost_feedback_enable) ==
        "function"
    then

        shaderhost_feedback_enable(
            false
        )
    end


    -- Leave OF draw color in a sane state for the next mode.

    of.setColor(
        255,
        255,
        255,
        255
    )


    print(
        "HAUTLLE_VECTOR_IMPACT_02_1 exit"
    )

    print(
        "script finished"
    )
end
