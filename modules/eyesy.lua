-- Minimal EYESY oFLua compatibility shim
-- Recreates the missing beta-image eyesy.lua enough for example modes.

knob1 = knob1 or 0.5
knob2 = knob2 or 0.5
knob3 = knob3 or 0.5
knob4 = knob4 or 0.5
knob5 = knob5 or 0.5

-- Audio buffers used by sound-reactive examples.
inL = inL or {}
inR = inR or {}

for i = 1, 100 do
    if inL[i] == nil then inL[i] = 0 end
    if inR[i] == nil then inR[i] = 0 end
end

-- Optional normalized aliases some patches may expect.
knobs = knobs or {knob1, knob2, knob3, knob4, knob5}


-- --- EYESY OF/Lua compatibility helper: colorPick ---
-- Simple compatibility helper expected by many C&G example modes.
function colorPick(knob, palette)
    knob = knob or 0.5

    local r = math.abs(math.sin(knob * math.pi * 2.0))
    local g = math.abs(math.sin((knob + 0.333) * math.pi * 2.0))
    local b = math.abs(math.sin((knob + 0.666) * math.pi * 2.0))

    if of and of.setColor then
        if of.floatColor then
            of.setColor(of.floatColor(r, g, b, 1.0))
        else
            of.setColor(r * 255, g * 255, b * 255)
        end
    end
end
-- --- end colorPick helper ---

return true
