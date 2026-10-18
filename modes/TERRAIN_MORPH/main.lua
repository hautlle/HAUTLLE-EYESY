---- TERRAIN MORPH v2.1 AUDIO WEATHER
require("eyesy")

modeTitle = "Terrain Morph v2.1 Audio Weather"
titleK1 = "Motion"
titleK2 = "Scale"
titleK3 = "Warp"
titleK4 = "Palette"
titleK5 = "Topology"
print(modeTitle)

local fragShader = [[
precision mediump float;

uniform vec2 u_resolution;
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

float fieldLayer(vec2 p, float t, float scale) {
    float v = 0.0;
    v += sin(p.x * scale + t) * cos(p.y * (scale * 0.67) - t * 0.47);
    v += sin((p.x + p.y) * (scale * 0.41) - t * 0.31) * 0.55;
    v += cos(length(p) * (scale * 0.85) - t * 0.22) * 0.35;
    return v;
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Feedback FBO texture reads vertically flipped unless corrected.
vec3 readPrev(vec2 p) {
    p.y = 1.0 - p.y;
    return texture2D(u_prevFrame, p).rgb;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec2 p = uv * 2.0 - 1.0;
    p.x *= u_resolution.x / u_resolution.y;

    // IG speed pass: faster motion range for short video capture.
    float t = u_time * (0.10 + u_k1 * 0.55);

    // K2 controls structural scale.
    float scale = mix(4.0, 10.0, u_k2);

    // Audio is terrain pressure, not a brightness strobe.
    // Energy drives large-scale deformation; brightness adds faster surface agitation.
    float audioPressure = clamp(u_energy * 1.35 + u_brightness * 0.55, 0.0, 1.0);
    float hitPressure   = clamp(u_trig, 0.0, 1.0);
    float audioWeather  = clamp(audioPressure + hitPressure * 0.65, 0.0, 1.35);

    // K3 remains the manual warp control, but audio now has enough authority
    // to visibly bend and breathe the terrain.
    float warpAmt = 0.06
                  + u_k3 * mix(0.32, 0.52, u_k2)
                  + audioPressure * mix(0.10, 0.24, u_k3)
                  + hitPressure * 0.14;

    vec2 warp;
    warp.x = sin(p.y * 2.2 + t * 0.9 + audioPressure * 2.4) * warpAmt;
    warp.y = cos(p.x * 2.0 - t * 0.7 - audioPressure * 1.8) * warpAmt;

    // Bass/energy creates a slower radial shove through the field.
    float radialPulse = sin(length(p) * (5.0 + u_k2 * 4.0)
                          - t * 1.6
                          - audioPressure * 3.2);
    warp += normalize(p + vec2(0.0001)) * radialPulse
          * (audioPressure * 0.075 + hitPressure * 0.055);

    vec2 wp = p + warp;

    float field = fieldLayer(wp, t, scale);
    field = field * 0.5 + 0.5;

    // Softer scan/interference. Avoid resolution-scaled high-frequency shimmer.
    float scan = sin(uv.y * 80.0 + t * 2.0) * 0.018;
    field += scan;

    // Audio changes terrain elevation rather than simply making it brighter.
    // This makes contours appear to rise/fall with the input.
    field += (audioPressure - 0.28) * 0.11;
    field += hitPressure * 0.055;

    field = clamp(field, 0.0, 1.0);

    // --- terrain normalization + true-black void ---
    // Loud material temporarily pushes more terrain above the void floor.
    float voidThreshold = 0.18 - audioPressure * 0.045 - hitPressure * 0.025;
    float terrain = max(0.0, (field - voidThreshold) / (1.0 - voidThreshold));
    terrain = clamp(terrain, 0.0, 1.0);

    // --- K5: smooth gradient -> contour -> posterized topology ---
    // Audio compresses the topology toward fewer, bolder contour bands.
    float levels = mix(30.0, 4.0, u_k5);
    levels *= mix(1.0, 0.72, audioPressure);
    levels = max(levels, 3.0);
    float quantized = floor(terrain * levels) / levels;

    float shapedK5 = smoothstep(0.10, 0.92, u_k5);
    float shaped = mix(terrain, quantized, shapedK5);

    vec3 coldLow   = vec3(0.0, 0.0, 0.0);
    vec3 coldHigh  = vec3(0.10, 0.45, 0.75);
    vec3 vioHigh   = vec3(0.52, 0.18, 0.72);
    vec3 warmHigh  = vec3(0.90, 0.42, 0.10);

    vec3 cold   = mix(coldLow, coldHigh, shaped);
    vec3 violet = mix(coldLow, vioHigh, shaped);
    vec3 warm   = mix(coldLow, warmHigh, shaped);

    vec3 terrainColor;
    if (u_k4 < 0.5) {
        terrainColor = mix(cold, violet, u_k4 * 2.0);
    } else {
        terrainColor = mix(violet, warm, (u_k4 - 0.5) * 2.0);
    }

    // Void RGB stays true black unless ShaderHost memory is allowed through.
    float voidMask = 1.0 - smoothstep(0.0, 0.018, terrain);
    vec3 color = mix(terrainColor, vec3(0.0), voidMask);

    // Small highlight only on higher terrain, preserving the current glow identity.
    // Audio can strengthen this edge glow, but never becomes a full-screen flash.
    color += vec3(0.06 + audioPressure * 0.08)
           * smoothstep(0.72 - audioPressure * 0.08, 1.0, shaped)
           * (1.0 - voidMask);

    // -------------------------
    // ShaderHost memory bus
    // -------------------------
    // This does NOT sample the final EYESY screen.
    // It samples the private ShaderHost feedback buffer.
    // Shift+Persist controls whether this gate is open.
    // Long Shift+Persist clear wipes this memory.

    if (u_feedbackEnabled > 0.5 && u_feedbackReady > 0.5) {
        // Let memory drift slightly under the terrain.
        vec2 memUv = uv;

        float blockCount = mix(10.0, 36.0, u_k3);
        vec2 blockUv = floor(uv * vec2(blockCount, blockCount * 0.5625));
        float r = hash21(blockUv + floor(u_time * mix(0.35, 2.5, u_k1)));

        float slip = (r - 0.5) * mix(0.002, 0.040, u_k3);
        memUv.x += slip;
        memUv.y += sin(blockUv.x * 0.7 + u_time * 0.25) * 0.002 * u_k3;

        vec3 memory = readPrev(memUv);

        // Pull memory mostly through voids and contour edges, not flat terrain body.
        float q = terrain * levels;
        float f = fract(q);
        float distToEdge = min(f, 1.0 - f);
        float contourMemory = 1.0 - smoothstep(0.018, 0.12, distToEdge);
        contourMemory *= smoothstep(0.10, 0.70, shapedK5);

        float lowTerrainMemory = 1.0 - smoothstep(0.12, 0.62, terrain);
        float memoryGate = max(voidMask * 0.82, lowTerrainMemory * 0.28);
        memoryGate = max(memoryGate, contourMemory * 0.38);

        // Keep memory dim enough that terrain still owns the mode.
        float memoryStrength = mix(0.10, 0.52, u_k3);
        color = mix(color, max(color, memory * 0.95), memoryGate * memoryStrength);

        // Audio/trig expose archive residue much more clearly in low terrain.
        color += memory * audioPressure * 0.18 * lowTerrainMemory;
        color += memory * hitPressure * 0.24;
    }

    // --- persist-aware alpha ---
    // Smooth mode is fairly solid.
    // Mid contour mode lets lines carry memory.
    // Posterized mode gets stronger again.
    float fillAlpha = mix(0.72, 0.38, smoothstep(0.0, 0.50, shapedK5));
    fillAlpha = mix(fillAlpha, 0.64, smoothstep(0.50, 1.0, shapedK5));
    fillAlpha *= smoothstep(0.02, 0.20, terrain);

    // Contour alpha appears as K5 rises.
    float q2 = terrain * levels;
    float f2 = fract(q2);
    float distToEdge2 = min(f2, 1.0 - f2);
    float contourAlpha = 1.0 - smoothstep(0.018, 0.095, distToEdge2);
    contourAlpha *= smoothstep(0.08, 0.45, shapedK5);

    float bodyAlpha = clamp(max(fillAlpha, contourAlpha * 0.92), 0.0, 1.0);

    // Void is transparent black. Persist OFF still shows clean black because Lua clears first.
    // Persist ON lets previous contour residue live inside the low/void areas.
    float voidAlpha = 0.02;
    float alpha = mix(bodyAlpha, voidAlpha, voidMask);

    gl_FragColor = vec4(color, alpha);
}
]]

local smoothEnergy = 0.0
local smoothBrightness = 0.0
local trigPressure = 0.0
local prevTrig = false
local hostAvailable = false
local shaderLoaded = false

local function has_shaderhost_api()
    return type(shaderhost_load) == "function"
        and type(shaderhost_step) == "function"
        and type(shaderhost_draw) == "function"
end

local function compute_audio_features()
    local samples = inL or {}
    local energy = 0
    local crossings = 0
    local prevSign = nil

    for i = 1, 100 do
        local s = samples[i] or 0
        energy = energy + math.abs(s)

        local sign = s >= 0
        if prevSign ~= nil and sign ~= prevSign then
            crossings = crossings + 1
        end
        prevSign = sign
    end

    energy = math.min(1, (energy / 100) * 3)
    local brightness = crossings / 100

    return energy, brightness
end

function setup()
    print("done setup")
    hostAvailable = has_shaderhost_api()

    if not hostAvailable then
        print("SHADER_TERRAIN_MEMORY: shaderhost_* functions not found -- ShaderHostGenerator not registered in this build.")
        return
    end

    if type(shaderhost_reset) == "function" then
        shaderhost_reset()
    end

    local ok = shaderhost_load(fragShader)
    shaderLoaded = ok

    if not ok then
        print("SHADER_TERRAIN_MEMORY: shaderhost_load() returned false -- shader failed to compile/link. Check console for GLSL errors.")
        return
    end

    print("SHADER_TERRAIN_MEMORY: shader loaded")

    -- Memory-bus compliant:
    -- Enable ShaderHost feedback so this mode can read/write the private buffer.
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(true)
        print("SHADER_TERRAIN_MEMORY: ShaderHost feedback enabled")
    else
        print("SHADER_TERRAIN_MEMORY: feedback API missing")
    end

    -- IMPORTANT:
    -- Do NOT clear by default.
    -- This lets this mode inherit residue from the previous ShaderHost feedback writer.
    --
    -- If you want a clean-entry version, uncomment this:
    --
    -- if type(shaderhost_feedback_clear) == "function" then
    --     shaderhost_feedback_clear()
    -- end

    smoothEnergy = 0.0
    smoothBrightness = 0.0
    trigPressure = 0.0
    prevTrig = false
end

function update()
end

function draw()
    -- Important: enable blending BEFORE the persist fade rectangle.
    -- shaderhost_draw() enables blending too, but that happens too late
    -- for the Lua fade pass above the shader.
    if of.enableAlphaBlending then
        of.enableAlphaBlending()
    end

    local W = renderW or 1280
    local H = renderH or 720

    local bgA
    if persist and not persistClear then
        bgA = 5
    else
        bgA = 255
    end

    of.setColor(0, 0, 0, bgA)
    of.drawRectangle(0, 0, W, H)

    if not hostAvailable or not shaderLoaded then
        return
    end

    local energy, brightness = compute_audio_features()

    -- Slower smoothing: audio becomes weather, not strobe.
    smoothEnergy = smoothEnergy + (energy - smoothEnergy) * 0.035
    smoothBrightness = smoothBrightness + (brightness - smoothBrightness) * 0.025

    local trigNow = (trig == true) or ((tonumber(trig) or 0) > 0)
    local trigEdge = trigNow and not prevTrig
    prevTrig = trigNow

    -- Trigger becomes a decaying terrain shove, not a one-frame flash.
    if trigEdge then
        trigPressure = 1.0
    end
    trigPressure = trigPressure * 0.885

    shaderhost_step(os.clock(), smoothEnergy, smoothBrightness, trigPressure,
                    knob1 or 0.3, knob2 or 0.4, knob3 or 0.3,
                    knob4 or 0.0, knob5 or 0.5)

    shaderhost_draw()
end

function exit()
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    print("script finished")
end