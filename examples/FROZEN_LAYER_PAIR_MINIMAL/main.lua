-- Small EYESY example. Install the helper in the executable's matching bin/data.
-- Trigger shifts the retained image; exit releases this instance permanently.
local layer,painted,failed
function setup()
    if layer then return end -- A failed cleanup must be retried before reentry.
    local api=dofile(of.toDataPath('lua/hautlle/v1/frozen_layer_pair.lua',true))
    assert(api.API_VERSION==1 and api.API_MINOR>=2,'frozen-layer API 1.2 required')
    layer=api.new(print,{width=320,height=180}) -- No GPU allocation yet.
    painted=false;failed=false
end
function update() end
function draw()
    if not layer or failed then return end
    if not painted then
        painted=layer:paint(function()
            of.clear(0,0,0,255)
            of.setColor(255,160,40,255)
            of.drawRectangle(40,45,80,90)
        end)
        if not painted then failed=true;return end
    end
    if trig==true or (type(trig)=='number' and trig>0) then
        if not layer:shift(0,0,320,180,8,0) then failed=true;return end
    end
    if not layer:draw() then failed=true end
end
function exit()
    if not layer or layer:release() then layer=nil;painted=false;failed=false
    else failed=true end -- Handles retained; a later exit() may retry cleanup.
end
