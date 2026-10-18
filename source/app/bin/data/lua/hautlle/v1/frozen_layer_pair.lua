-- Resolution probe extension: unchanged 320x180 default; opt-in larger layers.
-- New helper; OF/ofxLua do the rendering. No native extensions or host changes.
local M={API_VERSION=1,API_MINOR=2}
local function need(o,k) assert(type(o[k])=="function","missing API: "..k) end
local VERT=[[
#version 100
precision highp float;
attribute vec4 position;
varying mediump vec2 copyUV;
void main() {
    vec2 p=position.xy/LAYER_SIZE;
    copyUV=p;
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
]]
local FRAG=[[
#version 100
precision mediump float;
uniform sampler2D previousImage;
uniform vec4 region;
uniform vec2 shift;
uniform float mirrorRight;
varying mediump vec2 copyUV;
void main() {
    // Integer texel operations; sample centers, not an empirical image offset.
    vec2 p=floor(copyUV*LAYER_SIZE)+vec2(0.5);
    bool inside=p.x>=region.x && p.y>=region.y &&
                p.x<region.x+region.z && p.y<region.y+region.w;
    vec2 sampleP=p;
    if(inside) {
        if(mirrorRight>0.5) {
            if(p.x>=region.x+region.z*0.5)
                sampleP.x=region.x*2.0+region.z-p.x;
        } else {
            sampleP=region.xy+mod(p-region.xy-shift,region.zw);
        }
    }
    vec4 old=texture2D(previousImage,sampleP/LAYER_SIZE);
    gl_FragColor=vec4(old.rgb,1.0);
}
]]
function M.new(logger,options)
    options=options or {}
    local W,H=options.width or 320,options.height or 180
    assert((W==320 and H==180) or (W==640 and H==360) or (W==960 and H==540) or (W==1280 and H==720),
           "supported dimensions: 320x180, 640x360, 960x540, 1280x720")
    -- Constructing the helper allocates no GPU resources.
    local self={owners={},current=1,shader=nil,failed=false,uncertain=false,closed=false,initialized=false}
    local log=logger or print
    log("presentation g3c8: empty FBO scope before every presentation")
    local function scope(fbo,shader,body,texture)
        local style,bound,active=false,false,false
        local ok,err=pcall(function()
            of.pushStyle(); style=true
            if fbo then fbo:beginFbo(); bound=true end
            of.disableAlphaBlending(); of.fill(); of.setRectMode(0)
            if shader then shader:beginShader(); active=true end
            body()
        end)
        local function unwind(fn)
            local good,e=pcall(fn)
            if not good then self.uncertain=true; ok=false; err=tostring(err or "").." unwind: "..tostring(e) end
        end
        if texture and active then unwind(function() texture:unbind(1) end) end
        if active then unwind(function() shader:endShader() end) end
        if bound then unwind(function() fbo:endFbo() end) end
        if style then unwind(function() of.popStyle() end) end
        if not ok then error(err,0) end
    end
    function self:release()
        self.closed=true -- Terminal even when cleanup must be retried.
        if self.uncertain then log("cleanup deferred: uncertain scope state; handles retained"); return false end
        local clean=true
        local function attempt(label,fn)
            local ok,e=pcall(fn)
            if ok then log("cleanup "..label.." returned; GPU deletion unverified")
            else clean=false; log("cleanup "..label.." raised; handle retained: "..tostring(e)) end
            return ok
        end
        if self.shader and attempt("shader.unload",function() self.shader:unload() end) then self.shader=nil end
        for i=1,2 do
            local f=self.owners[i]
            if f and attempt("FBO "..i..".clear",function() f:clear() end) then self.owners[i]=nil end
        end
        return clean
    end
    local function guarded(body)
        if self.closed or self.failed then return false end
        local ok,e=pcall(body)
        if not ok then
            self.failed=true; log("STOP: "..tostring(e).."; no automatic retries")
            self:release()
        end
        return ok
    end
    local function ensure(index)
        if self.owners[index] then return self.owners[index] end
        for _,k in ipairs({"pushStyle","popStyle","disableAlphaBlending","fill","setRectMode","clear","setColor","drawRectangle"}) do need(of,k) end
        local f=of.Fbo(); self.owners[index]=f
        for _,k in ipairs({"allocate","isAllocated","beginFbo","endFbo","clear","draw","getTexture"}) do need(f,k) end
        -- Require explicit settings here: persistent repeated edits need known
        -- nearest sampling, RGBA/2D, clamp, and zero depth/stencil/MSAA.
        assert(of.FboSettings~=nil,"FboSettings required")
        local s=of.FboSettings()
        s.width=W; s.height=H; s.numColorbuffers=1
        s.useDepth=false; s.useStencil=false; s.depthStencilAsTexture=false
        s.internalformat=6408; s.textureTarget=3553; s.numSamples=0
        s.wrapModeHorizontal=33071; s.wrapModeVertical=33071
        s.minFilter=9728; s.maxFilter=9728
        f:allocate(s)
        local good=f:isAllocated(); log("allocate FBO "..index.." "..W.."x"..H.." RGBA nearest: "..tostring(good))
        assert(good,"FBO allocation failed")
        return f
    end
    local function ensureShader()
        if self.shader then return self.shader end
        local sh=of.Shader(); self.shader=sh
        local sizeLiteral=string.format("vec2(%d.0,%d.0)",W,H)
        local vertexSource=VERT:gsub("LAYER_SIZE",sizeLiteral)
        local fragmentSource=FRAG:gsub("LAYER_SIZE",sizeLiteral)
        if W>320 then
            -- Require highp for texel addressing above the proven baseline size.
            vertexSource=vertexSource:gsub("varying mediump vec2 copyUV;","varying highp vec2 copyUV;")
            fragmentSource=fragmentSource:gsub("varying mediump vec2 copyUV;","varying highp vec2 copyUV;")
            fragmentSource=fragmentSource:gsub("precision mediump float;","precision highp float;")
            fragmentSource=fragmentSource:gsub("#version 100", "#version 100\n#ifndef GL_FRAGMENT_PRECISION_HIGH\n#error High_precision_fragment_coordinates_required\n#endif",1)
        end
        for _,k in ipairs({"setupShaderFromSource","bindDefaults","linkProgram","isLoaded","beginShader","endShader","setUniformTexture","setUniform1f","setUniform2f","setUniform4f","unload"}) do need(sh,k) end
        local v=sh:setupShaderFromSource(35633,vertexSource); log("vertex compile="..tostring(v)); assert(v,"vertex compile failed")
        local f=sh:setupShaderFromSource(35632,fragmentSource); log("fragment compile="..tostring(f)); assert(f,"fragment compile failed")
        assert(sh:bindDefaults()~=false,"bindDefaults failed")
        local linked=sh:linkProgram(); local loaded=sh:isLoaded()
        log("shader link="..tostring(linked).." loaded="..tostring(loaded)); assert(linked and loaded,"shader link failed")
        return sh
    end
    function self:paint(body)
        return guarded(function()
            if not self.pairReady then
                local a,b=ensure(1),ensure(2)
                for _,f in ipairs({a,b}) do
                    assert(f:isAllocated(),"pair FBO is not allocated")
                    local td=f:getTexture():getTextureData()
                    assert(td.tex_w==W and td.tex_h==H and td.textureTarget==3553,"pair backing mismatch")
                end
                log("PAIR_VALIDATED "..W.."x"..H.." both FBOs before first clear/draw")
                scope(a,nil,function() of.clear(0,0,0,255) end)
                scope(b,nil,function() of.clear(0,0,0,255) end)
                self.pairReady=true
            end
            scope(self.owners[self.current],nil,body)
            self.initialized=true -- Commit only after the first paint succeeds.
        end)
    end
    local function transform(x,y,w,h,dx,dy,mirror)
        return guarded(function()
            assert(self.initialized,"paint must succeed before transform")
            assert(x>=0 and y>=0 and w>0 and h>0 and x+w<=W and y+h<=H,"region outside layer")
            assert(dx==math.floor(dx) and dy==math.floor(dy),"shift must be whole pixels")
            assert(x==math.floor(x) and y==math.floor(y) and w==math.floor(w) and h==math.floor(h),"region must use whole pixels")
            assert(not mirror or w%2==0,"mirror requires even region width")
            local src=ensure(self.current); local dst=ensure(3-self.current); local sh=ensureShader()
            local texture=src:getTexture(); need(texture,"getTextureData"); need(texture,"unbind"); need(of,"getNativeViewport")
            scope(dst,sh,function()
                local td=texture:getTextureData(); local vp=of.getNativeViewport()
                assert(td.tex_w==W and td.tex_h==H and td.textureTarget==3553,"unexpected backing texture")
                assert(vp.x==0 and vp.y==0 and vp.width==W and vp.height==H,"unexpected native viewport")
                of.clear(0,0,0,255)
                sh:setUniformTexture("previousImage",texture,1)
                sh:setUniform4f("region",x,y,w,h)
                sh:setUniform2f("shift",dx,dy)
                sh:setUniform1f("mirrorRight",mirror and 1 or 0)
                of.setColor(255,255,255,255); of.drawRectangle(0,0,W,H)
            end,texture)
            texture=nil; self.current=3-self.current
        end)
    end
    function self:shift(x,y,w,h,dx,dy) return transform(x,y,w,h,dx,dy,false) end
    function self:mirror(x,y,w,h) return transform(x,y,w,h,0,0,true) end
    function self:draw()
        if self.closed or self.failed then return false end
        if not self.owners[self.current] then return not self.failed end
        return guarded(function()
            -- Equalize the final OF begin/end boundary on hold and mutation
            -- callbacks. No clear, draw, shader, allocation, swap, or Y flip.
            -- Candidate workaround for nested render-surface state, not a
            -- claim that the installed renderer matches the legacy reference.
            scope(self.owners[self.current],nil,function() end)
            scope(nil,nil,function() of.setColor(255,255,255,255); self.owners[self.current]:draw(0,0,1280,720) end)
        end)
    end
    return self
end
return M
