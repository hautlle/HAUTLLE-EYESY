-- Deterministic OF call recorder and failure injector. No GL or pixel emulation.
local M={}
function M.json(x)
    local t=type(x)
    if t=='number' then assert(x==x and math.abs(x)~=math.huge);return string.format('%.17g',x) end
    if t=='boolean' then return x and 'true' or 'false' end
    if t=='nil' then return 'null' end
    if t=='string' then return '"'..x:gsub('\\','\\\\'):gsub('"','\\"'):gsub('\n','\\n'):gsub('\r','\\r'):gsub('\t','\\t')..'"' end
    assert(t=='table',t)
    local out={}
    if #x>0 then for i=1,#x do out[i]=M.json(x[i]) end;return '['..table.concat(out,',')..']' end
    local keys={} for k in pairs(x) do keys[#keys+1]=k end;table.sort(keys)
    for _,k in ipairs(keys) do out[#out+1]=M.json(k)..':'..M.json(x[k]) end
    return '{'..table.concat(out,',')..'}'
end
function M.new()
    local s={trace={},counts={},failures={},fbos={},shaders={},target=nil,stack={},styles={},style={color={9,8,7,6},alpha=true,fill=false,rect=1},activeShader=nil,texture=nil,clock=0,uninitializedReads=0,parentWrites=0}
    local of={}
    function s:event(name,...)
        self.counts[name]=(self.counts[name] or 0)+1
        self.trace[#self.trace+1]={name,...}
        if self.failures[name]==self.counts[name] then error('injected '..name) end
    end
    function s:fail(name,offset) self.failures[name]=(self.counts[name] or 0)+(offset or 1) end
    function s:assertBalanced()
        assert(#self.stack==0 and self.target==nil,'FBO stack not restored')
        assert(#self.styles==0,'style stack not restored')
        assert(self.activeShader==nil and self.texture==nil,'shader/texture still bound')
        assert(M.json(self.style)==M.json({color={9,8,7,6},alpha=true,fill=false,rect=1}),'parent style changed')
    end
    function s:assertRetired()
        for _,f in ipairs(self.fbos) do assert(not f.allocated,'owned FBO retained') end
        for _,sh in ipairs(self.shaders) do assert(not sh.loaded,'owned shader retained') end
    end
    function of.pushStyle() s:event('pushStyle');s.styles[#s.styles+1]=s.style;s.style={color=s.style.color,alpha=s.style.alpha,fill=s.style.fill,rect=s.style.rect} end
    function of.popStyle() s:event('popStyle');assert(#s.styles>0);s.style=s.styles[#s.styles];s.styles[#s.styles]=nil end
    function of.disableAlphaBlending() s:event('disableAlphaBlending');s.style.alpha=false end
    function of.fill() s:event('fill');s.style.fill=true end
    function of.setRectMode(v) s:event('setRectMode',v);s.style.rect=v end
    function of.setColor(...) s:event('setColor',...);s.style.color={...} end
    function of.clear(...)
        s:event('clearPixels',s.target and s.target.id or 0,...)
        if s.target then s.target.initialized=true else s.parentWrites=s.parentWrites+1 end
    end
    for _,name in ipairs({'drawRectangle','drawTriangle'}) do
        of[name]=function(...)
            s:event(name,s.target and s.target.id or 0,...)
            if s.target then s.target.initialized=true else s.parentWrites=s.parentWrites+1 end
        end
    end
    function of.getElapsedTimef() return s.clock end
    function of.toDataPath(path,absolute)
        assert(absolute==true and path=='lua/hautlle/v1/frozen_layer_pair.lua')
        return DATA_ROOT..'/'..path
    end
    function of.getNativeViewport()
        s:event('getNativeViewport')
        return {x=0,y=0,width=s.target and s.target.w or 1280,height=s.target and s.target.h or 720}
    end
    function of.FboSettings() s:event('FboSettings');return {} end
    function of.Fbo()
        s:event('Fbo')
        local f={id=#s.fbos+1,allocated=false,initialized=false};s.fbos[#s.fbos+1]=f
        function f:allocate(settings)
            s:event('allocate',self.id,settings)
            self.w,self.h=settings.width,settings.height;self.allocated=true
            s:event('allocate.after',self.id)
        end
        function f:isAllocated() s:event('isAllocated',self.id);return self.allocated and not s.rejectAllocation end
        function f:beginFbo()
            s:event('beginFbo',self.id);assert(self.allocated)
            s.stack[#s.stack+1]=s.target or false;s.target=self
        end
        function f:endFbo()
            s:event('endFbo',self.id);assert(s.target==self and #s.stack>0)
            s.target=s.stack[#s.stack] or nil;s.stack[#s.stack]=nil
        end
        function f:clear() s:event('clearFbo',self.id);self.allocated=false;self.initialized=false end
        function f:draw(...) s:event('drawFbo',self.id,...);assert(self.allocated and self.initialized) end
        function f:getTexture()
            s:event('getTexture',self.id)
            local tex={owner=self}
            function tex:getTextureData()
                s:event('getTextureData',f.id)
                return {tex_w=f.w,tex_h=f.h,textureTarget=s.badTarget or 3553}
            end
            function tex:unbind(unit) s:event('textureUnbind',f.id,unit);assert(unit==1);s.texture=nil end
            return tex
        end
        return f
    end
    function of.Shader()
        s:event('Shader');local sh={id=#s.shaders+1,loaded=false};s.shaders[#s.shaders+1]=sh
        function sh:setupShaderFromSource(kind,source) s:event('compile',kind,source);return not s.rejectCompile end
        function sh:bindDefaults() s:event('bindDefaults');return true end
        function sh:linkProgram() s:event('linkProgram');self.loaded=not s.rejectLink;return self.loaded end
        function sh:isLoaded() s:event('isLoaded');return self.loaded end
        function sh:beginShader() s:event('beginShader');assert(self.loaded);s.activeShader=self end
        function sh:endShader() s:event('endShader');s.activeShader=nil end
        function sh:setUniformTexture(name,texture,unit)
            s:event('uniformTexture',name,texture.owner.id,unit);assert(unit==1)
            if not texture.owner.initialized then s.uninitializedReads=s.uninitializedReads+1;error('uninitialized source') end
            s.texture=texture
        end
        function sh:setUniform1f(...) s:event('uniform1f',...) end
        function sh:setUniform2f(...) s:event('uniform2f',...) end
        function sh:setUniform4f(...) s:event('uniform4f',...) end
        function sh:unload() s:event('unloadShader');self.loaded=false end
        return sh
    end
    s.of=of
    return s
end
return M
