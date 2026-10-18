-- HAUTLLE_INTERFERENCE_03
-- Procedural moire/interference instrument using the known-working
-- MarkovMosh native texture path as a ShaderHost carrier.
--
-- K1 = Density
-- K2 = Registration / field separation
-- K3 = Geometry
-- K4 = Palette
-- K5 = Contrast / line pressure
--
-- Requires:
--   markovmosh_reset / markovmosh_step
--   ShaderHost external texture binding: u_markovMoshTex

require("eyesy")

modeTitle = "HAUTLLE Interference 03"
titleK1 = "Density"
titleK2 = "Register"
titleK3 = "Geometry"
titleK4 = "Palette"
titleK5 = "Pressure"
print(modeTitle)

local hostAvailable = false
local carrierAvailable = false
local shaderLoaded = false
local startTime = os.clock()
local prevTrig = false

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

// IMPORTANT: this sampler keeps us on the external-texture ShaderHost path
// already proven by HAUTLLE_MARKOV_MOSH_01.
uniform sampler2D u_markovMoshTex;

#define PI 3.14159265359

float sat(float x) {
    return clamp(x, 0.0, 1.0);
}

vec2 rotate2(vec2 p, float a) {
    float s = sin(a);
    float c = cos(a);
    return vec2(c*p.x - s*p.y, s*p.x + c*p.y);
}

// Anti-aliased repeating line lattice.
float lattice(float x, float freq, float width) {
    float v = abs(fract(x * freq) - 0.5) * 2.0;
    return 1.0 - smoothstep(width, width + 0.055, v);
}

float stripes(vec2 p, float freq, float width) {
    return lattice(p.x, freq, width);
}

float grid(vec2 p, float freq, float width) {
    return max(lattice(p.x, freq, width),
               lattice(p.y, freq, width));
}

float rings(vec2 p, float freq, float width) {
    return lattice(length(p), freq, width);
}

float spiral(vec2 p, float freq, float width, float twist) {
    float r = length(p);
    float a = atan(p.y, p.x);
    return lattice(r + a * twist, freq, width);
}

float field(vec2 p, float freq, float width, float geometry, float twist) {
    if (geometry < 0.25) {
        return stripes(p, freq, width);
    } else if (geometry < 0.50) {
        return grid(p, freq * 0.72, width);
    } else if (geometry < 0.75) {
        return rings(p, freq * 0.78, width);
    }
    return spiral(p, freq * 0.70, width, twist);
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec2 p = uv - 0.5;
    p.x *= u_resolution.x / max(u_resolution.y, 1.0);

    float k1 = sat(u_k1);
    float k2 = sat(u_k2);
    float k3 = sat(u_k3);
    float k4 = sat(u_k4);
    float k5 = sat(u_k5);

    // Broad playable range without immediately collapsing into sub-pixel noise.
    float density = mix(10.0, 82.0, k1 * k1);

    // K2 changes BOTH angle and scale relationship. Tiny offsets are where
    // the largest moire structures appear.
    float registerCurve = k2 * k2;
    float angle = mix(0.0015, 0.155, registerCurve);
    float scaleDelta = mix(0.0004, 0.042, registerCurve);

    // Slow autonomous drift is now fixed. K4 is reserved entirely for color.
    float travel = u_time * 0.105;

    // Audio is always structurally active. Small changes are deliberately
    // amplified by the moire relationship instead of pulsing brightness.
    float audioEnergy = sat(u_energy);
    float audioKick = sat(u_trig);

    float audioAngle = audioEnergy * 0.0105
                     + audioKick   * 0.0065;

    float audioScale = audioEnergy * 0.0048
                     + audioKick   * 0.0025;

    float audioPhase = audioEnergy * 0.0080
                     + audioKick   * 0.0110;

    vec2 aP = rotate2(p, -0.5 * angle - audioAngle * 0.18);
    vec2 bP = rotate2(p,  0.5 * angle + audioAngle);

    // Opposing slow drift creates giant beat structures even in silence.
    aP += vec2( travel * 0.030, sin(travel * 0.23) * 0.0045);
    bP += vec2(-travel * 0.026, cos(travel * 0.19) * 0.0055);

    // Audio slips one grating against the other in phase and scale.
    aP.x += audioPhase * 0.22;
    bP += vec2(-audioPhase, audioPhase * 0.37);
    bP *= 1.0 + scaleDelta + audioScale;

    // Gentle autonomous bend plus a restrained audio flex.
    float audioFlex = audioEnergy * 0.0045;
    float bendA = sin(p.y * 7.0 + travel * 0.31) * (0.0075 + audioFlex);
    float bendB = sin(p.x * 5.5 - travel * 0.27) * (0.0065 + audioFlex * 0.75);
    aP.x += bendA;
    bP.y += bendB;

    float width = mix(0.075, 0.34, k5);
    float twist = mix(0.010, 0.095, k3);

    float A = field(aP, density, width, k3,  twist);
    float B = field(bP, density * (1.0 + scaleDelta), width, k3, -twist);

    // The image is built from disagreement + coincidence between the fields.
    float disagreement = abs(A - B);
    float coincidence  = A * B;

    float threshold = mix(0.78, 0.16, k5);
    float structure = smoothstep(threshold, 1.0, disagreement);
    structure += coincidence * mix(0.48, 0.12, k5);
    structure = sat(structure);

    // Slight center emphasis keeps large moire forms readable on stage.
    float r2 = dot(p, p);
    structure *= 1.0 - smoothstep(0.48, 1.10, r2) * 0.16;

    // K4 palette. The first 30% is deliberately strict monochrome so B&W
    // remains the native identity of the mode.
    vec3 color;

    if (k4 < 0.30) {
        // Neutral black / white.
        color = vec3(structure);
    }
    else if (k4 < 0.48) {
        // Cold silver-blue.
        vec3 ink = vec3(0.005, 0.012, 0.020);
        vec3 paper = vec3(0.70, 0.86, 1.00);
        color = mix(ink, paper, structure);
    }
    else if (k4 < 0.66) {
        // Warm red / black print.
        vec3 ink = vec3(0.018, 0.002, 0.001);
        vec3 paper = vec3(1.00, 0.19, 0.085);
        color = mix(ink, paper, structure);
    }
    else if (k4 < 0.82) {
        // Green phosphor / instrument display.
        vec3 ink = vec3(0.001, 0.012, 0.003);
        vec3 paper = vec3(0.34, 1.00, 0.43);
        color = mix(ink, paper, structure);
    }
    else {
        // Chromatic registration: the two component fields carry different
        // inks, so color is generated by their disagreement rather than by
        // simply tinting the finished grayscale image.
        float cyanField = sat(A * 0.90 + disagreement * 0.52);
        float magentaField = sat(B * 0.90 + disagreement * 0.52);
        float common = coincidence * 0.48;

        color = vec3(
            magentaField + common,
            cyanField * 0.72 + common * 0.52,
            cyanField + magentaField * 0.20 + common
        );

        // Retain the large-scale moire structure as the dominant mask.
        color *= mix(0.34, 1.0, structure);
    }

    // Audio moves geometry; it does not act as a brightness fader.

    // Force the known-good external texture binding/render path. The carrier
    // is intentionally invisible to the eye, but its tiny contribution keeps
    // the sampler live so the compiler cannot optimize it away.
    vec3 carrier = texture2D(u_markovMoshTex, vec2(0.5, 0.5)).rgb;
    color += carrier * 0.00001;

    float alpha = clamp(0.32 + structure * 0.64, 0.0, 0.94);
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), alpha);
}
]]

local function clamp01(x)
    x = tonumber(x) or 0.0
    if x < 0.0 then return 0.0 end
    if x > 1.0 then return 1.0 end
    return x
end

local function audio_value(name)
    if type(_G[name]) == "number" then
        return clamp01(_G[name])
    end
    return 0.0
end

local function read_audio()
    local low = audio_value("low")
    local mid = audio_value("mid")
    local hi = audio_value("high")
    if hi == 0.0 then hi = audio_value("hi") end

    local e = audio_value("energy")
    if e == 0.0 then
        e = clamp01((low + mid + hi) / 3.0)
    end

    -- Fallback for modes/OS builds exposing peak instead of energy bands.
    if e == 0.0 then
        local peak = audio_value("peak")
        if peak == 0.0 then peak = audio_value("audio_peak") end
        e = math.sqrt(clamp01(peak * 2.5))
    end

    local rawTrig = trig == true or trig == 1
    local edgeTrig = rawTrig and not prevTrig
    prevTrig = rawTrig

    return e, low, mid, hi, edgeTrig
end

function setup()
    print("INTERFERENCE_03 setup")

    if type(shaderhost_reset) == "function" then
        shaderhost_reset()
    end

    carrierAvailable =
        type(markovmosh_reset) == "function" and
        type(markovmosh_step) == "function"

    hostAvailable =
        type(shaderhost_load) == "function" and
        type(shaderhost_step) == "function" and
        type(shaderhost_draw) == "function"

    if not carrierAvailable then
        print("INTERFERENCE_03 ERROR: markovmosh carrier API missing")
        return
    end

    if not hostAvailable then
        print("INTERFERENCE_03 ERROR: ShaderHost API missing")
        return
    end

    markovmosh_reset()

    shaderLoaded = shaderhost_load(fragShader)
    if shaderLoaded then
        print("INTERFERENCE_03 shader loaded")
    else
        print("INTERFERENCE_03 ERROR: shader compile/link failed")
    end
end

function update()
end

function draw()
    if not carrierAvailable or not hostAvailable or not shaderLoaded then
        return
    end

    local t = os.clock() - startTime
    local e, low, mid, hi, edgeTrig = read_audio()

    local k1 = clamp01(knob1 or 0.36)
    local k2 = clamp01(knob2 or 0.18)
    local k3 = clamp01(knob3 or 0.08)
    local k4 = clamp01(knob4 or 0.18)
    local k5 = clamp01(knob5 or 0.55)

    -- Run the native generator solely so ShaderHost has the external texture
    -- path it already knows how to bind and draw on this OS build.
    -- Carrier K4 is pinned so changing the visible palette cannot alter the
    -- hidden Markov generator behavior underneath ShaderHost.
    markovmosh_step(t, e, low, mid, hi, edgeTrig, k1, k2, k3, 0.25, k5)

    shaderhost_step(t, e, 1.0, edgeTrig, k1, k2, k3, k4, k5)
    shaderhost_draw()
end

function exit()
    print("HAUTLLE Interference 03 finished")
end
