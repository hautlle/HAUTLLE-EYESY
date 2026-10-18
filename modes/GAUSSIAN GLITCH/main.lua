-- HAUTLLE_GAUSSIAN_GLITCH_REBUILT_V4_ORBIT
--
-- Clean-slate recreation of Gaussian Glitch Lab for the custom EYESY OF/Lua OS.
--
-- ARCHITECTURE:
--   * ShaderHost private feedback is explicitly OFF.
--   * No u_prevFrame.
--   * No private feedback FBO.
--   * No native texture carrier.
--   * Normal EYESY Persist supplies temporal memory.
--   * K1 controls a translucent canvas decay wash when Persist is ON.
--   * FAST shader path: no per-pixel sin/cos; cheap rounded waves instead.
--
-- K1 = Glass Memory       short trails -> long smeared trails
-- K2 = Glitch Energy      slice tears / bars / displacement
-- K3 = Ribbons            density / width / curvature
-- K4 = Accent Hue         ribbon / fringe color
-- K5 = Background         dark background palette
-- TRIG = glitch burst

require("eyesy")

modeTitle = "Gaussian Glitch Rebuilt V4 Orbit"
titleK1 = "Glass Memory"
titleK2 = "Glitch"
titleK3 = "Ribbons"
titleK4 = "Accent"
titleK5 = "Background"

print(modeTitle)
print("GAUSSIAN_REBUILT_V4_ORBIT: 640x360 GPU FBO / SPINNING RIBBONS")

local hostAvailable = false
local shaderLoaded = false

local frameTime = 0.0
local previousTrigger = false
local triggerPulse = 0.0
local smoothEnergy = 0.0

local knobsPrimed = false
local sk1 = 0.0
local sk2 = 0.0
local sk3 = 0.0
local sk4 = 0.0
local sk5 = 0.0

local fragShader = [[
#ifdef GL_ES
precision mediump float;
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

float sat(float x)
{
    return clamp(x, 0.0, 1.0);
}

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Cheap rounded triangle-wave approximation.  This replaces the many
// per-pixel sin/cos calls from V1 while keeping the writhing/wavy character.
float cheapWave(float x)
{
    float f = fract(x * 0.15915494309189535 + 0.5);
    float y = 1.0 - 4.0 * abs(f - 0.5);
    return y * (1.5 - 0.5 * abs(y));
}

vec3 hsv2rgb(vec3 c)
{
    vec3 p = abs(fract(c.xxx + vec3(0.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// Cheap full-circle rotation basis. cheapWave() acts as a rounded cosine;
// the quarter-cycle offset acts as sine. Normalizing removes the small scale
// breathing of the approximation without bringing sin()/cos() back.
vec2 cheapUnitCircle(float angle)
{
    float c = cheapWave(angle);
    float s = cheapWave(angle - 1.57079632679);
    float invLen = inversesqrt(max(c * c + s * s, 0.0001));
    return vec2(c, s) * invLen;
}

// Rotate into a moving ribbon-family coordinate system.
// cs = vec2(cos-ish, sin-ish). This is the inverse rotation, so a ribbon
// drawn in this local frame appears to rotate positively in screen space.
vec2 rotateIntoFrame(vec2 p, vec2 cs)
{
    return vec2(
        cs.x * p.x + cs.y * p.y,
       -cs.y * p.x + cs.x * p.y
    );
}

vec2 ribbonY(
    vec2 p,
    vec2 center,
    float slope,
    float phase,
    float bend,
    float width,
    float glowWidth)
{
    vec2 q = p - center;
    float x = q.x;

    float wave =
        cheapWave(x * (6.0 + bend * 7.0) + phase) *
        (0.020 + 0.058 * bend);

    wave +=
        cheapWave(x * 14.0 - phase * 0.61) *
        (0.005 + 0.013 * bend);

    float d = abs(q.y - slope * x - wave);

    float core = 1.0 - smoothstep(width, width + 0.0045, d);
    float glow = 1.0 - smoothstep(width, width + glowWidth, d);

    return vec2(core, glow);
}

vec2 ribbonX(
    vec2 p,
    vec2 center,
    float slope,
    float phase,
    float bend,
    float width,
    float glowWidth)
{
    vec2 q = p - center;
    float y = q.y;

    float wave =
        cheapWave(y * (6.0 + bend * 7.0) + phase) *
        (0.020 + 0.058 * bend);

    wave +=
        cheapWave(y * 14.0 - phase * 0.61) *
        (0.005 + 0.013 * bend);

    float d = abs(q.x - slope * y - wave);

    float core = 1.0 - smoothstep(width, width + 0.0045, d);
    float glow = 1.0 - smoothstep(width, width + glowWidth, d);

    return vec2(core, glow);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;

    vec2 p = uv * 2.0 - 1.0;
    p.x *= u_resolution.x / max(u_resolution.y, 1.0);

    float t = u_time;
    float audio = sat(u_energy);
    float hit = sat(u_trig);

    float memory = sat(u_k1);
    float glitch = sat(u_k2);
    float ribbonsK = sat(u_k3);

    float activity = sat(glitch * 0.58 + audio * 0.82 + hit * 0.88);

    float glitchRate = mix(1.4, 11.0, glitch) + audio * 3.0;
    float tick = floor(t * glitchRate);

    vec2 q = p;

    // Quantized horizontal decode tears.
    float row = floor(uv.y * mix(9.0, 25.0, glitch));
    float rowRnd = hash11(row * 19.17 + tick * 7.31);
    float rowGate = step(0.80 - activity * 0.35, rowRnd);
    float rowDir = hash11(row * 5.27 + tick * 11.7) * 2.0 - 1.0;
    q.x += rowGate * rowDir * activity * mix(0.010, 0.145, glitch);

    // Occasional vertical address fracture.
    float col = floor(uv.x * 17.0);
    float colRnd = hash11(col * 13.1 + tick * 3.77 + 2.0);
    float colGate = step(0.91 - activity * 0.18, colRnd);
    q.y += colGate * (colRnd - 0.5) * activity * 0.085;

    // Cold-glass coordinate breathing, using cheapWave instead of sin/cos.
    float glassAmount = memory * (0.010 + 0.024 * activity);
    q.x += cheapWave(p.y * 8.5 + t * 0.37) * glassAmount;
    q.y += cheapWave(p.x * 6.8 - t * 0.29) * glassAmount * 0.65;

    float width = mix(0.0038, 0.0145, ribbonsK);
    float glowWidth = mix(0.024, 0.085, memory);
    float bend = mix(0.18, 1.0, ribbonsK);

    // ------------------------------------------------------------------
    // ORBITING / SPINNING RIBBON FAMILIES
    // ------------------------------------------------------------------
    // Restore the old Gaussian mode's defining motion: the dominant ribbon
    // pair continuously rotates through a full circle while the secondary
    // pair counter-rotates more slowly.  Fixed offsets in each rotating local
    // frame become true orbital motion around screen center.
    //
    // Important for the CM3: this uses cheapWave() + normalization instead of
    // GLSL sin()/cos(), so we keep the 640x360 performance win.
    float mainAngle = t * 0.30;
    float counterAngle = -t * 0.22 + 0.72;

    vec2 mainCS = cheapUnitCircle(mainAngle);
    vec2 counterCS = cheapUnitCircle(counterAngle);

    vec2 qMain = rotateIntoFrame(q, mainCS);
    vec2 qCounter = rotateIntoFrame(q, counterCS);

    // Primary ribbon: spins around the center like the old r0 axisRot line.
    vec2 r0 = ribbonY(
        qMain,
        vec2(0.0, 0.0),
        0.70,
        t * 0.76,
        bend,
        width,
        glowWidth
    );

    vec2 r1 = vec2(0.0);
    vec2 r2 = vec2(0.0);
    vec2 r3 = vec2(0.0);

    if (ribbonsK > 0.14)
    {
        // Offset center lives inside qMain, so the whole ribbon physically
        // orbits the screen center while countering r0's diagonal.
        vec2 orbit1 = vec2(
            -0.27 + cheapWave(t * 0.19 + 0.8) * 0.045,
             0.11 + cheapWave(t * 0.17 + 2.1) * 0.035
        );

        r1 = ribbonY(
            qMain,
            orbit1,
            -0.70,
            t * 0.63 + 2.3,
            bend * 0.88,
            width * 0.92,
            glowWidth * 0.92
        );
    }

    if (ribbonsK > 0.38)
    {
        vec2 orbit2 = vec2(
             0.30 + cheapWave(t * 0.13 + 4.0) * 0.040,
            -0.10 + cheapWave(t * 0.16 + 0.7) * 0.040
        );

        r2 = ribbonX(
            qCounter,
            orbit2,
            0.34,
            t * 0.55 + 4.2,
            bend * 0.78,
            width * 0.84,
            glowWidth * 0.86
        );
    }

    if (ribbonsK > 0.66)
    {
        vec2 orbit3 = vec2(
            -0.21 + cheapWave(t * 0.11 + 1.0) * 0.035,
             0.20 + cheapWave(t * 0.14 + 3.0) * 0.035
        );

        r3 = ribbonX(
            qCounter,
            orbit3,
            -0.54,
            t * 0.47 + 1.1,
            bend * 0.70,
            width * 0.78,
            glowWidth * 0.80
        );
    }

    float reveal1 = smoothstep(0.16, 0.32, ribbonsK);
    float reveal2 = smoothstep(0.40, 0.58, ribbonsK);
    float reveal3 = smoothstep(0.68, 0.84, ribbonsK);

    float core = sat(r0.x + r1.x * reveal1 + r2.x * reveal2 + r3.x * reveal3);
    float glow = sat(r0.y + r1.y * reveal1 + r2.y * reveal2 + r3.y * reveal3);

    // Horizontal raster bars / decoded packet bands.
    float barCount = mix(18.0, 42.0, glitch);
    float barCell = floor(uv.y * barCount);
    float barRnd = hash11(barCell * 31.7 + tick * 5.19);
    float barGate = step(0.86 - activity * 0.28, barRnd);

    float localY = fract(uv.y * barCount);
    float barShape = 1.0 - smoothstep(0.08, 0.19, abs(localY - 0.5));
    float bars = barGate * barShape;

    // Sparse packet blocks.
    vec2 blockGrid = floor(uv * vec2(22.0, 13.0));
    float blockRnd = hash21(blockGrid + vec2(tick, tick * 0.37));
    float blocks = step(0.972 - activity * 0.020, blockRnd);

    // Cheap scan texture: no trigonometry.
    float scanPhase = fract(gl_FragCoord.y * 0.1875 + tick * 0.03125);
    float scan = step(0.83, scanPhase);

    vec3 accent = hsv2rgb(vec3(fract(u_k4), 0.74, 1.0));
    vec3 hot = hsv2rgb(vec3(fract(u_k4 + 0.055), 0.86, 1.0));
    vec3 cold = hsv2rgb(vec3(fract(u_k4 + 0.91), 0.62, 1.0));

    float fringe = 0.5 + 0.5 * cheapWave(q.x * 11.0 + q.y * 7.0 + t * 0.44);
    vec3 ribbonColor = mix(cold, hot, fringe);
    ribbonColor = mix(ribbonColor, accent, 0.60);

    vec3 color = vec3(0.0);

    color += ribbonColor * glow * mix(0.10, 0.38, memory);
    color += ribbonColor * core * (0.72 + ribbonsK * 0.42);
    color += vec3(0.74, 0.86, 1.0) * core * 0.20;

    color += mix(cold, accent, barRnd) * bars * (0.18 + activity * 0.72);
    color += hot * blocks * (0.12 + hit * 0.55 + activity * 0.24);
    color += accent * scan * activity * 0.020;

    float grain = hash21(gl_FragCoord.xy + vec2(tick * 13.0, floor(t * 31.0))) - 0.5;
    color += vec3(grain) * (0.009 + activity * 0.030);

    // Mild contrast and vignette.
    color = mix(
        color,
        color * color * (3.0 - 2.0 * color),
        0.10 + memory * 0.18
    );

    vec2 vig = uv - 0.5;
    color *= 1.0 - dot(vig, vig) * 0.34;
    color = clamp(color, 0.0, 1.0);

    float alpha = 0.0;
    alpha += glow * mix(0.10, 0.30, memory);
    alpha += core * 0.62;
    alpha += bars * (0.12 + activity * 0.28);
    alpha += blocks * 0.18;
    alpha += scan * activity * 0.010;
    alpha = clamp(alpha, 0.0, 0.88);

    gl_FragColor = vec4(color * u_brightness, alpha);
}
]]

local function clamp01(x)
    x = tonumber(x) or 0.0
    if x > 1.0 then x = x / 1023.0 end
    if x < 0.0 then return 0.0 end
    if x > 1.0 then return 1.0 end
    return x
end

local function getNumber(name, fallback)
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

local function readKnob(index, fallback)
    local v = getNumber("knob" .. tostring(index), nil)
    if v == nil then
        v = getNumber("k" .. tostring(index), nil)
    end
    if type(v) ~= "number" then
        return fallback
    end
    return clamp01(v)
end

local function smoothValue(previous, target, amount)
    return previous + (target - previous) * amount
end

local function audioEnergy()
    local e = clamp01(getNumber("audio_peak", getNumber("peak", 0.0)))

    if e <= 0.0001 then
        e = clamp01(getNumber("energy", 0.0))
    end

    -- Moderate lift for line-level material without turning every transient
    -- into a full-screen flash.
    e = clamp01(e * 1.65)

    return e
end

local function triggerState()
    local v = _G["trig"]

    if type(v) == "boolean" then
        return v
    end

    if type(v) == "number" then
        return v > 0.0
    end

    return false
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local bgPalette = {
    {  2,  3,  6 },
    { 10,  4, 15 },
    {  3, 11, 15 },
    { 15,  6,  4 },
    {  7,  4, 12 }
}

local function backgroundColor(k)
    local scaled = clamp01(k) * 4.0
    local index = math.floor(scaled) + 1
    local frac = scaled - math.floor(scaled)

    if index >= 5 then
        index = 4
        frac = 1.0
    end

    local a = bgPalette[index]
    local b = bgPalette[index + 1]

    return
        math.floor(lerp(a[1], b[1], frac) + 0.5),
        math.floor(lerp(a[2], b[2], frac) + 0.5),
        math.floor(lerp(a[3], b[3], frac) + 0.5)
end

local function drawFallback()
    if of and type(of.setColor) == "function" and type(of.drawRectangle) == "function" then
        local W = renderW or 1280
        local H = renderH or 720
        of.setColor(70, 0, 25, 255)
        of.drawRectangle(0, 0, W, H)
    end
end

function setup()
    print("GAUSSIAN_REBUILT_V4_ORBIT setup")

    hostAvailable =
        type(shaderlow_load) == "function" and
        type(shaderlow_step) == "function" and
        type(shaderlow_draw) == "function" and
        type(shaderlow_scale) == "function"

    if not hostAvailable then
        print("GAUSSIAN_REBUILT_V4_ORBIT ERROR: shaderlow API missing -- bridge patch not installed")
        return
    end

    -- Clean up the old ShaderHost feedback opt-in if a previous mode left it on.
    if type(shaderhost_feedback_enable) == "function" then
        shaderhost_feedback_enable(false)
    end

    shaderlow_reset()

    -- 0.5 = 640x360 when the EYESY canvas is 1280x720.
    -- Try 0.375 later for 480x270 if we want an even more aggressive speed mode.
    shaderlow_scale(0.25)

    shaderLoaded = shaderlow_load(fragShader)

    if shaderLoaded then
        print("GAUSSIAN_REBUILT_V4_ORBIT shader loaded OK")
        print("GAUSSIAN_REBUILT_V4_ORBIT render scale 0.5")
        print("GAUSSIAN_REBUILT_V4_ORBIT normal EYESY Persist supplies memory")
    else
        print("GAUSSIAN_REBUILT_V4_ORBIT ERROR: shader compile/link failed")
    end

    frameTime = 0.0
    previousTrigger = false
    triggerPulse = 0.0
    smoothEnergy = 0.0
    knobsPrimed = false
end

function update()
end

function draw()
    local W = renderW or 1280
    local H = renderH or 720

    if not hostAvailable or not shaderLoaded then
        drawFallback()
        return
    end

    -- Explicit visual clock. No os.clock(), no feedback-frame clock dependency.
    frameTime = frameTime + (1.0 / 30.0)

    local k1 = readKnob(1, 0.56)
    local k2 = readKnob(2, 0.34)
    local k3 = readKnob(3, 0.52)
    local k4 = readKnob(4, 0.58)
    local k5 = readKnob(5, 0.10)

    if not knobsPrimed then
        sk1, sk2, sk3, sk4, sk5 = k1, k2, k3, k4, k5
        knobsPrimed = true
    else
        sk1 = smoothValue(sk1, k1, 0.24)
        sk2 = smoothValue(sk2, k2, 0.23)
        sk3 = smoothValue(sk3, k3, 0.22)
        sk4 = smoothValue(sk4, k4, 0.30)
        sk5 = smoothValue(sk5, k5, 0.28)
    end

    local rawEnergy = audioEnergy()
    smoothEnergy = smoothEnergy + (rawEnergy - smoothEnergy) * 0.16

    local trigNow = triggerState()

    if trigNow and not previousTrigger then
        triggerPulse = 1.0
    else
        triggerPulse = triggerPulse * 0.89
    end

    previousTrigger = trigNow

    if triggerPulse < 0.004 then
        triggerPulse = 0.0
    end

    -- ------------------------------------------------------------------
    -- NORMAL EYESY CANVAS MEMORY
    -- ------------------------------------------------------------------
    -- Persist OFF / clear request:
    --     opaque dark background every frame.
    --
    -- Persist ON:
    --     translucent dark wash every frame.
    --     K1 controls decay: low K1 clears quickly, high K1 retains trails.
    -- ------------------------------------------------------------------

    local bgR, bgG, bgB = backgroundColor(sk5)

    local washAlpha

    if not persist or persistClear then
        washAlpha = 255
    else
        local memoryCurve = sk1 * sk1
        washAlpha = math.floor(lerp(54.0, 3.0, memoryCurve) + 0.5)
    end

    of.setColor(bgR, bgG, bgB, washAlpha)
    of.drawRectangle(0, 0, W, H)

    shaderlow_step(
        frameTime,
        smoothEnergy,
        1.0,
        triggerPulse > 0.04,
        sk1,
        sk2,
        sk3,
        sk4,
        sk5
    )

    shaderlow_draw()
end

function exit()
    print("GAUSSIAN_REBUILT_V4_ORBIT exit")
    print("script finished")
end