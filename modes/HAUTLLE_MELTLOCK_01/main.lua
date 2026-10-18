-- HAUTLLE_MELTLOCK_01
-- Native low-resolution liquid field + ShaderHost phase/finish pass.
--
-- K1 = Mass / blob population
-- K2 = Convection speed
-- K3 = Phase: liquid -> rigid glitch structure
-- K4 = Palette bank
-- K5 = Surface depth / memory
--
-- Hardware:
--   Persist               = engine/global persist
--   Shift + Persist       = ShaderHost feedback gate
--   Shift + long Persist  = clear ShaderHost memory

require("eyesy")

inL = inL or {}
inR = inR or {}

modeTitle   = "HAUTLLE MELT/LOCK 01"
modeExplain = "Buoyant matter / stepped phase architecture"
titleK1 = "Mass"
titleK2 = "Flow"
titleK3 = "Phase"
titleK4 = "Palette"
titleK5 = "Surface"

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

uniform sampler2D u_meltlockTex;
uniform sampler2D u_prevFrame;
uniform float u_feedbackEnabled;
uniform float u_feedbackReady;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float readField(vec2 uv) {
    uv.y = 1.0 - uv.y;
    return texture2D(u_meltlockTex, clamp(uv, 0.0, 1.0)).r;
}

vec3 readPrev(vec2 uv) {
    uv.y = 1.0 - uv.y;
    return texture2D(u_prevFrame, clamp(uv, 0.0, 1.0)).rgb;
}

void choosePalette(float bank,
                   out vec3 background,
                   out vec3 shadow,
                   out vec3 body,
                   out vec3 highlight) {
    if (bank < 0.5) {
        background = vec3(0.035, 0.018, 0.020);
        shadow     = vec3(0.180, 0.018, 0.022);
        body       = vec3(0.880, 0.105, 0.032);
        highlight  = vec3(1.000, 0.620, 0.120);
    } else if (bank < 1.5) {
        background = vec3(0.008, 0.026, 0.030);
        shadow     = vec3(0.016, 0.190, 0.145);
        body       = vec3(0.170, 0.820, 0.420);
        highlight  = vec3(0.760, 1.000, 0.520);
    } else if (bank < 2.5) {
        background = vec3(0.018, 0.015, 0.040);
        shadow     = vec3(0.160, 0.080, 0.330);
        body       = vec3(0.720, 0.160, 0.560);
        highlight  = vec3(1.000, 0.520, 0.260);
    } else if (bank < 3.5) {
        background = vec3(0.015, 0.020, 0.024);
        shadow     = vec3(0.030, 0.180, 0.250);
        body       = vec3(0.050, 0.700, 0.870);
        highlight  = vec3(0.920, 0.980, 0.870);
    } else if (bank < 4.5) {
        background = vec3(0.025, 0.024, 0.021);
        shadow     = vec3(0.120, 0.115, 0.100);
        body       = vec3(0.715, 0.680, 0.555);
        highlight  = vec3(1.000, 0.955, 0.770);
    } else {
        background = vec3(0.015, 0.012, 0.010);
        shadow     = vec3(0.185, 0.055, 0.015);
        body       = vec3(0.920, 0.360, 0.025);
        highlight  = vec3(1.000, 0.900, 0.470);
    }
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    vec2 sourceTexel = vec2(1.0 / 320.0, 1.0 / 180.0);

    float phase = smoothstep(0.04, 0.96, u_k3);
    float hardPhase = smoothstep(0.38, 0.96, u_k3);
    float fracture = clamp(u_trig * 0.85 + u_energy * 0.16, 0.0, 1.0);

    // Liquid domain drift disappears as the material locks. It is intentionally
    // broad and slow; the native field already supplies the main motion.
    vec2 liquidWarp = vec2(
        sin(uv.y * 8.0 + u_time * mix(0.18, 0.72, u_k2)),
        cos(uv.x * 6.0 - u_time * mix(0.14, 0.55, u_k2))
    );
    liquidWarp *= sourceTexel * mix(3.6, 0.25, phase);

    // Phase lock progressively samples the field on a coarse rectilinear grid.
    vec2 cellCount = mix(vec2(110.0, 62.0),
                         vec2(17.0, 11.0),
                         hardPhase);
    vec2 cell = floor(uv * cellCount);
    vec2 cellUv = (cell + 0.5) / cellCount;

    float tick = floor(u_time * mix(0.24, 2.35, u_k2));
    float blockNoise = hash21(cell + vec2(tick * 0.73, tick * 1.17));
    float shiftGate = smoothstep(0.77, 0.97, blockNoise)
                    * hardPhase
                    * (0.30 + 0.70 * u_k5);
    float shiftSign = step(0.5, hash21(cell + vec2(8.3, tick))) * 2.0 - 1.0;
    float shiftCells = shiftSign * mix(0.0, 2.2, hardPhase)
                     * (shiftGate + fracture * 0.45);
    cellUv.x += shiftCells / cellCount.x;

    vec2 sampleUv = mix(uv + liquidWarp, cellUv, hardPhase * 0.94);

    // Axis extrusion produces architectural shelves from the same liquid field.
    vec2 axisUvX = vec2(sampleUv.x, cellUv.y);
    vec2 axisUvY = vec2(cellUv.x, sampleUv.y);
    float field = readField(sampleUv);
    float axisField = max(readField(axisUvX), readField(axisUvY));
    field = mix(field, max(field, axisField * 0.86), hardPhase * 0.72);

    float levels = mix(36.0, 5.0, hardPhase);
    float terraced = floor(field * levels + 0.5) / levels;
    field = mix(field, terraced, hardPhase * 0.88);

    // Four field taps provide a surface normal and remain fixed-cost.
    float fx1 = readField(sampleUv + vec2(sourceTexel.x, 0.0));
    float fx0 = readField(sampleUv - vec2(sourceTexel.x, 0.0));
    float fy1 = readField(sampleUv + vec2(0.0, sourceTexel.y));
    float fy0 = readField(sampleUv - vec2(0.0, sourceTexel.y));
    vec2 gradient = vec2(fx1 - fx0, fy1 - fy0);

    float threshold = mix(0.305, 0.225, u_k1)
                    - u_energy * 0.030;
    float antialiasWidth = mix(0.040, 0.006, hardPhase);
    float bodyMask = smoothstep(threshold - antialiasWidth,
                                threshold + antialiasWidth,
                                field);

    float edgeDistance = abs(field - threshold);
    float liquidEdge = 1.0 - smoothstep(0.015, 0.095, edgeDistance);
    float hardEdge = 1.0 - smoothstep(0.003, 0.026, edgeDistance);
    float edge = mix(liquidEdge, hardEdge, hardPhase);

    vec3 background;
    vec3 shadow;
    vec3 body;
    vec3 highlight;
    float bank = floor(clamp(u_k4, 0.0, 0.9999) * 6.0);
    choosePalette(bank, background, shadow, body, highlight);

    float normalLight = clamp(0.52
                            - gradient.x * 5.3
                            + gradient.y * 7.8,
                              0.0, 1.0);
    float innerDepth = smoothstep(threshold, threshold + 0.52, field);
    float contour = 0.5 + 0.5 * sin(field * mix(17.0, 43.0, u_k5)
                                 - u_time * mix(0.10, 0.34, u_k2));
    contour = smoothstep(0.34, 0.82, contour) * bodyMask;

    vec3 material = mix(shadow, body, innerDepth * 0.82 + normalLight * 0.18);
    material = mix(material, highlight,
                   edge * mix(0.34, 0.78, u_k5));
    material = mix(material, highlight,
                   contour * u_k5 * mix(0.32, 0.11, hardPhase));

    // Rigid phase adds cell seams and missing digital facets, but only inside
    // the material so the structure still grows from the liquid silhouette.
    vec2 cellFract = fract(uv * cellCount);
    float seamDistance = min(min(cellFract.x, 1.0 - cellFract.x),
                             min(cellFract.y, 1.0 - cellFract.y));
    float seam = (1.0 - smoothstep(0.0, 0.070, seamDistance))
               * hardPhase * bodyMask;
    float missingFacet = smoothstep(0.90, 0.985,
                                    hash21(cell + floor(tick * 0.5)))
                       * hardPhase * bodyMask;

    material = mix(material, shadow, seam * 0.58);
    material = mix(material, background, missingFacet * 0.72);

    vec3 color = mix(background, material, bodyMask);

    // A faint halo retains the lamp-like glow without filling the field.
    float halo = smoothstep(threshold - 0.16, threshold, field)
               * (1.0 - bodyMask);
    color += body * halo * mix(0.055, 0.018, hardPhase);

    if (u_feedbackEnabled > 0.5 && u_feedbackReady > 0.5) {
        vec2 memoryUv = uv;
        vec2 fluidDrift = vec2(0.0, -sourceTexel.y * mix(0.6, 2.7, u_k2));
        vec2 lockedUv = (floor(uv * cellCount) + 0.5) / cellCount;
        memoryUv = mix(memoryUv + fluidDrift, lockedUv, hardPhase);

        vec3 memory = readPrev(memoryUv);
        float memoryMix = mix(0.018, 0.155, u_k5)
                        * mix(0.72, 1.0, hardPhase);
        color = mix(color, memory, memoryMix);
    }

    // Trigger is a brief phase fracture, not a continuous audio wobble.
    if (fracture > 0.001) {
        float fractureBand = step(0.72,
                                  hash21(vec2(floor(uv.y * 24.0), tick + 4.0)));
        vec3 split = vec3(color.r,
                          color.g * mix(1.0, 0.72, fractureBand * fracture),
                          color.b);
        color = mix(color, split, fracture * 0.36);
    }

    float vignette = 1.0 - smoothstep(0.22, 0.92,
                                      length((uv - 0.5) * vec2(0.90, 1.18)));
    color *= mix(0.72, 1.0, vignette);
    color *= mix(0.90, 1.08, u_brightness);
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
    return clamp01(getv("peak", getv("audio_peak", 0.0)) * 1.25)
end

function setup()
    print("HAUTLLE_MELTLOCK_01 setup")

    generatorAvailable =
        type(meltlock_prepare) == "function" and
        type(meltlock_step) == "function"

    hostAvailable =
        type(shaderhost_load) == "function" and
        type(shaderhost_step) == "function" and
        type(shaderhost_draw) == "function"

    if not generatorAvailable then
        print("HAUTLLE_MELTLOCK_01 ERROR: native meltlock_* API missing")
        return
    end

    if not hostAvailable then
        print("HAUTLLE_MELTLOCK_01 ERROR: ShaderHost API missing")
        return
    end

    -- Native field state intentionally survives Lua reloads and mode changes.
    -- For a fresh seed during development, call meltlock_reset() explicitly.
    meltlock_prepare()

    if type(shaderhost_reset) == "function" then
        shaderhost_reset()
    end

    shaderLoaded = shaderhost_load(fragShader)
    frameTime = 0.0

    if shaderLoaded then
        print("HAUTLLE_MELTLOCK_01 shader loaded")

        if type(shaderhost_feedback_enable) == "function" then
            shaderhost_feedback_enable(true)
            print("HAUTLLE_MELTLOCK_01 ShaderHost feedback opt-in ON")
        end
    else
        print("HAUTLLE_MELTLOCK_01 ERROR: shader failed")
    end
end

function update()
end

function draw()
    local W = renderW or 1280
    local H = renderH or 720

    if not generatorAvailable or not hostAvailable or not shaderLoaded then
        of.setColor(38, 0, 9, 255)
        of.drawRectangle(0, 0, W, H)
        return
    end

    local dt = 1.0 / 30.0
    frameTime = frameTime + dt

    local k1 = clamp01(getv("knob1", 0.44))
    local k2 = clamp01(getv("knob2", 0.24))
    local k3 = clamp01(getv("knob3", 0.05))
    local k4 = clamp01(getv("knob4", 0.08))
    local k5 = clamp01(getv("knob5", 0.56))

    local energy = audio_energy()
    local hit = trig_now()

    meltlock_step(dt, energy, hit, k1, k2, k3, k4, k5)

    -- ShaderHost contract: time, energy, brightness, trig, k1..k5.
    -- Exposure is intentionally fixed; K5 remains dedicated to surface/memory.
    local exposure = 1.0
    shaderhost_step(frameTime, energy, exposure, hit, k1, k2, k3, k4, k5)
    shaderhost_draw()
end

function exit()
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    print("HAUTLLE_MELTLOCK_01 exit")
    print("script finished")
end
