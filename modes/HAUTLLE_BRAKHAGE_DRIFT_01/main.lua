-- HAUTLLE_BRAKHAGE_DRIFT_01 / revision 1.1
--
-- A calmer reconstruction of Brakhage Dreams -- HAUTLLE Drift v2.8:
-- "NO RINGS + HW PERSIST".
--
-- The frame is treated as painted emulsion, not a scene containing floating
-- digital objects. Correlated pigment fields, black voids, embedded smears,
-- grit, scratches, and brief splice mutations cover the film plane.
--
-- Dense music is deliberately rate-limited. Audio passes through slow envelope
-- followers into a tension accumulator; only a filled accumulator can fire a
-- mutation, and every mutation has a refractory period. Complex material such
-- as Autechre NTS therefore pressurizes the film instead of retriggering every
-- visual system on every transient.
--
-- NO circles / ellipses / arcs / radial fields.
-- NO FBO / ShaderHost / texture / native generator.
-- Normal EYESY Persist owns all frame memory.
--
-- K1 = FILM RATE       suspended evolution -> faster frame mutation
-- K2 = PIGMENT         broad sparse clumps -> detailed dense emulsion
-- K3 = AUDIO PRESSURE  nearly autonomous -> stronger tension and deformation
-- K4 = PALETTE         continuous travel through six painted-film palettes
-- K5 = EXPOSURE        dark transparent pigment -> luminous opaque pigment
-- TRIG = immediate splice mutation (bypasses the audio refractory period)

modeTitle = "Brakhage Dreams - HAUTLLE Drift 01"
modeDescrip = "Painted emulsion / slow audio pressure / splice mutation"
modeExplain = modeDescrip
titleK1 = "Film Rate"
titleK2 = "Pigment"
titleK3 = "Audio Pressure"
titleK4 = "Palette"
titleK5 = "Exposure"

print(modeTitle)
print("BRAKHAGE_DRIFT_01 r1.1: CALM GATES / NO RINGS / HW PERSIST")

inL = inL or {}
inR = inR or {}

local W, H = 1280, 720
local frame = 0
local time = 0.0

local lowFilter = 0.0
local fastFilter = 0.0
local bassEnv = 0.0
local midEnv = 0.0
local highEnv = 0.0
local energyEnv = 0.0
local lastEnergy = 0.0
local onsetEnv = 0.0

local tension = 0.0
local refractory = 0
local previousTrig = false
local fieldTarget = 0.0
local fieldPhase = 0.0
local colorKick = 0.0
local mutationAge = 999
local mutationDuration = 72
local mutationSeed = 0.0
local mutationStrength = 0.0

local smears = {}
local scratches = {}

local function clamp(x, a, b)
    x = tonumber(x) or 0.0
    if x < a then return a end
    if x > b then return b end
    return x
end

local function clamp01(x)
    return clamp(x, 0.0, 1.0)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function fract(x)
    return x - math.floor(x)
end

local function smooth01(x)
    x = clamp01(x)
    return x * x * (3.0 - 2.0 * x)
end

local function globalNumber(name, fallback)
    local value = _G[name]
    if type(value) == "number" then return value end
    if type(value) == "function" then
        local ok, result = pcall(value)
        if ok and type(result) == "number" then return result end
    end
    return fallback
end

local function globalBool(name)
    local value = _G[name]
    if type(value) == "boolean" then return value end
    if type(value) == "number" then return value > 0 end
    return false
end

local function hash2(x, y, seed)
    return fract(math.sin(x * 127.1 + y * 311.7 + (seed or 0.0) * 74.7) * 43758.5453)
end

local function valueNoise(x, y, seed)
    local ix = math.floor(x)
    local iy = math.floor(y)
    local fx = smooth01(fract(x))
    local fy = smooth01(fract(y))
    local a = hash2(ix, iy, seed)
    local b = hash2(ix + 1, iy, seed)
    local c = hash2(ix, iy + 1, seed)
    local d = hash2(ix + 1, iy + 1, seed)
    return lerp(lerp(a, b, fx), lerp(c, d, fx), fy)
end

local PALETTES = {
    {{248,31,43},{255,103,13},{255,211,42},{20,8,18},{255,235,184},{146,13,56}},
    {{7,216,216},{242,17,139},{248,227,30},{17,7,47},{66,77,249},{255,117,183}},
    {{226,199,250},{124,54,167},{235,38,70},{8,84,87},{252,230,185},{38,13,43}},
    {{174,250,24},{23,134,42},{250,75,14},{106,29,183},{250,234,70},{13,15,9}},
    {{237,226,191},{20,28,36},{15,77,154},{169,23,31},{201,166,89},{82,124,135}},
    {{252,242,210},{250,45,26},{17,19,80},{27,177,148},{239,146,8},{89,29,99}},
}

local function filmRgb(paletteU, slot, scale)
    paletteU = fract(paletteU or 0.0)
    slot = ((math.floor(slot or 1) - 1) % 6) + 1
    local p = paletteU * #PALETTES
    local aIndex = math.floor(p) + 1
    local bIndex = (aIndex % #PALETTES) + 1
    local mix = smooth01(p - math.floor(p))
    local a = PALETTES[aIndex][slot]
    local b = PALETTES[bIndex][slot]
    scale = scale or 1.0
    return
        math.floor(clamp(lerp(a[1], b[1], mix) * scale, 0, 255)),
        math.floor(clamp(lerp(a[2], b[2], mix) * scale, 0, 255)),
        math.floor(clamp(lerp(a[3], b[3], mix) * scale, 0, 255))
end

local function setFilmColor(paletteU, slot, alpha, scale)
    local r, g, b = filmRgb(paletteU, slot, scale)
    of.setColor(r, g, b, math.floor(clamp(alpha or 255, 0, 255)))
end

local function drawQuad(x1,y1,x2,y2,x3,y3,x4,y4)
    of.drawTriangle(x1,y1,x2,y2,x3,y3)
    of.drawTriangle(x1,y1,x3,y3,x4,y4)
end

local function waveformSample(index)
    local lc = (type(inL) == "table") and #inL or 0
    local rc = (type(inR) == "table") and #inR or 0
    local count = math.max(lc, rc)
    if count < 1 then return 0.0 end
    index = ((math.floor(index or 1) - 1) % count) + 1
    local l = (lc > 0) and (tonumber(inL[((index - 1) % lc) + 1]) or 0.0) or 0.0
    local r = (rc > 0) and (tonumber(inR[((index - 1) % rc) + 1]) or l) or l
    return clamp((l + r) * 0.5, -1.0, 1.0)
end

local function analyzeAudio()
    local lc = (type(inL) == "table") and #inL or 0
    local rc = (type(inR) == "table") and #inR or 0
    local count = math.min(112, math.max(lc, rc))
    if count < 1 then
        local fallback = clamp01(globalNumber("audio_peak", globalNumber("peak", 0.0)))
        return fallback, fallback * 0.7, fallback * 0.5, fallback, 0.0
    end

    local lowSum, midSum, highSum, total = 0.0, 0.0, 0.0, 0.0
    for i = 1, count do
        local x = waveformSample(i)
        lowFilter = lowFilter + (x - lowFilter) * 0.065
        fastFilter = fastFilter + (x - fastFilter) * 0.275
        local lo = lowFilter
        local mi = fastFilter - lowFilter
        local hi = x - fastFilter
        lowSum = lowSum + lo * lo
        midSum = midSum + mi * mi
        highSum = highSum + hi * hi
        total = total + x * x
    end
    local energy = math.sqrt(total / count)
    local onset = math.max(0.0, energy - lastEnergy)
    lastEnergy = lastEnergy + (energy - lastEnergy) * 0.18
    return
        clamp01(math.sqrt(lowSum / count) * 5.1),
        clamp01(math.sqrt(midSum / count) * 7.0),
        clamp01(math.sqrt(highSum / count) * 6.2),
        clamp01(energy * 2.8),
        clamp01(onset * 8.0)
end

local function buildMarks()
    smears = {}
    scratches = {}
    for i = 1, 30 do
        smears[i] = {
            x = math.random(), y = math.random(),
            length = 0.12 + math.random() * 0.54,
            width = 0.009 + math.random() * 0.064,
            angle = (math.random() * 2.0 - 1.0) * 1.25,
            phase = math.random() * 6.2831853,
            slot = math.random(1, 6),
        }
    end
    for i = 1, 42 do
        scratches[i] = {
            x = math.random(), y = math.random(),
            length = 0.04 + math.random() * 0.36,
            slant = (math.random() * 2.0 - 1.0) * 0.09,
            phase = math.random() * 6.2831853,
            slot = math.random(1, 6),
            width = 0.55 + math.random() * 1.35,
        }
    end
end


local function fireMutation(strength, forced)
    mutationAge = 0
    mutationDuration = forced and 88 or math.floor(lerp(54, 76, math.random()))
    mutationSeed = math.random() * 100.0
    mutationStrength = clamp01(strength)
    fieldTarget = fieldTarget + (forced and (1.6 + math.random() * 1.8) or (0.45 + strength * 0.85))
    colorKick = forced and ((math.random() < 0.5) and 0.50 or 0.1667) or (math.random() * 0.08)
    tension = forced and tension or 0.12
    if not forced then
        refractory = math.random(120, 240) -- two to four seconds at 60 fps
    end
end

local function mutationEnvelope()
    if mutationAge >= mutationDuration then return 0.0 end
    return math.sin((mutationAge / mutationDuration) * math.pi) * mutationStrength
end

local function drawBase(paletteU, exposure, persistOn)
    local r,g,b = filmRgb(paletteU, 4, lerp(0.11, 0.32, exposure))
    local alpha = persistOn and math.floor(lerp(5, 25, exposure)) or 255
    of.setColor(r,g,b,alpha)
    of.drawRectangle(0,0,W,H)
end

-- A correlated, nearly full-frame field. Adjacent cells share low-frequency
-- noise, so the result reads as clotted pigment rather than a digital grid.
local function drawPigmentField(paletteU, pigment, pressure, exposure, mutation)
    local cols = 13 + math.floor(pigment * 8)
    local rows = math.max(8, math.floor(cols * H / W))
    local cellW = W / cols
    local cellH = H / rows
    local scale = lerp(0.22, 0.48, pigment)
    local phase = fieldPhase
    local bucket = math.floor(frame / math.floor(lerp(13, 5, pigment)))
    local deform = (1.5 + pressure * 8.0) * (0.20 + midEnv * 0.55 + mutation * 0.65)
    local baseAlpha = lerp(54, 176, exposure) + pigment * 32

    for y = 0, rows - 1 do
        for x = 0, cols - 1 do
            local nx = x * scale + phase * 0.21
            local ny = y * scale - phase * 0.13
            local macro = valueNoise(nx, ny, 3.17)
            local vein = valueNoise(nx * 1.83 - phase * 0.08, ny * 1.67, 8.41)
            local grain = hash2(x, y, bucket * 0.017 + mutationSeed)
            local slot = math.floor(fract(macro * 2.7 + vein * 1.6 + colorKick) * 6) + 1

            -- Dark gaps are emulsion voids, not outlines around every cell.
            local alpha = baseAlpha * (0.72 + macro * 0.42)
            local valueScale = 0.76 + exposure * 0.42
            if vein < lerp(0.26, 0.16, pigment) then
                slot = 4
                alpha = alpha * 0.82
                valueScale = 0.40 + exposure * 0.20
            end

            local wave = waveformSample(x * 5 + y * 11 + bucket)
            local wobble = (grain - 0.5) * deform + wave * pressure * 2.8
            local left = x * cellW - cellW * 0.06
            local top = y * cellH - cellH * 0.06
            local right = (x + 1) * cellW + cellW * 0.06
            local bottom = (y + 1) * cellH + cellH * 0.06
            setFilmColor(paletteU, slot, alpha, valueScale)
            drawQuad(
                left + wobble, top - wobble * 0.35,
                right - wobble * 0.55, top + wobble,
                right + wobble * 0.30, bottom - wobble * 0.45,
                left - wobble * 0.40, bottom + wobble * 0.25
            )
        end
    end
end

local function drawEmbeddedSmears(paletteU, pigment, pressure, exposure, mutation)
    local count = 8 + math.floor(pigment * 22)
    for i = 1, count do
        local s = smears[i]
        local segments = 3 + math.floor(pigment * 3)
        local totalLength = s.length * W
        local dx = math.cos(s.angle)
        local dy = math.sin(s.angle)
        local px = -dy
        local py = dx
        local cx = (s.x + math.sin(fieldPhase * 0.12 + s.phase) * 0.025) * W
        local cy = (s.y + math.cos(fieldPhase * 0.09 + s.phase) * 0.020) * H
        for part = 1, segments do
            local u = (part - 0.5) / segments - 0.5
            local wave = waveformSample(i * 13 + part * 9)
            local bend = wave * pressure * (4.0 + midEnv * 16.0) + math.sin(s.phase + part * 1.7) * 2.0
            local x = cx + dx * totalLength * u + px * bend
            local y = cy + dy * totalLength * u + py * bend
            local len = totalLength / segments * 1.35
            local wid = s.width * H * (0.75 + math.sin(s.phase + part * 2.3) * 0.18)
            wid = wid * (1.0 + mutation * 0.35)
            local hx = dx * len * 0.5
            local hy = dy * len * 0.5
            local wx = px * wid * 0.5
            local wy = py * wid * 0.5
            setFilmColor(paletteU, s.slot + part, lerp(28,112,exposure) + mutation * 35, 0.9 + exposure * 0.25)
            drawQuad(x-hx+wx,y-hy+wy, x-hx-wx,y-hy-wy,
                     x+hx-wx*0.75,y+hy-wy*0.75, x+hx+wx*1.15,y+hy+wy*1.15)
        end
    end
end

local function drawGrit(paletteU, pigment, exposure, mutation)
    local count = 22 + math.floor(pigment * 82)
    local bucket = math.floor(frame / 7)
    for i = 1, count do
        local x = hash2(i, bucket, 2.1) * W
        local y = hash2(i, bucket, 7.9) * H
        local size = 0.7 + hash2(i, bucket, 13.7) * (2.5 + pigment * 3.5)
        local slot = math.floor(hash2(i, bucket, 19.2) * 6) + 1
        setFilmColor(paletteU, slot, 16 + exposure * 55 + mutation * 25, 1.05)
        if i % 3 == 0 then
            of.drawTriangle(x,y, x+size*1.8,y-size*0.35, x+size*0.4,y+size*1.5)
        else
            of.drawRectangle(x,y,math.max(1.0,size*0.55),math.max(1.0,size))
        end
    end
end

local function drawScratches(paletteU, pigment, pressure, exposure, mutation)
    local count = 8 + math.floor(pigment * 27)
    local audioLift = highEnv * pressure * 0.35
    for i = 1, count do
        local s = scratches[i]
        local segments = 3 + (i % 3)
        local x = s.x * W + math.sin(fieldPhase * 0.18 + s.phase) * W * 0.018
        local y = s.y * H
        local length = s.length * H
        of.setLineWidth(math.max(1.0, s.width + mutation * 0.8))
        setFilmColor(paletteU, s.slot, 16 + exposure * 62 + audioLift * 42 + mutation * 50, 1.08)
        local lastX, lastY = x, y - length * 0.5
        for part = 1, segments do
            local u = part / segments
            local jitter = (hash2(i, part, math.floor(frame / 9) + s.phase) - 0.5) * (3.0 + mutation * 9.0)
            local nextX = x + s.slant * W * (u - 0.5) + jitter
            local nextY = y - length * 0.5 + length * u
            of.drawLine(lastX,lastY,nextX,nextY)
            lastX,lastY = nextX,nextY
        end
    end
end

local function drawMutation(paletteU, pressure, exposure, mutation)
    if mutation <= 0.001 then return end
    local bars = 2 + math.floor(mutationStrength * 3)
    for i = 1, bars do
        local n = hash2(i, math.floor(mutationAge / 5), mutationSeed)
        local y = n * H
        local height = H * (0.014 + hash2(i, 4.3, mutationSeed) * 0.09)
        local shift = math.sin(mutationSeed + i * 2.1) * W * mutation * (0.03 + pressure * 0.06)
        setFilmColor(paletteU, i + 2, mutation * lerp(55,150,exposure), 1.14)
        of.drawRectangle(shift,y,W,height)
    end

    -- Only the first two frames can flare hard. The remainder is a slow stain.
    if mutationAge < 2 then
        local r,g,b = filmRgb(paletteU, 5, 1.12)
        of.setColor(r,g,b,math.floor(44 + exposure * 62))
        of.drawRectangle(0,0,W,H)
    end
end

function setup()
    W = renderW or of.getWidth() or 1280
    H = renderH or of.getHeight() or 720
    math.randomseed(os.time())
    for _ = 1,7 do math.random() end
    buildMarks()
    frame,time = 0,0.0
    fieldPhase,fieldTarget = 0.0,0.0
    tension,refractory = 0.0,0
    previousTrig = false
    mutationAge = 999
    if type(of.enableAlphaBlending) == "function" then of.enableAlphaBlending() end
    print("BRAKHAGE_DRIFT_01 setup: " .. tostring(W) .. "x" .. tostring(H))
end

function update()
end

function draw()
    W = renderW or of.getWidth() or W
    H = renderH or of.getHeight() or H
    frame = frame + 1
    time = time + 1.0 / 60.0
    if refractory > 0 then refractory = refractory - 1 end

    local k1 = clamp01(globalNumber("knob1",0.28))
    local k2 = clamp01(globalNumber("knob2",0.56))
    local k3 = clamp01(globalNumber("knob3",0.42))
    local k4 = clamp01(globalNumber("knob4",0.08))
    local k5 = clamp01(globalNumber("knob5",0.54))

    local bass,mid,high,energy,onset = analyzeAudio()
    -- Intentionally slow followers: dense transients cannot shake every layer.
    bassEnv = bassEnv + (bass - bassEnv) * 0.055
    midEnv = midEnv + (mid - midEnv) * 0.045
    highEnv = highEnv + (high - highEnv) * 0.060
    energyEnv = energyEnv + (energy - energyEnv) * 0.035
    onsetEnv = onsetEnv + (onset - onsetEnv) * ((onset > onsetEnv) and 0.18 or 0.035)

    local trigState = globalBool("trig")
    local trigEdge = trigState and not previousTrig
    previousTrig = trigState

    -- Audio fills pressure slowly and silence leaks it away. Refractory timing
    -- caps autonomous events at roughly one every 2-4 seconds, even at K3 max.
    local drive = math.max(0.0, energyEnv - 0.28) * 0.010
                + math.max(0.0, bassEnv - 0.44) * 0.006
                + math.max(0.0, onsetEnv - 0.24) * 0.004
    tension = clamp(tension + drive * k3 - lerp(0.0022,0.0007,k3), 0.0, 1.15)

    if trigEdge then
        fireMutation(1.0,true)
    elseif tension >= 1.0 and refractory <= 0 then
        fireMutation(0.42 + energyEnv * 0.38 + onsetEnv * 0.15,false)
    end

    local autonomousRate = lerp(0.0025,0.020,k1)
    fieldTarget = fieldTarget + autonomousRate
    -- Audio adds pressure to the destination but never directly teleports it.
    fieldTarget = fieldTarget + energyEnv * k3 * 0.0009
    fieldPhase = fieldPhase + (fieldTarget - fieldPhase) * lerp(0.008,0.026,k1)
    colorKick = colorKick * 0.975

    local mutation = mutationEnvelope()
    if mutationAge < mutationDuration then mutationAge = mutationAge + 1 end
    local paletteU = fract(k4 + colorKick + bassEnv * k3 * 0.018)
    local persistOn = globalBool("persist") and not globalBool("persistClear")

    drawBase(paletteU,k5,persistOn)
    drawPigmentField(paletteU,k2,k3,k5,mutation)
    drawEmbeddedSmears(paletteU,k2,k3,k5,mutation)
    drawGrit(paletteU,k2,k5,mutation)
    drawScratches(paletteU,k2,k3,k5,mutation)
    drawMutation(paletteU,k3,k5,mutation)

    of.setLineWidth(1.0)
    of.setColor(255,255,255,255)
end

function exit()
    of.setLineWidth(1.0)
    of.setColor(255,255,255,255)
    print("BRAKHAGE_DRIFT_01 exit")
    print("script finished")
end
