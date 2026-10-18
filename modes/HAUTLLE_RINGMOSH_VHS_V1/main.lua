-- HAUTLLE_RINGMOSH_VHS_01
--
-- Pure-Lua / direct-openFrameworks refresh of the Pygame RingMosh_VHS line.
-- The target is the full v1.1.0 "pops & bursts" character, rendered as GPU
-- geometry instead of Pygame surface copies. No ShaderHost, native generator,
-- private FBO, image asset, or require("eyesy") dependency.
--
-- Lineage retained:
--   * v1.0.2 band layout, now drawn from the live audio waveform
--   * K5 triangular morph windows centered at 0.00 / 0.33 / 0.66
--   * mid-driven tile jitter and high-driven ring shimmer
--   * bass-spawned rings and brief high-frequency mirror flashes
--   * v1.1.0 26-frame implode/explode pops, scene zoom/shake, palette flip
--
-- CONTROLS
--   K1 = Band Speed
--   K2 = Tile / Ring Density
--   K3 = Warp
--   K4 = Palette
--   K5 = Bands -> Tile Mosh -> Ring Field
--   TRIG = force a full ring pop + mirrored palette flash
--   PERSIST = normal EYESY trails; K3 increases cleanup slightly

modeTitle = "HAUTLLE RingMosh VHS 01"
modeDescrip = "Live waveforms / tile mosh / segmented ring pops"
titleK1 = "Band Speed"
titleK2 = "Density"
titleK3 = "Warp"
titleK4 = "Palette"
titleK5 = "Scene Morph"

print(modeTitle)
print("RINGMOSH_VHS_01: full pops & bursts / pure Lua GPU geometry")

-- The oF runner updates these tables in place before every draw call.
-- require("eyesy") normally creates them; this standalone mode must do it.
inL = inL or {}
inR = inR or {}

-- -----------------------------------------------------------------------------
-- Original-family constants
-- -----------------------------------------------------------------------------

local TAU = math.pi * 2.0

local BANDS_ROWS = 5
local WAVE_SEGMENTS = 128

local TILES_MIN = 22
local TILES_MAX = 66
local TILE_SUBSET = 0.33

local RINGS_BASE_MIN = 7
local RINGS_BASE_MAX = 31
local RINGS_BURST_MAX = 44
local RING_SEGMENTS = 16
local RING_THICK = 5.0
local RING_BASE_RADIUS = 14.0
local RING_RADIUS_JITTER = 33.0
local RING_SHIMMER = 0.33
local RING_WARP = 0.22

local BASS_RING_THRESHOLD = 0.77
local BASS_RING_COOLDOWN = 20       -- ~0.33 sec at 60 fps
local POP_THRESHOLD = 0.88
local POP_COOLDOWN = 75             -- 1.25 sec at 60 fps
local POP_DURATION = 26
local ZOOM_MAX_EXTRA = 0.15
local SHAKE_MAX = 4.0

-- -----------------------------------------------------------------------------
-- State
-- -----------------------------------------------------------------------------

local W, H = 1280, 720
local frame = 0
local time = 0.0

local tiles = {}
local rings = {}
local nextRing = 1

local bassEnv = 0.0
local midEnv = 0.0
local highEnv = 0.0
local audioSlow = 0.0
local audioFast = 0.0

local previousTrig = false
local bassWasHot = false
local popWasHot = false
local bassRingCooldown = 0
local popCooldown = 0
local mirrorFrames = 0

local popAge = POP_DURATION + 1
local popDirection = 1.0
local popSeed = 0.0
local respawnAfterPop = false

-- -----------------------------------------------------------------------------
-- Utilities
-- -----------------------------------------------------------------------------

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

local function smoothstep01(x)
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

local function triWeight(x, center, width)
    local distance = math.abs(x - center)
    if distance >= width then return 0.0 end
    return 1.0 - distance / width
end

-- Five loud analog-video anchors. K4 moves continuously around the loop.
local PALETTE = {
    {255,  35, 126},
    {255, 112,  28},
    {250, 231,  52},
    { 54, 238, 183},
    { 48, 168, 255},
    {171,  74, 255},
}

local function paletteRgb(u, offset, valueScale)
    u = fract((u or 0.0) + (offset or 0.0))
    local count = #PALETTE
    local position = u * count
    local index = math.floor(position) + 1
    local mix = position - math.floor(position)
    local nextIndex = (index % count) + 1
    local a = PALETTE[index]
    local b = PALETTE[nextIndex]
    local scale = valueScale or 1.0
    return
        math.floor(lerp(a[1], b[1], mix) * scale),
        math.floor(lerp(a[2], b[2], mix) * scale),
        math.floor(lerp(a[3], b[3], mix) * scale)
end

local function setPaletteColor(u, offset, alpha, valueScale)
    local r, g, b = paletteRgb(u, offset, valueScale)
    of.setColor(r, g, b, math.floor(clamp(alpha or 255, 0, 255)))
end

local function triggerNow()
    return globalBool("trig")
end

-- -----------------------------------------------------------------------------
-- Audio: cheap pseudo-bands from the current time-domain buffer
-- -----------------------------------------------------------------------------

local function analyzeAudio()
    local lowSum, midSum, highSum = 0.0, 0.0, 0.0
    local source = inL
    local available = (type(source) == "table") and #source or 0
    local count = math.min(100, available)

    if count <= 0 then
        local fallback = clamp01(globalNumber("audio_peak", globalNumber("peak", 0.0)))
        return fallback, fallback * 0.80, fallback * 0.60
    end

    for i = 1, count do
        local x = tonumber(source[i]) or 0.0
        x = clamp(x, -1.0, 1.0)

        audioSlow = audioSlow + (x - audioSlow) * 0.075
        audioFast = audioFast + (x - audioFast) * 0.28

        local low = audioSlow
        local mid = audioFast - audioSlow
        local high = x - audioFast

        lowSum = lowSum + low * low
        midSum = midSum + mid * mid
        highSum = highSum + high * high
    end

    local bass = clamp01(math.sqrt(lowSum / count) * 5.5)
    local mid = clamp01(math.sqrt(midSum / count) * 8.0)
    local high = clamp01(math.sqrt(highSum / count) * 7.0)
    return bass, mid, high
end

-- -----------------------------------------------------------------------------
-- Stable tile and ring layouts
-- -----------------------------------------------------------------------------

local function rebuildTiles()
    tiles = {}
    for i = 1, TILES_MAX do
        tiles[i] = {
            x = math.random(),
            y = math.random(),
            w = 0.025 + math.random() * 0.135,
            h = 0.018 + math.random() * 0.105,
            dir = (math.random() < 0.5) and -1.0 or 1.0,
            phase = math.random() * TAU,
            color = math.random(),
            block = math.random(0, 5),
        }
    end
end

local function makeRing(index)
    local cell = math.random(0, 11)
    local col = cell % 4
    local row = math.floor(cell / 4)
    local cellW = W / 4.0
    local cellH = H / 3.0

    return {
        x = (col + 0.18 + math.random() * 0.64) * cellW,
        y = (row + 0.18 + math.random() * 0.64) * cellH,
        radius = RING_BASE_RADIUS + math.random() * RING_RADIUS_JITTER,
        phase = math.random() * TAU,
        spin = (math.random() * 2.0 - 1.0) * (0.006 + math.random() * 0.018),
        wobble = 1.5 + math.random() * 4.5,
        color = math.random(),
        seed = (index or 1) * 0.731 + math.random() * 5.0,
    }
end

local function rebuildRings(count)
    rings = {}
    local total = count or RINGS_BASE_MAX
    for i = 1, total do
        rings[i] = makeRing(i)
    end
    nextRing = 1
end

local function addBurstRing()
    if #rings < RINGS_BURST_MAX then
        rings[#rings + 1] = makeRing(#rings + 1)
    else
        rings[nextRing] = makeRing(nextRing)
        nextRing = (nextRing % RINGS_BURST_MAX) + 1
    end
end

-- -----------------------------------------------------------------------------
-- Pop state
-- -----------------------------------------------------------------------------

local function startPop(force)
    if (not force) and popCooldown > 0 then return end

    popAge = 0
    popDirection = (math.random() < 0.50) and -1.0 or 1.0
    popSeed = math.random() * TAU
    respawnAfterPop = true
    mirrorFrames = force and 2 or 1
    popCooldown = POP_COOLDOWN

    -- A full pop throws several new rings into the field immediately.
    local additions = force and 7 or 4
    for _ = 1, additions do addBurstRing() end
end

local function currentPop()
    if popAge >= POP_DURATION then return 0.0 end
    local normalized = popAge / POP_DURATION
    return math.sin(normalized * math.pi)
end

-- -----------------------------------------------------------------------------
-- Draw layers
-- -----------------------------------------------------------------------------

local function waveformSample(index, sampleCount)
    if sampleCount <= 0 then return 0.0 end
    index = ((index - 1) % sampleCount) + 1

    local left = tonumber(inL[index]) or 0.0
    local rightCount = (type(inR) == "table") and #inR or 0
    local value
    if rightCount > 0 then
        local rightIndex = ((index - 1) % rightCount) + 1
        local right = tonumber(inR[rightIndex]) or left
        value = (left + right) * 0.5
    else
        value = left
    end
    return clamp(value, -1.0, 1.0)
end

local function drawBands(paletteU, k1, k3, mid, pop, alphaScale)
    local leftCount = (type(inL) == "table") and #inL or 0
    local rightCount = (type(inR) == "table") and #inR or 0
    local sampleCount = math.max(leftCount, rightCount)
    local segments = math.min(WAVE_SEGMENTS, math.max(1, sampleCount - 1))
    local speed = lerp(0.20, 1.80, k1)

    -- Normalize display height without changing the waveform's actual shape.
    -- True silence remains a flat line.
    local peak = 0.0
    for i = 1, sampleCount do
        peak = math.max(peak, math.abs(waveformSample(i, sampleCount)))
    end
    local autoGain = math.min(4.0, 0.78 / math.max(peak, 0.08))
    local amplitude = H * (0.034 + k3 * 0.038) * autoGain
    local baseWidth = 1.5 + k3 * 5.0 + mid * 1.8 + pop * 2.5
    local bleed = 2.0 + k3 * 7.0

    for row = 1, BANDS_ROWS do
        local baseY = (row / (BANDS_ROWS + 1)) * H
        -- K1 rolls the live buffer sideways; each row reads a different phase
        -- of the same captured sound rather than inventing a sine wave.
        local phaseShift = 0
        if sampleCount > 0 then
            phaseShift = math.floor(time * speed * sampleCount * 0.15
                                    + (row - 1) * sampleCount * 0.11) % sampleCount
        end

        local function pointAt(part)
            local x = (part / segments) * W
            if sampleCount <= 0 then return x, baseY end
            local sourceIndex = math.floor((part / segments) * (sampleCount - 1)) + 1
            local sample = waveformSample(sourceIndex + phaseShift, sampleCount)
            local y = baseY + sample * amplitude
            -- A pop briefly pulls the complete captured trace toward or away
            -- from center without replacing its audio-derived contour.
            y = y + (y - H * 0.5) * pop * popDirection * 0.11
            return x, y
        end

        -- VHS chroma ghost: the exact same waveform, spatially displaced.
        of.setLineWidth(math.max(1.0, baseWidth * 0.62))
        local cr, cg, cb = paletteRgb(paletteU, 0.22 + row * 0.035, 0.72)
        of.setColor(cr, cg, cb, math.floor(90 * alphaScale))
        local x1, y1 = pointAt(0)
        for part = 1, segments do
            local x2, y2 = pointAt(part)
            of.drawLine(x1 - bleed, y1 + bleed * 0.35,
                        x2 - bleed, y2 + bleed * 0.35)
            x1, y1 = x2, y2
        end

        -- Main live oscilloscope trace.
        of.setLineWidth(baseWidth)
        setPaletteColor(paletteU, row * 0.075, 225 * alphaScale, 1.0)
        x1, y1 = pointAt(0)
        for part = 1, segments do
            local x2, y2 = pointAt(part)
            of.drawLine(x1, y1, x2, y2)
            x1, y1 = x2, y2
        end
    end
end

local function drawTiles(paletteU, k2, k3, mid, high, pop, alphaScale)
    local available = math.floor(lerp(TILES_MIN, TILES_MAX, k2) + 0.5)
    local subsetPeriod = math.max(1, math.floor(1.0 / TILE_SUBSET + 0.5))
    local subsetPhase = math.floor(frame / 3) % subsetPeriod
    local gridCols = math.floor(7 + k2 * 9)
    local gridRows = math.max(4, math.floor(gridCols * H / W))
    local cellW = W / gridCols
    local cellH = H / gridRows
    local jitter = (2.0 + 25.0 * k3) * (0.20 + mid * 1.25 + pop * 0.85)

    for i = 1, available do
        if ((i + subsetPhase) % subsetPeriod) == 0 then
            local tile = tiles[i]
            local gx = math.floor(tile.x * gridCols)
            local gy = math.floor(tile.y * gridRows)
            local baseX = gx * cellW
            local baseY = gy * cellH

            local wobble = math.sin(time * 2.1 + tile.phase) * jitter * 0.22
            local shift = tile.dir * jitter * (0.35 + tile.color * 0.65) + wobble
            local radialX = (baseX - W * 0.5) * pop * popDirection * 0.13
            local radialY = (baseY - H * 0.5) * pop * popDirection * 0.08

            local widthCells = 0.55 + tile.w * gridCols * 0.72
            local heightCells = 0.40 + tile.h * gridRows * 0.88
            local tw = math.max(4.0, cellW * widthCells)
            local th = math.max(3.0, cellH * heightCells)
            local x = baseX + shift + radialX
            local y = baseY + math.sin(tile.phase + frame * 0.025) * jitter * 0.18 + radialY

            local alpha = (92 + 115 * k2 + high * 28) * alphaScale
            setPaletteColor(paletteU, tile.color * 0.45 + tile.block * 0.025, alpha, 0.94)
            of.drawRectangle(x, y, tw, th)

            -- Thin displaced echo gives the blocks their VHS/mosh edge.
            if k3 > 0.12 then
                setPaletteColor(paletteU, tile.color * 0.45 + 0.50, 42 * k3 * alphaScale, 0.86)
                of.drawRectangle(x - shift * 0.38, y + 1.0 + k3 * 3.0, tw, math.max(1.0, th * 0.16))
            end
        end
    end
end

local function drawRingSegment(cx, cy, radius, angle1, angle2, warp, phase)
    local midAngle = (angle1 + angle2) * 0.5
    local r1 = radius * (1.0 + math.sin(angle1 * 3.0 + phase) * warp)
    local r2 = radius * (1.0 + math.sin(angle2 * 3.0 + phase) * warp)
    local rm = radius * (1.0 + math.sin(midAngle * 3.0 + phase) * warp)

    local x1 = cx + math.cos(angle1) * r1
    local y1 = cy + math.sin(angle1) * r1
    local xm = cx + math.cos(midAngle) * rm
    local ym = cy + math.sin(midAngle) * rm
    local x2 = cx + math.cos(angle2) * r2
    local y2 = cy + math.sin(angle2) * r2

    of.drawLine(x1, y1, xm, ym)
    of.drawLine(xm, ym, x2, y2)
end

local function drawRings(paletteU, k2, k3, bass, high, pop, alphaScale)
    local baseCount = math.floor(lerp(RINGS_BASE_MIN, RINGS_BASE_MAX, k2) + 0.5)
    local burstLimit = math.floor(lerp(11.0, RINGS_BURST_MAX, k2) + 0.5)
    local drawCount = math.min(#rings, math.max(baseCount, burstLimit))
    local gapFraction = 0.18
    local segmentAngle = TAU / RING_SEGMENTS
    local warpAmount = math.min(0.20, k3 * RING_WARP)

    for i = 1, drawCount do
        local ring = rings[i]
        ring.phase = ring.phase + ring.spin * (0.65 + k3 * 1.35)

        local shimmer = 1.0 + math.sin(time * ring.wobble + ring.phase) * high * RING_SHIMMER
        local radius = ring.radius * shimmer * (0.82 + k2 * 0.78)

        if pop > 0.0 then
            if popDirection > 0.0 then
                radius = radius + pop * (W * 0.11 + ring.seed * 7.0)
            else
                radius = radius * math.max(0.035, 1.0 - pop * 0.97)
            end
        end

        local lineWidth = RING_THICK * (0.52 + k3 * 0.82) + bass * 2.0
        of.setLineWidth(math.max(1.0, lineWidth))
        setPaletteColor(paletteU, ring.color * 0.42 + i * 0.009, (150 + high * 100) * alphaScale, 1.0)

        for segment = 0, RING_SEGMENTS - 1 do
            -- Broken codecs do not lose the same wedge on every ring.
            if ((segment + i + math.floor(time * 2.0)) % 7) ~= 0 then
                local startAngle = ring.phase + segment * segmentAngle + segmentAngle * gapFraction * 0.5
                local endAngle = ring.phase + (segment + 1) * segmentAngle - segmentAngle * gapFraction * 0.5
                drawRingSegment(ring.x, ring.y, radius, startAngle, endAngle,
                                warpAmount, ring.phase + popSeed)
            end
        end
    end
end

local function drawScene(paletteU, k1, k2, k3, k5, bass, mid, high, pop, alphaScale)
    local width = 0.33
    local bandsWeight = triWeight(k5, 0.00, width)
    local tilesWeight = triWeight(k5, 0.33, width)
    local ringsWeight = triWeight(k5, 0.66, width)

    -- The original v1.0.2 used the weights as layer gates rather than normalized
    -- alpha. That overdraw is part of the crossfade's intensity, so retain it.
    if bandsWeight + tilesWeight + ringsWeight <= 0.0 then bandsWeight = 1.0 end
    if bandsWeight > 0.0 then drawBands(paletteU, k1, k3, mid, pop, alphaScale) end
    if tilesWeight > 0.0 then drawTiles(paletteU, k2, k3, mid, high, pop, alphaScale) end
    if ringsWeight > 0.0 then drawRings(paletteU, k2, k3, bass, high, pop, alphaScale) end
end

-- -----------------------------------------------------------------------------
-- EYESY lifecycle
-- -----------------------------------------------------------------------------

function setup()
    W = renderW or of.getWidth() or 1280
    H = renderH or of.getHeight() or 720

    math.randomseed(os.time())
    -- Discard the first few low-quality values on Lua 5.1-era generators.
    for _ = 1, 5 do math.random() end

    rebuildTiles()
    rebuildRings(RINGS_BASE_MAX)

    frame = 0
    time = 0.0
    previousTrig = false
    bassWasHot = false
    popWasHot = false
    bassRingCooldown = 0
    popCooldown = 0
    mirrorFrames = 0
    popAge = POP_DURATION + 1

    if type(of.enableAlphaBlending) == "function" then
        of.enableAlphaBlending()
    elseif type(of.enableBlendMode) == "function" and of.BLENDMODE_ALPHA then
        of.enableBlendMode(of.BLENDMODE_ALPHA)
    end

    print("RINGMOSH_VHS_01 setup: " .. tostring(W) .. "x" .. tostring(H))
    print("K1 speed / K2 density / K3 warp / K4 palette / K5 scene / TRIG pop")
end

function update()
end

function draw()
    W = renderW or of.getWidth() or W
    H = renderH or of.getHeight() or H
    frame = frame + 1
    time = time + 1.0 / 60.0

    if bassRingCooldown > 0 then bassRingCooldown = bassRingCooldown - 1 end
    if popCooldown > 0 then popCooldown = popCooldown - 1 end

    local k1 = clamp01(globalNumber("knob1", 0.42))
    local k2 = clamp01(globalNumber("knob2", 0.58))
    local k3 = clamp01(globalNumber("knob3", 0.38))
    local k4 = clamp01(globalNumber("knob4", 0.08))
    local k5 = clamp01(globalNumber("knob5", 0.55))

    local bassRaw, midRaw, highRaw = analyzeAudio()
    bassEnv = bassEnv + (bassRaw - bassEnv) * 0.22
    midEnv = midEnv + (midRaw - midEnv) * 0.18
    highEnv = highEnv + (highRaw - highEnv) * 0.25

    local trigState = triggerNow()
    local trigEdge = trigState and not previousTrig
    previousTrig = trigState

    local bassHot = bassEnv > BASS_RING_THRESHOLD
    if bassHot and not bassWasHot and bassRingCooldown <= 0 and math.random() < 0.60 then
        addBurstRing()
        bassRingCooldown = BASS_RING_COOLDOWN
    end
    bassWasHot = bassHot

    local popHot = math.max(bassEnv, highEnv) > POP_THRESHOLD
    if trigEdge then
        startPop(true)
    elseif popHot and not popWasHot then
        startPop(false)
    end
    popWasHot = popHot

    if highEnv > 0.88 and math.random() < 0.07 then
        mirrorFrames = math.max(mirrorFrames, 1)
    end

    local pop = currentPop()
    if popAge < POP_DURATION then
        popAge = popAge + 1
        if popAge >= POP_DURATION and respawnAfterPop then
            -- Respawn the base field when an implode/explode resolves, while
            -- keeping any temporary density headroom available for the next hit.
            rebuildRings(RINGS_BASE_MAX)
            respawnAfterPop = false
        end
    end

    -- Current engine Persist owns the history buffer. This mode only supplies
    -- a cheap black decay layer; no private feedback surface is allocated.
    local backgroundAlpha
    if globalBool("persist") and not globalBool("persistClear") then
        backgroundAlpha = 8 + math.floor(k3 * 10)
    else
        backgroundAlpha = 255
    end
    of.setColor(0, 0, 0, backgroundAlpha)
    of.drawRectangle(0, 0, W, H)

    -- Exact recovered audio palette nudges, plus the v1.1.0 half-cycle flip.
    local paletteU = fract(k4 + bassEnv * 0.07 - midEnv * 0.03 + highEnv * 0.05)
    if pop > 0.035 then paletteU = fract(paletteU + 0.50) end

    -- The old implementation copied and smooth-scaled the whole Pygame screen.
    -- Here the same pop transform is applied before GPU geometry is submitted.
    local zoom = 1.0 + pop * ZOOM_MAX_EXTRA
    local shake = pop * SHAKE_MAX
    local shakeX = math.sin(frame * 2.41 + popSeed) * shake
    local shakeY = math.cos(frame * 2.77 + popSeed * 1.31) * shake

    of.pushMatrix()
    of.translate(W * 0.5 + shakeX, H * 0.5 + shakeY)
    of.scale(zoom, zoom, 1.0)
    of.translate(-W * 0.5, -H * 0.5)
    drawScene(paletteU, k1, k2, k3, k5, bassEnv, midEnv, highEnv, pop, 1.0)
    of.popMatrix()

    -- One/two-frame mirrored afterimage: direct redraw, no captured texture.
    if mirrorFrames > 0 then
        of.pushMatrix()
        of.translate(W, 0)
        of.scale(-1.0, 1.0, 1.0)
        drawScene(fract(paletteU + 0.50), k1, k2, k3, k5,
                  bassEnv, midEnv, highEnv, pop, 0.34)
        of.popMatrix()

        local fr, fg, fb = paletteRgb(paletteU, 0.50, 1.0)
        of.setColor(fr, fg, fb, mirrorFrames > 1 and 30 or 17)
        of.drawRectangle(0, 0, W, H)
        mirrorFrames = mirrorFrames - 1
    end

    of.setLineWidth(1.0)
    of.setColor(255, 255, 255, 255)
end

function exit()
    of.setLineWidth(1.0)
    of.setColor(255, 255, 255, 255)
    print("RINGMOSH_VHS_01 exit")
    print("script finished")
end

