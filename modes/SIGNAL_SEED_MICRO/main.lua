---- HAUTLLE SIGNAL SEED MICRO v2.0
---- "GERMINATION"
----
---- Lightweight ShaderHost signal ecology.
----
---- Small artificial signal-organisms emerge, mutate, disappear,
---- and contaminate the feedback field.
----
---- Shader remains deliberately inexpensive:
----
----   - ONE previous-frame texture read
----   - NO shader loops
----   - simple hashes / masks
----   - lightweight CPU audio analysis
----
---- Controls:
----
----   K1 = Population
----   K2 = Motion
----   K3 = Memory
----   K4 = Color
----   K5 = Burn
----
---- Audio:
----
----   LEVEL     = body movement / swelling / displacement
----   ROUGHNESS = internal instability / fine agitation
----   TRIG      = short violent fault / rupture event
----
---- TRIG has NO cooldown.
---- Every new trigger edge can re-strike the disturbance.
----
---- Feedback:
----
----   shaderhost_feedback_enable(true)
----       = mode opts into u_prevFrame
----
----   Shift + Persist
----       = hardware ShaderHost feedback gate
----
----   Persist
----       = regular EYESY canvas persistence
----

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle = "Signal Seed Micro v2"
modeExplain = "germinating signal organisms / damaged carrier field"

titleK1 = "Population"
titleK2 = "Motion"
titleK3 = "Memory"
titleK4 = "Color"
titleK5 = "Burn"

print(modeTitle)


---------------------------------------------------------------------
-- SHADER
---------------------------------------------------------------------

local fragShader = [[
#ifdef GL_ES
precision mediump float;
precision lowp sampler2D;
#endif

uniform vec2  u_resolution;
uniform float u_time;

uniform float u_energy;
uniform float u_brightness;
uniform float u_trig;

uniform float u_k1;
uniform float u_k2;
uniform float u_k3;
uniform float u_k4;
uniform float u_k5;

uniform sampler2D u_prevFrame;
uniform float u_feedbackEnabled;
uniform float u_feedbackReady;


/* --------------------------------------------------------------- */
/* HASH                                                             */
/* --------------------------------------------------------------- */

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}


/* --------------------------------------------------------------- */
/* BASIC MASKS                                                      */
/* --------------------------------------------------------------- */

float rectMask(vec2 p, vec2 halfSize, float soft)
{
    vec2 d = abs(p) - halfSize;
    float m = max(d.x, d.y);

    return 1.0 - smoothstep(0.0, soft, m);
}


float circleMask(vec2 p, float radius, float soft)
{
    return 1.0 - smoothstep(
        radius,
        radius + soft,
        length(p)
    );
}


float line1(float x, float c, float w, float soft)
{
    return 1.0 - smoothstep(
        w,
        w + soft,
        abs(x - c)
    );
}


/* --------------------------------------------------------------- */
/* FEEDBACK                                                         */
/* --------------------------------------------------------------- */

vec3 readPrev(vec2 p)
{
    p = clamp(
        p,
        vec2(0.001),
        vec2(0.999)
    );

    p.y = 1.0 - p.y;

    return texture2D(
        u_prevFrame,
        p
    ).rgb;
}


/* --------------------------------------------------------------- */
/* MAIN                                                             */
/* --------------------------------------------------------------- */

void main()
{
    vec2 uv =
        gl_FragCoord.xy /
        u_resolution.xy;

    float energy =
        clamp(
            u_energy,
            0.0,
            1.0
        );

    /*
        u_brightness is intentionally used by the Lua side
        as our secondary AUDIO ROUGHNESS measurement.
    */

    float rough =
        clamp(
            u_brightness,
            0.0,
            1.0
        );

    float hit =
        clamp(
            u_trig,
            0.0,
            1.0
        );


    float activity =
        clamp(
            energy * 0.78 +
            rough  * 0.40 +
            hit    * 0.35,
            0.0,
            1.0
        );


    /* ----------------------------------------------------------- */
    /* TIME                                                        */
    /* ----------------------------------------------------------- */

    float t =
        u_time *
        mix(
            0.10,
            0.72,
            u_k2
        );


    /* ----------------------------------------------------------- */
    /* HORIZONTAL FAULT FIELD                                      */
    /* ----------------------------------------------------------- */

    float faultRows =
        mix(
            7.0,
            15.0,
            u_k1
        );

    float faultRowID =
        floor(
            uv.y *
            faultRows
        );

    float faultEpoch =
        floor(
            t *
            0.18
        );

    float faultRandom =
        hash21(
            vec2(
                faultRowID * 0.71,
                faultEpoch + 31.7
            )
        );

    float faultChance =
        clamp(
            hit +
            rough * 0.30,
            0.0,
            1.0
        );

    float faultGate =
        step(
            mix(
                0.970,
                0.845,
                faultChance
            ),
            faultRandom
        );

    float faultLocal =
        fract(
            uv.y *
            faultRows
        );

    float faultShape =
        1.0 -
        smoothstep(
            0.18,
            0.48,
            abs(
                faultLocal -
                0.5
            )
        );

    float fault =
        faultGate *
        faultShape;


    /* ----------------------------------------------------------- */
    /* FEEDBACK MEMORY                                             */
    /* ----------------------------------------------------------- */

    vec3 memory =
        vec3(0.0);

    if (
        u_feedbackEnabled > 0.5 &&
        u_feedbackReady > 0.5
    )
    {
        vec2 q =
            uv - 0.5;

        float feedbackMotion =
            mix(
                0.0005,
                0.0080,
                u_k3
            );

        float angle =
            sin(
                t * 0.17
            ) *
            feedbackMotion *
            mix(
                0.15,
                1.0,
                u_k2
            );

        angle +=
            hit *
            sin(
                uv.y * 19.0 +
                t * 2.0
            ) *
            0.0055 *
            u_k3;

        float ca =
            cos(angle);

        float sa =
            sin(angle);

        q =
            mat2(
                ca, -sa,
                sa,  ca
            ) * q;

        float breathe =
            1.0 -
            (
                sin(
                    t * 0.11
                ) *
                0.0014 +

                energy *
                0.0018
            ) *
            u_k3;

        q *= breathe;

        vec2 mUv =
            q + 0.5;

        float warp =
            mix(
                0.00015,
                0.0065,
                u_k3
            );

        warp *=
            mix(
                0.45,
                1.45,
                activity
            );

        mUv.x +=
            sin(
                uv.y * 5.3 +
                t * 0.47
            ) *
            warp;

        mUv.y +=
            cos(
                uv.x * 4.1 -
                t * 0.31
            ) *
            warp *
            0.72;

        mUv.x +=
            fault *
            sin(
                faultRowID * 2.17 +
                t * 1.4
            ) *
            (
                0.008 +
                0.045 * hit
            ) *
            u_k3;

        memory =
            readPrev(
                mUv
            );
    }


    float memoryGain =
        mix(
            0.08,
            0.86,
            u_k3
        );

    memoryGain *=
        mix(
            0.995,
            0.900,
            u_k5
        );

    vec3 color =
        memory *
        memoryGain;


    /* ----------------------------------------------------------- */
    /* SEED DOMAIN                                                 */
    /* ----------------------------------------------------------- */

    vec2 seedUv =
        uv;

    seedUv.x +=
        fault *
        sin(
            faultRowID * 1.83 +
            t * 1.7
        ) *
        (
            0.008 +
            u_k2 * 0.050 +
            hit * 0.055
        );

    seedUv.x =
        fract(
            seedUv.x
        );


    /* ----------------------------------------------------------- */
    /* CELL GRID                                                   */
    /* ----------------------------------------------------------- */

    vec2 grid =
        vec2(
            mix(
                5.0,
                20.0,
                u_k1
            ),

            mix(
                3.0,
                11.0,
                u_k1
            )
        );

    vec2 cell =
        floor(
            seedUv *
            grid
        );

    vec2 local =
        fract(
            seedUv *
            grid
        );


    float baseHash =
        hash21(
            cell +
            vec2(
                7.13,
                19.31
            )
        );


    float life =
        floor(
            t * 0.105 +
            baseHash * 3.0
        );


    float r0 =
        hash21(
            cell +
            vec2(
                1.13 +
                life * 1.37,

                7.31
            )
        );

    float r1 =
        hash21(
            cell +
            vec2(
                4.71,

                2.93 +
                life * 0.91
            )
        );

    float r2 =
        hash21(
            cell +
            vec2(
                11.17 +
                life * 0.43,

                5.19
            )
        );


    /* ----------------------------------------------------------- */
    /* POPULATION                                                  */
    /* ----------------------------------------------------------- */

    float active =
        step(
            mix(
                0.970,
                0.590,
                u_k1
            ),
            r0
        );


    /* ----------------------------------------------------------- */
    /* SEED GEOMETRY                                               */
    /* ----------------------------------------------------------- */

    vec2 p =
        local - 0.5;


    float vertical =
        step(
            0.52,
            r1
        );

    p =
        mix(
            p,
            p.yx,
            vertical
        );


    float driftAmount =
        mix(
            0.003,
            0.060,
            u_k2
        );

    driftAmount *=
        mix(
            0.55,
            1.35,
            activity
        );


    vec2 drift =
        vec2(
            sin(
                t *
                (
                    0.31 +
                    r0 * 0.23
                ) +
                r0 * 6.2831
            ),

            cos(
                t *
                (
                    0.24 +
                    r1 * 0.19
                ) +
                r1 * 6.2831
            )
        );

    p -=
        drift *
        driftAmount;


    p.x +=
        sin(
            p.y * 16.0 +
            r0 * 9.0 +
            t * 2.1
        ) *
        hit *
        mix(
            0.015,
            0.145,
            u_k2
        );

    p.y +=
        cos(
            p.x * 13.0 -
            r1 * 8.0 +
            t * 1.7
        ) *
        hit *
        mix(
            0.006,
            0.060,
            u_k2
        );


    /* ----------------------------------------------------------- */
    /* BODY SIZE                                                   */
    /* ----------------------------------------------------------- */

    vec2 halfSize =
        vec2(
            mix(
                0.075,
                0.210,
                u_k1
            ) *
            mix(
                0.72,
                1.22,
                r0
            ),

            mix(
                0.022,
                0.070,
                u_k1
            ) *
            mix(
                0.70,
                1.30,
                r1
            )
        );


    float seedPulse =
        0.84 +
        0.16 *
        sin(
            t *
            (
                0.75 +
                r2 * 0.8
            ) +
            r0 * 6.2831
        );

    seedPulse +=
        energy *
        0.13;

    halfSize *=
        seedPulse;


    /* ----------------------------------------------------------- */
    /* SHELL / FRAME                                               */
    /* ----------------------------------------------------------- */

    float body =
        rectMask(
            p,
            halfSize,
            0.030
        );

    float inner =
        rectMask(
            p,
            halfSize *
            vec2(
                0.46,
                0.42
            ),
            0.025
        );

    float frame =
        max(
            body -
            inner,
            0.0
        );


    /* ----------------------------------------------------------- */
    /* INTERNAL NUCLEUS                                            */
    /* ----------------------------------------------------------- */

    vec2 coreOffset =
        vec2(
            mix(
                -0.070,
                 0.070,
                 r2
            ),
            0.0
        );

    float core =
        circleMask(
            p -
            coreOffset,

            mix(
                0.014,
                0.045,
                u_k1
            ),

            0.020
        );


    float slitPosition =
        sin(
            t * 0.9 +
            r1 * 8.0
        ) *
        0.020 *
        u_k2;

    float slit =
        line1(
            p.y,
            slitPosition,

            0.004 +
            rough * 0.010,

            0.012
        );

    slit *=
        body;


    /* ----------------------------------------------------------- */
    /* RARE HALO                                                   */
    /* ----------------------------------------------------------- */

    float rare =
        step(
            0.925,
            r2
        );

    float haloOuter =
        circleMask(
            p,

            mix(
                0.10,
                0.19,
                r0
            ),

            0.025
        );

    float haloInner =
        circleMask(
            p,

            mix(
                0.065,
                0.14,
                r0
            ),

            0.022
        );

    float halo =
        max(
            haloOuter -
            haloInner,
            0.0
        );

    halo *=
        rare;


    /* ----------------------------------------------------------- */
    /* BUILD SEED                                                  */
    /* ----------------------------------------------------------- */

    float seed =
        frame * 0.95 +
        body  * 0.13 +
        core  * 0.90 +
        slit  * 0.32 +
        halo  * 0.52;

    seed *=
        active;


    float internalPulse =
        0.58 +
        0.42 *
        sin(
            local.x * 11.0 +
            local.y * 5.0 +
            t * 1.38 +
            r0 * 6.2831
        );

    internalPulse =
        mix(
            internalPulse,
            1.0,
            energy * 0.45
        );


    /* ----------------------------------------------------------- */
    /* CARRIER FIELD                                               */
    /* ----------------------------------------------------------- */

    float carrierY =
        0.5 +
        sin(
            t * 0.29 +
            u_k4 * 6.2831
        ) *
        mix(
            0.12,
            0.43,
            u_k2
        );

    carrierY +=
        sin(
            t * 0.81
        ) *
        energy *
        0.035 *
        u_k2;

    float hBand =
        line1(
            uv.y,
            carrierY,

            mix(
                0.003,
                0.020,
                u_k1
            ),

            0.012
        );


    float carrierX =
        0.5 +
        cos(
            t * 0.21 +
            u_k4 * 4.1
        ) *
        mix(
            0.10,
            0.40,
            u_k2
        );

    carrierX +=
        sin(
            t * 0.57
        ) *
        rough *
        0.025 *
        u_k2;

    float vBand =
        line1(
            uv.x,
            carrierX,

            mix(
                0.002,
                0.014,
                u_k1
            ),

            0.010
        );


    /* ----------------------------------------------------------- */
    /* MOVING PACKET ROW                                           */
    /* ----------------------------------------------------------- */

    float packetY =
        fract(
            0.17 +
            t * 0.043
        );

    float packetRow =
        line1(
            uv.y,
            packetY,
            0.0025,
            0.006
        );

    float packetPhase =
        fract(
            uv.x *
            mix(
                12.0,
                42.0,
                u_k1
            ) +
            floor(
                t * 1.7
            ) *
            0.17
        );

    float packetMask =
        step(
            0.57,
            packetPhase
        );

    float packets =
        packetRow *
        packetMask;


    /* ----------------------------------------------------------- */
    /* SCAN RESIDUE                                                */
    /* ----------------------------------------------------------- */

    float scan =
        step(
            0.991,

            fract(
                uv.y *
                mix(
                    58.0,
                    170.0,
                    u_k5
                ) +
                t * 0.19
            )
        );


    /* ----------------------------------------------------------- */
    /* FAULT SPARKS                                                */
    /* ----------------------------------------------------------- */

    float faultNoise =
        fract(
            uv.x * 71.0 +
            faultRowID * 0.37 +
            t * 3.1
        );

    float faultSpark =
        fault *
        step(
            0.94,
            faultNoise
        );


    /* ----------------------------------------------------------- */
    /* SIGNAL MIX                                                  */
    /* ----------------------------------------------------------- */

    float seedSignal =
        seed *
        internalPulse *
        mix(
            0.38,
            1.00,
            u_k1
        );


    seedSignal +=
        energy *
        seed *
        mix(
            0.10,
            0.42,
            u_k2
        );


    seedSignal +=
        rough *
        (
            slit * 0.45 +
            frame * 0.12
        );


    seedSignal +=
        hit *
        max(
            core,
            max(
                frame,
                halo
            )
        ) *
        mix(
            0.22,
            0.85,
            u_k2
        );


    float carrierSignal =
        0.0;

    carrierSignal +=
        hBand *
        mix(
            0.12,
            0.50,
            u_k5
        );

    carrierSignal +=
        vBand *
        mix(
            0.08,
            0.34,
            u_k5
        );

    carrierSignal +=
        packets *
        mix(
            0.08,
            0.31,
            u_k1
        );

    carrierSignal +=
        scan *
        mix(
            0.006,
            0.040,
            u_k5
        );

    carrierSignal +=
        faultSpark *
        (
            0.18 +
            hit * 0.62
        );


    seedSignal *=
        1.0 -
        fault *
        mix(
            0.24,
            0.73,
            u_k2
        );


    float faultEdge =
        faultGate *
        (
            line1(
                faultLocal,
                0.20,
                0.012,
                0.025
            ) +

            line1(
                faultLocal,
                0.80,
                0.012,
                0.025
            )
        );

    carrierSignal +=
        faultEdge *
        (
            0.08 +
            hit * 0.42
        );


    seedSignal =
        clamp(
            seedSignal,
            0.0,
            1.0
        );

    carrierSignal =
        clamp(
            carrierSignal,
            0.0,
            1.0
        );

    float signal =
        clamp(
            seedSignal +
            carrierSignal,
            0.0,
            1.0
        );


    /* ----------------------------------------------------------- */
    /* COLOR                                                       */
    /* ----------------------------------------------------------- */

    vec3 deepBlue =
        vec3(
            0.015,
            0.095,
            0.300
        );

    vec3 electricBlue =
        vec3(
            0.025,
            0.260,
            0.500
        );

    vec3 violet =
        vec3(
            0.240,
            0.045,
            0.330
        );

    vec3 amber =
        vec3(
            0.760,
            0.245,
            0.025
        );

    vec3 red =
        vec3(
            0.690,
            0.025,
            0.012
        );

    vec3 bone =
        vec3(
            0.825,
            0.680,
            0.440
        );


    vec3 cold =
        mix(
            deepBlue,
            electricBlue,
            r0
        );

    cold =
        mix(
            cold,
            violet,
            r1 * 0.48
        );


    vec3 hot =
        mix(
            amber,
            red,
            r1
        );


    float warmBias =
        smoothstep(
            0.12,
            0.88,
            u_k4
        );

    vec3 seedColor =
        mix(
            cold,
            hot,
            warmBias
        );


    seedColor =
        mix(
            seedColor,
            violet,

            (1.0 - warmBias) *
            r2 *
            0.20
        );


    seedColor =
        mix(
            seedColor,
            bone,

            clamp(
                core *
                u_k5 *
                0.32 +

                hit *
                u_k5 *
                0.18,

                0.0,
                0.46
            )
        );


    vec3 carrierColor =
        mix(
            vec3(
                0.040,
                0.140,
                0.310
            ),

            amber,
            warmBias
        );

    carrierColor =
        mix(
            carrierColor,
            bone,
            u_k5 * 0.30
        );


    color +=
        seedColor *
        seedSignal;

    color +=
        carrierColor *
        carrierSignal;


    /* ----------------------------------------------------------- */
    /* BURN                                                        */
    /* ----------------------------------------------------------- */

    float hotCore =
        signal *
        signal *
        u_k5;

    color +=
        bone *
        hotCore *
        0.18;


    color =
        max(
            color -
            vec3(
                mix(
                    0.006,
                    0.022,
                    u_k5
                )
            ),

            vec3(0.0)
        );


    color =
        pow(
            max(
                color,
                vec3(0.0)
            ),

            vec3(
                mix(
                    1.18,
                    0.72,
                    u_k5
                )
            )
        );


    /* ----------------------------------------------------------- */
    /* EDGE FALLOFF                                                */
    /* ----------------------------------------------------------- */

    vec2 c =
        uv - 0.5;

    float vig =
        1.0 -
        dot(
            c,
            c
        ) *
        0.21;

    color *=
        clamp(
            vig,
            0.78,
            1.0
        );


    /* ----------------------------------------------------------- */
    /* ALPHA                                                       */
    /* ----------------------------------------------------------- */

    float memLum =
        dot(
            memory,
            vec3(
                0.299,
                0.587,
                0.114
            )
        );


    float alpha =
        mix(
            0.18,
            0.25,
            u_k5
        );

    alpha =
        max(
            alpha,

            signal *
            mix(
                0.56,
                0.94,
                u_k5
            )
        );

    alpha =
        max(
            alpha,

            memLum *
            mix(
                0.035,
                0.13,
                u_k3
            )
        );

    alpha =
        clamp(
            alpha,
            0.0,
            0.97
        );


    gl_FragColor =
        vec4(
            color,
            alpha
        );
}
]]


---------------------------------------------------------------------
-- HOST STATE
---------------------------------------------------------------------

local hostAvailable = false
local shaderLoaded = false

local frameTime = 0.0
local FRAME_DT = 0.033333


---------------------------------------------------------------------
-- AUDIO STATE
---------------------------------------------------------------------

local smoothEnergy = 0.0
local smoothRoughness = 0.0

local trigPressure = 0.0
local prevTrig = false


---------------------------------------------------------------------
-- HELPERS
---------------------------------------------------------------------

local function getv(name, fallback)

    local v = _G[name]

    if type(v) == "number" then
        return v
    end

    if type(v) == "function" then

        local ok, r =
            pcall(v)

        if ok and type(r) == "number" then
            return r
        end
    end

    return fallback
end


local function clamp01(x)

    x = tonumber(x) or 0

    if x < 0 then
        return 0
    end

    if x > 1 then
        return 1
    end

    return x
end


local function follow(current, target, attack, release)

    local speed

    if target > current then
        speed = attack
    else
        speed = release
    end

    return
        current +
        (target - current) *
        speed
end


local function has_shaderhost_api()

    return
        type(shaderhost_load) == "function"
        and
        type(shaderhost_step) == "function"
        and
        type(shaderhost_draw) == "function"
end


local function enable_alpha()

    if
        type(of) == "table"
        and
        type(of.enableAlphaBlending) == "function"
    then

        of.enableAlphaBlending()

    elseif
        type(ofEnableAlphaBlending) == "function"
    then

        ofEnableAlphaBlending()
    end
end


---------------------------------------------------------------------
-- AUDIO ANALYSIS
--
-- energy:
--     average absolute amplitude
--
-- roughness:
--     average sample-to-sample movement
--
-- This gives us a cheap distinction between sustained/smooth audio
-- and noisy/percussive/busy audio without doing an FFT.
---------------------------------------------------------------------

local function compute_audio()

    local samples =
        inL or {}

    local count =
        64

    local sum =
        0.0

    local diff =
        0.0

    local peak =
        0.0

    local previous =
        tonumber(
            samples[1]
        ) or 0.0


    for i = 1, count do

        local s =
            tonumber(
                samples[i]
            ) or 0.0

        local a =
            math.abs(s)

        sum =
            sum + a


        if a > peak then
            peak = a
        end


        if i > 1 then

            diff =
                diff +
                math.abs(
                    s -
                    previous
                )
        end


        previous =
            s
    end


    local energy =
        (
            sum /
            count
        ) *
        3.15


    local roughness =
        (
            diff /
            (count - 1)
        ) *
        4.25


    -- Peak gets only a small vote.
    -- This keeps smoother low-frequency material from leaving
    -- the internal circuitry completely static.

    roughness =
        roughness +
        peak * 0.08


    if energy > 1.0 then
        energy = 1.0
    end

    if roughness > 1.0 then
        roughness = 1.0
    end


    return
        energy,
        roughness
end


---------------------------------------------------------------------
-- SETUP
---------------------------------------------------------------------

function setup()

    print(
        "SIGNAL_SEED_MICRO_V2 setup"
    )


    hostAvailable =
        has_shaderhost_api()


    if not hostAvailable then

        print(
            "SIGNAL_SEED_MICRO_V2: shaderhost_* functions not found"
        )

        return
    end


    if type(shaderhost_reset) == "function" then
        shaderhost_reset()
    end


    shaderLoaded =
        shaderhost_load(
            fragShader
        )


    frameTime =
        0.0


    if shaderLoaded then

        print(
            "SIGNAL_SEED_MICRO_V2: shader loaded"
        )


        -- Mode opts into ShaderHost feedback.
        -- Shift + Persist remains the actual feedback gate.

        if type(shaderhost_feedback_enable) == "function" then

            shaderhost_feedback_enable(
                true
            )

            print(
                "SIGNAL_SEED_MICRO_V2: ShaderHost feedback opt-in enabled"
            )
        end

    else

        print(
            "SIGNAL_SEED_MICRO_V2: shader failed"
        )
    end


    smoothEnergy =
        0.0

    smoothRoughness =
        0.0

    trigPressure =
        0.0

    prevTrig =
        false
end


---------------------------------------------------------------------
-- UPDATE
---------------------------------------------------------------------

function update()
end


---------------------------------------------------------------------
-- DRAW
---------------------------------------------------------------------

function draw()

    enable_alpha()


    local W =
        renderW or 1280

    local H =
        renderH or 720


    ---------------------------------------------------------------
    -- KNOBS
    ---------------------------------------------------------------

    local k1 =
        clamp01(
            getv(
                "knob1",
                0.25
            )
        )

    local k2 =
        clamp01(
            getv(
                "knob2",
                0.32
            )
        )

    local k3 =
        clamp01(
            getv(
                "knob3",
                0.38
            )
        )

    local k4 =
        clamp01(
            getv(
                "knob4",
                0.20
            )
        )

    local k5 =
        clamp01(
            getv(
                "knob5",
                0.52
            )
        )


    ---------------------------------------------------------------
    -- REGULAR EYESY PERSIST
    --
    -- K5 low:
    --     extremely slow cleanup
    --
    -- K5 high:
    --     increasingly aggressive cleanup / burn
    ---------------------------------------------------------------

    local bgA


    if persist and not persistClear then

        local burnCurve =
            k5 *
            k5

        bgA =
            1 +
            math.floor(
                burnCurve *
                25
            )

    else

        bgA =
            255
    end


    of.setColor(
        0,
        0,
        0,
        bgA
    )

    of.drawRectangle(
        0,
        0,
        W,
        H
    )


    ---------------------------------------------------------------
    -- FAILURE DISPLAY
    ---------------------------------------------------------------

    if
        not hostAvailable
        or
        not shaderLoaded
    then

        of.setColor(
            255,
            0,
            255,
            255
        )

        of.drawRectangle(
            0,
            0,
            W,
            H
        )

        return
    end


    ---------------------------------------------------------------
    -- AUDIO
    ---------------------------------------------------------------

    local energy, roughness =
        compute_audio()


    -- Save old envelope before updating it.
    -- This becomes our cheap extra onset detector.

    local oldSmoothEnergy =
        smoothEnergy


    -- Faster attack, slower release.
    -- Loud sustained material should move the system without
    -- keeping everything permanently maxed out.

    smoothEnergy =
        follow(
            smoothEnergy,
            energy,
            0.18,
            0.045
        )


    smoothRoughness =
        follow(
            smoothRoughness,
            roughness,
            0.24,
            0.080
        )


    ---------------------------------------------------------------
    -- TRIGGER
    ---------------------------------------------------------------

    local trigNow =
        (trig == true)
        or
        (
            (tonumber(trig) or 0) > 0
        )


    local trigEdge =
        trigNow
        and
        not prevTrig


    prevTrig =
        trigNow


    -- NO cooldown.
    -- Every trigger edge can immediately re-strike the field.

    if trigEdge then

        trigPressure =
            1.0

    else

        trigPressure =
            trigPressure *
            0.82
    end


    -- Sharp amplitude attacks get a smaller secondary strike.
    -- This catches some transients even if hardware TRIG misses them.

    local onset =
        energy -
        oldSmoothEnergy


    if onset < 0 then
        onset = 0
    end


    local onsetStrike =
        onset *
        2.7


    if onsetStrike > 0.58 then
        onsetStrike = 0.58
    end


    if onsetStrike > trigPressure then
        trigPressure =
            onsetStrike
    end


    ---------------------------------------------------------------
    -- TIME
    ---------------------------------------------------------------

    frameTime =
        frameTime +
        FRAME_DT


    ---------------------------------------------------------------
    -- SHADERHOST
    --
    -- u_time       = frameTime
    -- u_energy     = smoothEnergy
    -- u_brightness = smoothRoughness
    -- u_trig       = trigPressure
    ---------------------------------------------------------------

    shaderhost_step(
        frameTime,
        smoothEnergy,
        smoothRoughness,
        trigPressure,
        k1,
        k2,
        k3,
        k4,
        k5
    )


    shaderhost_draw()
end


---------------------------------------------------------------------
-- EXIT
---------------------------------------------------------------------

function exit()

    if type(shaderhost_feedback_enable) == "function" then

        shaderhost_feedback_enable(
            false
        )
    end


    print(
        "SIGNAL_SEED_MICRO_V2 exit"
    )

    print(
        "script finished"
    )
end