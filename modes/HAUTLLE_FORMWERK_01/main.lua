-- HAUTLLE_FORMWERK_01
-- Constructed geometry / mechanical poster field
--
-- Hardware:
--   Persist                = engine/global persist
--   Shift + Persist        = ShaderHost feedback gate
--   Shift + long Persist   = clear ShaderHost memory
--
-- Feedback lifecycle:
--   * Enter with ShaderHost feedback disabled.
--   * Load the new shader before enabling feedback.
--   * Clear private ShaderHost feedback on entry.
--   * Disable and clear private ShaderHost feedback on exit.
--
-- This prevents FORM/WORK frames from leaking into later ShaderHost modes
-- and prevents FORM/WORK from inheriting stale private feedback from them.

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle   = "HAUTLLE FORM/WORK 01"
modeExplain = "Constructed geometry / mechanical poster field"
titleK1 = "Forms"
titleK2 = "Tempo"
titleK3 = "Rupture"
titleK4 = "Palette"
titleK5 = "Pressure"

print(modeTitle)

local hostAvailable = false
local generatorAvailable = false
local shaderLoaded = false
local frameTime = 0.0

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

uniform sampler2D u_bauhausTex;
uniform sampler2D u_prevFrame;
uniform float u_feedbackEnabled;
uniform float u_feedbackReady;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec3 readSource(vec2 uv) {
    // Native OF FBO textures read vertically inverted through ShaderHost.
    uv.y = 1.0 - uv.y;
    return texture2D(u_bauhausTex, clamp(uv, 0.0, 1.0)).rgb;
}

vec3 readPrev(vec2 uv) {
    uv.y = 1.0 - uv.y;
    return texture2D(u_prevFrame, clamp(uv, 0.0, 1.0)).rgb;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec2 sourceTexel = vec2(1.0 / 640.0, 1.0 / 360.0);

    // Three-sample color registration. At K5 minimum this collapses to one
    // ordinary texture read; at maximum the channels separate by ~2.5 pixels.
    float press = clamp(u_energy * 1.25 + u_trig * 0.65, 0.0, 1.0);
    float registerPx = u_k5 * 2.5 + press * 3.5;
    vec2 registerOffset = sourceTexel * registerPx;

    vec3 center = readSource(uv);
    vec3 shiftedA = readSource(uv + vec2(registerOffset.x, 0.0));
    vec3 shiftedB = readSource(uv - vec2(0.0, registerOffset.y));

    vec3 color = vec3(shiftedA.r, center.g, shiftedB.b);

    // Four cheap neighbor taps approximate ink spread without a blur loop.
    vec3 spread = readSource(uv + vec2(sourceTexel.x, 0.0))
                + readSource(uv - vec2(sourceTexel.x, 0.0))
                + readSource(uv + vec2(0.0, sourceTexel.y))
                + readSource(uv - vec2(0.0, sourceTexel.y));
    spread *= 0.25;
    color = mix(color, spread, u_k5 * 0.16);

    // Discrete print-history ghosting. The gate is controlled globally by
    // Shift + Persist; this mode opts into the private feedback bus only after
    // setup has loaded the current shader and cleared stale private memory.
    if (u_feedbackEnabled > 0.5 && u_feedbackReady > 0.5) {
        float tick = floor(u_time * mix(0.7, 2.6, u_k2));
        float rx = hash21(vec2(tick, 3.1)) - 0.5;
        float ry = hash21(vec2(7.7, tick)) - 0.5;
        vec2 ghostOffset = vec2(rx, ry)
                         * vec2(0.020, 0.014)
                         * mix(0.18, 1.0, u_k3);
        vec2 steppedUv = floor((uv + ghostOffset) * vec2(80.0, 45.0))
                       / vec2(80.0, 45.0);
        vec3 memory = readPrev(steppedUv);
        float memoryMix = mix(0.035, 0.24, u_k5)
                        * mix(0.35, 1.0, u_k3);
        color = mix(color, memory, memoryMix);
    }

    // Paper/ink variation is deliberately tiny and stable enough not to read
    // as digital television noise.
    float grain = hash21(gl_FragCoord.xy + floor(u_time * 2.0)) - 0.5;
    float band = sin(gl_FragCoord.y * 0.095 + floor(u_time * 1.3)) * 0.5 + 0.5;
    color += grain * mix(0.002, 0.018, u_k5);
    color *= 1.0 - band * u_k5 * 0.012;

    // Audio acts as a brief press strike, not as continuous deformation.
    color *= 1.0 + press * 0.16;

    // Pressure increases contrast while preserving the light palette fields.
    float gamma = mix(1.04, 0.86, u_k5);
    color = pow(max(color, vec3(0.0)), vec3(gamma));
    color = clamp(color, 0.0, 1.0);

    gl_FragColor = vec4(color, 1.0);
}
]]

local function clamp01(x)
    x = tonumber(x) or 0
    if x < 0 then return 0 end
    if x > 1 then return 1 end
    return x
end

local function getv(name, fallback)
    local v = _G[name]

    if type(v) == "number" then
        return v
    end

    if type(v) == "function" then
        local ok, result = pcall(v)
        if ok and type(result) == "number" then
            return result
        end
    end

    return fallback
end

local function trig_now()
    if type(trig) == "boolean" then
        return trig
    end

    return (tonumber(trig) or 0) > 0
end

local function audio_energy()
    local raw = clamp01(getv("peak", getv("audio_peak", 0.0)))

    -- Direct mixer feed: expand useful musical range without a noise gate.
    raw = clamp01(raw * 2.5)

    -- Bring medium-level material forward while retaining dynamics.
    return math.sqrt(raw)
end

function setup()
    print("HAUTLLE_FORMWERK_01 setup")

    shaderLoaded = false
    frameTime = 0.0

    generatorAvailable =
        type(bauhaus_prepare) == "function" and
        type(bauhaus_step) == "function"

    hostAvailable =
        type(shaderhost_load) == "function" and
        type(shaderhost_step) == "function" and
        type(shaderhost_draw) == "function"

    if not generatorAvailable then
        print("HAUTLLE_FORMWERK_01 ERROR: native bauhaus_* API missing")
        return
    end

    if not hostAvailable then
        print("HAUTLLE_FORMWERK_01 ERROR: ShaderHost API missing")
        return
    end

    -- Enter with private ShaderHost feedback OFF so stale frames from another
    -- mode cannot become visible while this mode prepares/loads.
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    -- Clear the outgoing ShaderHost program/uniform state before loading ours.
    if type(shaderhost_reset) == "function" then
        shaderhost_reset()
    end

    -- Prepare FORM/WORK's native source texture/FBO.
    bauhaus_prepare()

    -- Compile/link this mode's shader while private feedback is still disabled.
    shaderLoaded = shaderhost_load(fragShader)

    if not shaderLoaded then
        print("HAUTLLE_FORMWERK_01 ERROR: shader failed")
        return
    end

    print("HAUTLLE_FORMWERK_01 shader loaded")

    -- Opt into ShaderHost feedback only after the current shader is valid.
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(true)
        print("HAUTLLE_FORMWERK_01 ShaderHost feedback opt-in ON")
    end

    -- Start from known-clean private feedback instead of inheriting an older
    -- ShaderHost mode's image.
    if type(shaderhost_feedback_clear) == "function" then
        shaderhost_feedback_clear()
        print("HAUTLLE_FORMWERK_01 feedback cleared on entry")
    end
end

function update()
end

function draw()
    local W = renderW or 1280
    local H = renderH or 720

    if not generatorAvailable or not hostAvailable or not shaderLoaded then
        of.setColor(48, 0, 0, 255)
        of.drawRectangle(0, 0, W, H)
        return
    end

    local dt = 1.0 / 30.0
    frameTime = frameTime + dt

    local k1 = clamp01(getv("knob1", 0.30))
    local k2 = clamp01(getv("knob2", 0.28))
    local k3 = clamp01(getv("knob3", 0.16))
    local k4 = clamp01(getv("knob4", 0.04))
    local k5 = clamp01(getv("knob5", 0.46))

    local energy = audio_energy()
    local hit = trig_now()

    bauhaus_step(dt, energy, hit, k1, k2, k3, k4, k5)
    shaderhost_step(frameTime, energy, 1.0, hit, k1, k2, k3, k4, k5)
    shaderhost_draw()
end

function exit()
    -- Do not expose FORM/WORK's private feedback to the next mode.
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    -- Also clear the private feedback texture so FORM/WORK cannot become the
    -- "ghost frame" resurrected by a later ShaderHost mode.
    if type(shaderhost_feedback_clear) == "function" then
        shaderhost_feedback_clear()
    end

    print("HAUTLLE_FORMWERK_01 exit")
    print("script finished")
end