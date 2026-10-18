-- KCSC RELIQUARY 01: isolated audition candidate, not hardware validated.
-- Original KCSC monogram, true beveled mesh, no native extension required.
require('eyesy')
modeTitle='KCSC Reliquary 01'
titleK1='Size'; titleK2='Depth'; titleK3='Motion'; titleK4='Silver / Bronze'; titleK5='Audio'
local mesh,vbo,shader,target
local vertexCount,indexCount=0,0
local ready=false
local t,fast,slow,hit,last,gate=0,0,0,0,0,0
local previousTrigger=false
local RW,RH=960,540
local function clamp(v,a,b) return math.max(a,math.min(b,v)) end
local function knob(v,d) return clamp(tonumber(v) or d,0,1) end
local function yes(v) return v==true or v==1 end
local function release()
    ready=false
    if shader then pcall(function() shader:unload() end);shader=nil end
    if vbo then pcall(function() vbo:clear() end);vbo=nil end
    if mesh then pcall(function() mesh:clear() end);mesh=nil end
    if target then pcall(function() target:clear() end);target=nil end
    collectgarbage('collect')
end
function setup()
    release()
    local ok,err=pcall(function()
        -- OF data root differs from the mode cwd; always give OF absolute paths.
        local root=of.FilePath.getCurrentWorkingDirectory()..'/'
        mesh=of.Mesh();mesh:load(root..'logo.ply')
        if mesh:getNumVertices()==0 or mesh:getNumNormals()~=mesh:getNumVertices() then error('mesh or normals missing') end
        vertexCount=mesh:getNumVertices();indexCount=mesh:getNumIndices()
        if indexCount==0 then error('mesh indices missing') end
        vbo=of.Vbo();vbo:setMesh(mesh,35044) -- GL_STATIC_DRAW, upload once
        if not vbo:getIsAllocated() or not vbo:getUsingNormals() or not vbo:getUsingIndices() then error('VBO upload failed') end
        mesh:clear();mesh=nil
        shader=of.Shader()
        if not shader:load(root..'metal.vert',root..'metal.frag') then error('shader compile/link failed') end
        local settings=of.FboSettings()
        settings.width=RW;settings.height=RH;settings.numColorbuffers=1
        settings.useDepth=true;settings.useStencil=false;settings.depthStencilAsTexture=false
        settings.textureTarget=3553 -- GL_TEXTURE_2D
        settings.internalformat=6408 -- GL_RGBA
        settings.depthStencilInternalFormat=33189 -- GL_DEPTH_COMPONENT16
        settings.numSamples=0
        settings.minFilter=9729;settings.maxFilter=9729 -- GL_LINEAR
        settings.wrapModeHorizontal=33071;settings.wrapModeVertical=33071 -- CLAMP_TO_EDGE
        target=of.Fbo();target:allocate(settings)
        if not target:isAllocated() then error('color/depth FBO allocation failed') end
        if target:getDepthBuffer()==0 then error('depth renderbuffer missing') end
        last=of.getElapsedTimef();t=0;fast=0;slow=0;hit=0;gate=0;previousTrigger=false
        ready=true
        print('KCSC_RELIQUARY_01 READY: '..vertexCount..' vertices; 960x540 RGBA + D16 requested; physical storage and GPU cost unverified')
    end)
    if not ok then print('KCSC_RELIQUARY_01 FAILED: '..tostring(err));release() end
end
function update() end
function draw()
    local w,h=renderW or 1280,renderH or 720
    if not ready then return end
    local now=of.getElapsedTimef();local dt=clamp(now-last,0,0.1);last=now;t=t+dt
    local k1,k2,k3,k4,k5=knob(knob1,.55),knob(knob2,.6),knob(knob3,.28),knob(knob4,.08),knob(knob5,.65)
    local energy=knob(peak,0)
    fast=fast+(energy-fast)*(1-math.exp(-dt*24))
    slow=slow+(energy-slow)*(1-math.exp(-dt*3.8))
    local trigger=yes(trig);gate=math.max(0,gate-dt)
    local onset=math.max(0,fast-slow*1.12)
    if gate<=0 and ((trigger and not previousTrigger) or onset>.09) then
        hit=math.max(hit,clamp(.25+onset*2.5,0,1));gate=.22
    end
    previousTrigger=trigger
    hit=hit*math.exp(-dt*4.2)
    local response=k5*hit
    local depth=.12+k2*.28+response*.08
    local scale=.64+k1*.12+response*.018
    local pitch=.18+math.sin(t*.19)*k3*.10
    local yaw=-.38+math.sin(t*.16)*k3*.27
    local roll=math.sin(t*.13)*k3*.025
    of.pushStyle()
    local fboOpen,shaderOpen=false,false
    local ok,err=pcall(function()
        target:beginFbo();fboOpen=true
        of.clear(0,0,0,255) -- includes depth clear in OF; no feedback history
        of.enableDepthTest()
        of.disableAlphaBlending()
        shader:beginShader();shaderOpen=true
        shader:setUniform2f('u_resolution',RW,RH)
        shader:setUniform3f('u_pose',pitch,yaw,roll)
        shader:setUniform1f('u_depth',depth);shader:setUniform1f('u_scale',scale)
        -- OF's FBO texture is presented with its bottom row at screen top.
        -- Our custom clip-space camera bypasses OF's matrix flip, so do it explicitly.
        shader:setUniform1f('u_clipY',-1)
        shader:setUniform1f('u_time',t);shader:setUniform1f('u_hit',response)
        shader:setUniform1f('u_finish',k4);shader:setUniform1f('u_light',.55)
        vbo:drawElements(4,indexCount) -- GL_TRIANGLES
    end)
    -- Unwind owned state even if a Lua binding fails while the FBO is active.
    if shaderOpen then pcall(function() shader:endShader() end) end
    of.disableDepthTest()
    if fboOpen then pcall(function() target:endFbo() end) end
    if ok then
        of.enableAlphaBlending();of.setColor(255,255,255,255)
        ok,err=pcall(function() target:draw(0,0,w,h) end)
    end
    of.popStyle()
    if not ok then print('KCSC_RELIQUARY_01 DRAW FAILED: '..tostring(err));release() end
end
function exit()
    of.disableDepthTest()
    release()
    print('KCSC_RELIQUARY_01 released mesh/VBO, shader, color/depth FBO')
end
