-- ha_palette.lua
-- HAUTLLE / EYESY OF-Lua Patch5A1
--
-- OSv3-compatible cosine palette helper.
--
-- Reads:
--   /sdcard/System/palettes.json
--   /sdcard/System/ha_palette_config.json
--
-- Config stores OSv3-style 0-based indexes:
--   fg_palette = 0
--   bg_palette = 0
--
-- Lua API:
--   local pal = require("ha_palette")
--   local r,g,b = pal.fg(t)
--   local r,g,b = pal.bg(t)
--   local r,g,b = pal.bg_dim(t, 0.20)
--   pal.next_fg()
--   pal.next_bg()
--   pal.save_config()

local M = {}

local PALETTE_PATH = "/sdcard/System/palettes.json"
local CONFIG_PATH  = "/sdcard/System/ha_palette_config.json"
local TWO_PI = math.pi * 2.0

local defaults = {
    {
        name = "Void Blue",
        a = {0.12, 0.14, 0.24},
        b = {0.28, 0.32, 0.55},
        c = {0.80, 0.70, 0.55},
        d = {0.10, 0.25, 0.52},
    },
    {
        name = "Signal Amber",
        a = {0.34, 0.22, 0.08},
        b = {0.48, 0.30, 0.10},
        c = {0.35, 0.55, 0.80},
        d = {0.02, 0.08, 0.18},
    },
    {
        name = "Deep Purple",
        a = {0.18, 0.08, 0.28},
        b = {0.34, 0.18, 0.46},
        c = {0.30, 0.42, 0.27},
        d = {0.70, 0.25, 0.05},
    },
    {
        name = "Bone Teal",
        a = {0.36, 0.42, 0.39},
        b = {0.32, 0.44, 0.48},
        c = {0.20, 0.32, 0.41},
        d = {0.08, 0.33, 0.62},
    },
    {
        name = "Cold Mono",
        a = {0.20, 0.22, 0.25},
        b = {0.52, 0.54, 0.58},
        c = {0.50, 0.50, 0.50},
        d = {0.00, 0.00, 0.00},
    },
    {
        name = "Bad VHS",
        a = {0.28, 0.13, 0.26},
        b = {0.62, 0.33, 0.50},
        c = {0.90, 0.65, 0.37},
        d = {0.14, 0.57, 0.91},
    },
}

local palettes = defaults
local config = {
    fg_palette = 0,
    bg_palette = 0,
}

local function read_file(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

local function clamp01(x)
    x = tonumber(x) or 0
    if x < 0 then return 0 end
    if x > 1 then return 1 end
    return x
end

local function byte(x)
    x = clamp01(x)
    return math.floor(x * 255 + 0.5)
end

-- Minimal JSON decoder sufficient for OSv3 palette/config files.
local function decode_json(src)
    local i, n = 1, #src

    local function err(msg)
        error("json parse error near byte " .. tostring(i) .. ": " .. msg, 0)
    end

    local function peek()
        return src:sub(i, i)
    end

    local function nextc()
        local c = src:sub(i, i)
        i = i + 1
        return c
    end

    local function skip_ws()
        while i <= n do
            local c = peek()
            if c == " " or c == "\n" or c == "\r" or c == "\t" then
                i = i + 1
            else
                break
            end
        end
    end

    local parse_value

    local function parse_string()
        if nextc() ~= '"' then err("expected string") end
        local out = {}

        while i <= n do
            local c = nextc()
            if c == '"' then
                return table.concat(out)
            elseif c == "\\" then
                local e = nextc()
                if e == '"' or e == "\\" or e == "/" then
                    out[#out + 1] = e
                elseif e == "b" then
                    out[#out + 1] = "\b"
                elseif e == "f" then
                    out[#out + 1] = "\f"
                elseif e == "n" then
                    out[#out + 1] = "\n"
                elseif e == "r" then
                    out[#out + 1] = "\r"
                elseif e == "t" then
                    out[#out + 1] = "\t"
                elseif e == "u" then
                    -- Keep parser simple; palette names should not need unicode escapes.
                    i = i + 4
                    out[#out + 1] = "?"
                else
                    err("bad escape")
                end
            else
                out[#out + 1] = c
            end
        end

        err("unterminated string")
    end

    local function parse_number()
        local start = i
        local c = peek()

        if c == "-" then i = i + 1 end

        while i <= n and src:sub(i, i):match("%d") do i = i + 1 end

        if src:sub(i, i) == "." then
            i = i + 1
            while i <= n and src:sub(i, i):match("%d") do i = i + 1 end
        end

        c = src:sub(i, i)
        if c == "e" or c == "E" then
            i = i + 1
            c = src:sub(i, i)
            if c == "+" or c == "-" then i = i + 1 end
            while i <= n and src:sub(i, i):match("%d") do i = i + 1 end
        end

        local num = tonumber(src:sub(start, i - 1))
        if num == nil then err("bad number") end
        return num
    end

    local function parse_array()
        if nextc() ~= "[" then err("expected array") end
        local arr = {}
        skip_ws()

        if peek() == "]" then
            i = i + 1
            return arr
        end

        while true do
            arr[#arr + 1] = parse_value()
            skip_ws()

            local c = nextc()
            if c == "]" then
                return arr
            elseif c ~= "," then
                err("expected comma or ]")
            end
            skip_ws()
        end
    end

    local function parse_object()
        if nextc() ~= "{" then err("expected object") end
        local obj = {}
        skip_ws()

        if peek() == "}" then
            i = i + 1
            return obj
        end

        while true do
            skip_ws()
            if peek() ~= '"' then err("expected object key") end
            local key = parse_string()
            skip_ws()

            if nextc() ~= ":" then err("expected colon") end
            skip_ws()

            obj[key] = parse_value()
            skip_ws()

            local c = nextc()
            if c == "}" then
                return obj
            elseif c ~= "," then
                err("expected comma or }")
            end
            skip_ws()
        end
    end

    function parse_value()
        skip_ws()
        local c = peek()

        if c == '"' then return parse_string() end
        if c == "{" then return parse_object() end
        if c == "[" then return parse_array() end
        if c == "-" or c:match("%d") then return parse_number() end

        if src:sub(i, i + 3) == "true" then
            i = i + 4
            return true
        end

        if src:sub(i, i + 4) == "false" then
            i = i + 5
            return false
        end

        if src:sub(i, i + 3) == "null" then
            i = i + 4
            return nil
        end

        err("unexpected character " .. tostring(c))
    end

    local v = parse_value()
    skip_ws()
    return v
end

local function valid_triplet(t)
    return type(t) == "table"
       and type(t[1]) == "number"
       and type(t[2]) == "number"
       and type(t[3]) == "number"
end

local function valid_palette(p)
    return type(p) == "table"
       and type(p.name) == "string"
       and valid_triplet(p.a)
       and valid_triplet(p.b)
       and valid_triplet(p.c)
       and valid_triplet(p.d)
end

local function load_palettes()
    local s = read_file(PALETTE_PATH)
    if not s then
        palettes = defaults
        return false, "missing palettes.json; using defaults"
    end

    local ok, data = pcall(decode_json, s)
    if not ok or type(data) ~= "table" or #data < 1 then
        palettes = defaults
        return false, "bad palettes.json; using defaults"
    end

    for idx, p in ipairs(data) do
        if not valid_palette(p) then
            palettes = defaults
            return false, "invalid palette at index " .. tostring(idx) .. "; using defaults"
        end
    end

    palettes = data
    return true, "loaded palettes.json"
end

local function clamp_index0(i0)
    i0 = math.floor(tonumber(i0) or 0)
    if i0 < 0 then i0 = 0 end
    if i0 >= #palettes then i0 = #palettes - 1 end
    return i0
end

local function load_config()
    local s = read_file(CONFIG_PATH)
    if not s then
        config.fg_palette = 0
        config.bg_palette = 0
        return false, "missing config; using defaults"
    end

    local ok, data = pcall(decode_json, s)
    if not ok or type(data) ~= "table" then
        config.fg_palette = 0
        config.bg_palette = 0
        return false, "bad config; using defaults"
    end

    config.fg_palette = clamp_index0(data.fg_palette or 0)
    config.bg_palette = clamp_index0(data.bg_palette or 0)
    return true, "loaded config"
end

function M.save_config()
    local f = io.open(CONFIG_PATH, "w")
    if not f then return false end

    f:write(string.format('{\n  "fg_palette": %d,\n  "bg_palette": %d\n}\n',
        clamp_index0(config.fg_palette),
        clamp_index0(config.bg_palette)
    ))
    f:close()
    return true
end

function M.reload()
    local pok, pmsg = load_palettes()
    local cok, cmsg = load_config()
    return pok and cok, pmsg .. "; " .. cmsg
end

local function palette_by_index0(i0)
    if #palettes < 1 then palettes = defaults end
    i0 = clamp_index0(i0)
    return palettes[i0 + 1]
end

function M.count()
    return #palettes
end

function M.name(i0)
    return palette_by_index0(i0 or 0).name
end

function M.fg_index()
    return clamp_index0(config.fg_palette)
end

function M.bg_index()
    return clamp_index0(config.bg_palette)
end

function M.fg_name()
    return M.name(M.fg_index())
end

function M.bg_name()
    return M.name(M.bg_index())
end

function M.set_fg(i0, save)
    config.fg_palette = clamp_index0(i0)
    if save then M.save_config() end
    return config.fg_palette
end

function M.set_bg(i0, save)
    config.bg_palette = clamp_index0(i0)
    if save then M.save_config() end
    return config.bg_palette
end

function M.next_fg(save)
    return M.set_fg((M.fg_index() + 1) % #palettes, save)
end

function M.prev_fg(save)
    return M.set_fg((M.fg_index() - 1) % #palettes, save)
end

function M.next_bg(save)
    return M.set_bg((M.bg_index() + 1) % #palettes, save)
end

function M.prev_bg(save)
    return M.set_bg((M.bg_index() - 1) % #palettes, save)
end

function M.color(t, i0)
    t = clamp01(t or 0)
    local p = palette_by_index0(i0 or 0)

    local r = p.a[1] + p.b[1] * math.cos(TWO_PI * (p.c[1] * t + p.d[1]))
    local g = p.a[2] + p.b[2] * math.cos(TWO_PI * (p.c[2] * t + p.d[2]))
    local b = p.a[3] + p.b[3] * math.cos(TWO_PI * (p.c[3] * t + p.d[3]))

    return byte(r), byte(g), byte(b)
end

function M.fg(t)
    return M.color(t, M.fg_index())
end

function M.bg(t)
    return M.color(t, M.bg_index())
end

function M.bg_dim(t, amount)
    amount = clamp01(amount or 0.20)
    local r, g, b = M.bg(t)
    return math.floor(r * amount), math.floor(g * amount), math.floor(b * amount)
end

M.reload()

return M
