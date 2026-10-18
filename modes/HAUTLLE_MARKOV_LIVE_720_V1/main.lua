-- HAUTLLE Markov source recovery: original no-audio branch, timed 30Hz.
-- Virtual source geometry 640x480 -> retained 1280x720; see README mappings.
local W,H=1280,720
local SX,SY=W/640,H/480
local ERASE_SCALE=H/180
local CFG={mutationHz=30,k1=.55,k2=.55,k3=.45,k4=.33,k5=.35,seed=69069}
inL={};inR={}
local S,layer,started,failed
local keepHistory=false
local lastPersist=nil
local pendingTrigger=false
-- Presentation-only cache: at most nine bars, reused between 30 Hz mutations.
local bars,barCount={},0
local nextAt,lastTime,lastLog,callbacks,steps,skipped
local names={'DRAG_V','DRAG_H','TEAR','MIRROR'}
local function log(s) print('[HAUTLLE MARKOV SOURCE] '..s) end
local function clamp(x,a,b) return math.max(a,math.min(b,x)) end
-- Host supplies knob1..knob5 normalized to 0..1, including knob recorder playback.
-- Preserve last valid value on missing/nonfinite input; clamp finite bounds.
local function readControls()
    for i=1,5 do
        local value=_G['knob'..i]
        if type(value)=='number' and value==value and value~=math.huge and value~=-math.huge then
            CFG['k'..i]=clamp(value,0,1)
        end
    end
end
local function int(x) if x<0 then return math.ceil(x) else return math.floor(x) end end
local function round(x)
    local n=math.floor(x);local f=x-n
    if f<.5 then return n elseif f>.5 then return n+1 else return n+n%2 end
end
local function rnd() S.rng=S.rng*48271%2147483647;return S.rng/2147483647 end
local function ri(a,b) return a+math.floor(rnd()*(b-a+1)) end
local function uniform(a,b) return a+(b-a)*rnd() end
local function smooth(x) x=clamp(x,0,1);return x*x*(3-2*x) end
local function color(h)
    local x=(h%1)*6;local f=x-math.floor(x)
    local p,q,t=.23,1-.77*f,.23+.77*f
    local a={{1,t,p},{q,1,p},{p,1,t},{p,q,1},{t,p,1},{1,p,q}}
    local c=a[math.floor(x)+1];return {int(c[1]*255),int(c[2]*255),int(c[3]*255)}
end
local function spawn()
    S.lines={};S.sliding=true;S.cohorts=S.cohorts+1
    for i=1,ri(5,9) do
        local cx,cy=uniform(96,544),uniform(72,408)
        local angle=uniform(0,math.pi*2)
        local len=uniform(.55*800,1.05*800)*(.8+.4*rnd())
        local dx,dy=math.cos(angle)*len*.5,math.sin(angle)*len*.5
        local a,b,c,d=clamp(int(cx-dx),0,639),clamp(int(cy-dy),0,479),clamp(int(cx+dx),0,639),clamp(int(cy+dy),0,479)
        local width=ri(3,7);local rgb=color(S.hue+rnd()*.25)
        local albedo=.85+rnd()*.30;local speed=.0012+rnd()*.0012
        local theta=uniform(0,math.pi*2);local ox,oy=int(math.cos(theta)*384),int(math.sin(theta)*384)
        local frames=ri(14,32)
        S.lines[i]={a=a,b=b,c=c,d=d,width=width,rgb=rgb,albedo=albedo,speed=speed,ox=ox,oy=oy,left=frames,total=frames}
    end
end
local function line(l,a,b,c,d)
    -- Lighting in original source geometry; two triangles map the thick segment
    -- anisotropically into the 16:9 buffer. No GLES wide-line dependency.
    local dx,dy=c-a,d-b;local mag=math.max(1e-6,math.sqrt(dx*dx+dy*dy))
    local ux,uy=dx/mag,dy/mag;local theta=S.t*.015+CFG.k4*3
    local lx,ly=math.cos(theta),math.sin(theta)
    local gain=.30+.55*math.max(0,-uy*lx+ux*ly)+.35*(1-math.abs(ux*lx+uy*ly))^2
    local function ch(v) return clamp(int(clamp(int(v*gain),0,255)*l.albedo),0,255) end
    local r,g,bv=ch(l.rgb[1]),ch(l.rgb[2]),ch(l.rgb[3])
    local nx,ny=-uy*l.width*.5,ux*l.width*.5
    local x1,y1=(a+nx)*SX,(b+ny)*SY
    local x2,y2=(c+nx)*SX,(d+ny)*SY
    local x3,y3=(c-nx)*SX,(d-ny)*SY
    local x4,y4=(a-nx)*SX,(b-ny)*SY
    barCount=barCount+1
    local bar=bars[barCount]
    bar[1],bar[2],bar[3]=r,g,bv
    bar[4],bar[5],bar[6],bar[7]=x1,y1,x2,y2
    bar[8],bar[9],bar[10],bar[11]=x3,y3,x4,y4
    of.setColor(r,g,bv,255)
    of.drawTriangle(x1,y1,x2,y2,x3,y3)
    of.drawTriangle(x1,y1,x3,y3,x4,y4)
end
local function foreground(speed)
    barCount=0
    if S.sliding then
        local still=false
        for _,l in ipairs(S.lines) do
            local a,b,c,d=l.a,l.b,l.c,l.d
            if l.left>0 then
                local w=smooth(1-l.left/l.total)
                a=int(l.a-l.ox+l.ox*w);b=int(l.b-l.oy+l.oy*w)
                c=int(l.c-l.ox+l.ox*w);d=int(l.d-l.oy+l.oy*w)
                l.left=l.left-1;still=true
            end
            line(l,a,b,c,d)
        end
        S.sliding=still
    else
        local amp=math.max(1,int(3*(.5+.5*speed)));local rate=.6+.4*speed
        for i,l in ipairs(S.lines) do
            local j=S.t*l.speed*rate+(i-1)*.017
            local dx,dy=int(math.sin(j)*amp),int(math.cos(j*1.1)*amp)
            line(l,clamp(l.a+dx,0,639),clamp(l.b+dy,0,479),clamp(l.c-dx,0,639),clamp(l.d-dy,0,479))
        end
    end
end
local function scanlines()
    of.setColor(0,0,0,255)
    for y=0,H-1,8*ERASE_SCALE do of.drawRectangle(0,y,W,ERASE_SCALE) end
end
local function overlayBody()
    of.disableAlphaBlending();of.fill();of.setRectMode(0)
    scanlines()
    -- Reuse the exact geometry/color painted this mutation. foreground() is
    -- stateful, and S.t/controls/cohort may already have advanced since then.
    for i=1,barCount do
        local b=bars[i]
        of.setColor(b[1],b[2],b[3],255)
        of.drawTriangle(b[4],b[5],b[6],b[7],b[8],b[9])
        of.drawTriangle(b[4],b[5],b[8],b[9],b[10],b[11])
    end
end
local function drawOverlay()
    of.pushStyle()
    local ok,e=pcall(overlayBody)
    of.popStyle()
    if not ok then error(e) end
end
local function choose()
    local boost=.33+1.7*CFG.k2
    local w={.7*boost,.7*boost,.3*boost,.08*(.5+CFG.k2)}
    local fav=round(CFG.k5*3)+1;local total=0
    for i=1,4 do
        w[i]=w[i]*S.kernel[S.mcState][i]
        if i==fav then w[i]=w[i]*2 end
        total=total+w[i]
    end
    local p=rnd()*total;local sum=0
    for i=1,4 do sum=sum+w[i];if p<=sum then return i end end
    return S.state
end
local function rebuild()
    local scores={.7,.7,.6,.5}
    for row=1,4 do
        for col=1,4 do S.kernel[row][col]=(.8+2.2*scores[col]/2.5)*(.94+.12*rnd()) end
    end
end
local function sign(axis)
    local p=clamp(.7+.09*S.sign[axis]+.22*S.bias[axis],.06,.94)
    local s=rnd()<p and 1 or -1;S.sign[axis]=s;return s
end
local function tear(shift,height)
    local y=ri(80,math.max(80,400-height));local dx=clamp(int(shift),-214,213)
    if dx==0 then return true end
    local top=int(y*SY);local h=math.max(1,int((y+height)*SY)-top)
    if not keepHistory then return true end
    return layer:shift(0,top,W,h,round(dx*SX),0)
end
local function tick()
    if not keepHistory and not layer:paint(function() of.clear(0,0,0,255) end) then return false end
    local speed=.35+.65*(1-CFG.k1)
    S.dirTick=(S.dirTick+1)%10
    if S.dirTick==0 then
        local damp=.12*(1-.6*CFG.k1)
        for i=1,2 do S.bias[i]=clamp(S.bias[i]*(1-damp)+uniform(-.2,.2)*damp,-1,1) end
    end
    if S.hold<=0 then
        local nextState=choose();S.mcState=nextState;S.prev=S.state;S.state=nextState
        S.total=math.max(6,6+int(12*(.6*(1-speed)+.4)));S.left=S.total
        S.chain=S.chain+1;S.decisions=S.decisions+1
        if S.chain>=22 then
            S.hue=(rnd()+CFG.k4*.77)%1;spawn();S.chain=0;rebuild()
            if rnd()<.25 then S.bias[1]=-S.bias[1] end
            if rnd()<.25 then S.bias[2]=-S.bias[2] end
            log('cohort reseed at tick '..S.t..'; stored image retained')
        end
        S.hold=math.max(5,int(ri(7,44)*(1+1.5*(1-speed))))
    else S.hold=S.hold-1 end
    local target=math.max(1,int(int(9+CFG.k3*18)*speed))
    S.drag=.85*S.drag+.15*target;local drag=math.max(1,round(S.drag))
    local height=math.max(1,int(int(9+CFG.k3*35)*(.6+.4*speed)))
    local shift=int((rnd()*2-1)*(66*(.25+.75*speed)))
    local activity=.5+.5*speed
    local dx,dy=0,0
    local function applyDrag(st,n)
        if st==1 then dy=dy+sign(2)*n elseif st==2 then dx=dx+sign(1)*n end
    end
    if S.left>0 then
        local w=smooth(1-S.left/math.max(1,S.total))
        applyDrag(S.prev,math.max(1,round(drag*(1-w))))
        applyDrag(S.state,math.max(1,round(drag*w)))
        S.left=S.left-1
        -- No tears/mirrors during this branch, including zero net drag.
    elseif S.state<=2 then applyDrag(S.state,drag)
    elseif S.state==3 then
        if rnd()<.25*activity then
            if not tear(shift,height) then return false end
            if CFG.k2>.66 and rnd()<.15*activity then
                if not tear(math.floor(-shift/2),math.max(1,math.floor(height/2))) then return false end
            end
        end
    elseif S.state==4 and rnd()<.15*activity then
        local h=ri(33,77);local y=ri(0,480-h);local top=int(y*SY)
        if keepHistory and not layer:mirror(0,top,W,math.max(1,int((y+h)*SY)-top)) then return false end
    end
    -- Whole-image source drags commute; combine before one GPU copy. Carry
    -- fractional scaled displacement to avoid amplifying one-pixel morphs.
    local fx,fy=dx*SX+S.rx,dy*SY+S.ry
    local ix,iy=round(fx),round(fy);S.rx=fx-ix;S.ry=fy-iy
    if keepHistory and (ix~=0 or iy~=0) then if not layer:shift(0,0,W,H,ix,iy) then return false end end
    if not layer:paint(function()
        foreground(speed)
        S.fade=(S.fade+1)%6
        -- Keep the original periodic decay inside history. The steady screen
        -- overlay below gives scanlines a fixed place beneath the current bars.
        if S.fade==0 then scanlines() end
    end) then return false end
    -- Original Python trigger block runs after foreground/decay, before t++.
    -- Keep Markov.cur separate: reseeding does not immediately change last_state.
    if pendingTrigger then
        pendingTrigger=false
        S.mcState=ri(1,4);S.hold=0;S.hue=(S.hue+rnd()*.31)%1
        spawn();S.chain=0;S.prev=S.state;S.total=6;S.left=6
        if rnd()<.35 then S.bias[1]=-S.bias[1] end
        if rnd()<.35 then S.bias[2]=-S.bias[2] end
    end
    S.t=S.t+1;return true
end
local function stop(e)
    if failed then return end
    failed=true;log('STOP: '..tostring(e)..'; no automatic retries')
    if layer and not layer.failed then layer:release() end
end
function setup()
    if started then return end
    started=true;failed=false;keepHistory=false;lastPersist=nil;pendingTrigger=false
    bars={};for i=1,9 do bars[i]={} end;barCount=0
    local ok,e=pcall(function()
        assert(type(dofile)=='function','standard dofile unavailable')
        assert(of and type(of.getElapsedTimef)=='function','elapsed-time API unavailable')
        assert(type(of.drawTriangle)=='function','drawTriangle unavailable')
        assert(CFG.mutationHz>=1 and CFG.mutationHz<=30,'mutationHz must be 1..30')
        for _,k in ipairs({'k1','k2','k3','k4','k5'}) do assert(CFG[k]>=0 and CFG[k]<=1,k..' must be 0..1') end
        S={rng=CFG.seed,t=0,state=1,mcState=1,prev=1,chain=0,decisions=0,cohorts=0,total=0,left=0,fade=0,drag=0,dirTick=0,rx=0,ry=0,sign={1,1},bias={},kernel={}}
        -- Match Python setup's separate module-RNG decisions with a second
        -- local stream; its heightmap call reseeds the artistic RNG even when
        -- edge wash is disabled. Preserve that semantic, without scratch blits.
        local aux=CFG.seed*48271%2147483647;S.hold=7+math.floor(aux/2147483647*38)
        aux=aux*48271%2147483647;local variant=math.floor(aux/2147483647*3)
        S.hue=(.13+rnd()*.37)%1;S.rng=33+variant*17
        for i=1,4 do S.kernel[i]={1,1,1,1} end
        S.bias={uniform(-.15,.35),uniform(-.15,.35)};spawn()
        local modulePath=of.toDataPath('lua/hautlle/v1/frozen_layer_pair.lua',true)
        local module=dofile(modulePath)
        assert(module.API_VERSION==1,'Unsupported frozen-layer API')
        log('shared helper: '..modulePath..' API_VERSION='..tostring(module.API_VERSION))
        layer=module.new(log,{width=W,height=H})
        if not layer:paint(function() of.clear(0,0,0,255) end) then failed=true;return end
        lastTime=of.getElapsedTimef();nextAt=lastTime;lastLog=lastTime
        callbacks=0;steps=0;skipped=0
        log('ready CONTROL candidate: OG v2.2 knobs + trigger; clean OFF / retained ON; 30Hz; no audio analysis')
    end)
    if not ok then stop(e) end
end
function update() end
function draw()
    if not started or failed or not layer then return end
    local ok,e=pcall(function()
        readControls()
        -- Latch a one-display-frame host pulse until the next 30Hz mutation.
        if trig==true or (type(trig)=='number' and trig>0) then pendingTrigger=true end
        local wanted=(persist==true or persist==1)
        -- Handle the one-display-frame clear pulse before the 30 Hz scheduler.
        -- Enabling retains the clean frame unless the host requests a clear.
        -- Disabling clears immediately; host clear pulses always take precedence.
        if persistClear==true or persistClear==1 or (lastPersist~=nil and lastPersist~=wanted and not wanted) then
            if not layer:paint(function() of.clear(0,0,0,255) end) then failed=true;return end
            barCount=0 -- A clear between mutations must not resurrect old bars.
        end
        if lastPersist~=wanted then log('PERSIST '..(wanted and 'ON: retained transforms' or 'OFF: clean foreground')) end
        keepHistory=wanted;lastPersist=wanted
        local now=of.getElapsedTimef();callbacks=callbacks+1
        if now<lastTime then nextAt=now;lastLog=now;callbacks=0;steps=0;skipped=0 end
        lastTime=now
        if now+1e-6>=nextAt then
            local due=math.max(1,math.floor((now-nextAt)*CFG.mutationHz+1e-6)+1)
            skipped=skipped+due-1;nextAt=nextAt+due/CFG.mutationHz
            if not tick() then failed=true;return end;steps=steps+1
        end
        if not layer:draw() then failed=true;return end
        drawOverlay()
        if now-lastLog>=10 then
            local dt=now-lastLog
            log(string.format('callbackFPS=%.1f mutationHz=%.1f skipped=%d state=%s decisions=%d cohorts=%d',callbacks/dt,steps/dt,skipped,names[S.state],S.decisions,S.cohorts))
            callbacks=0;steps=0;skipped=0;lastLog=now
        end
    end)
    if not ok then stop(e) end
end
function exit()
    if not layer or layer:release() then layer=nil;bars={};barCount=0;started=false;failed=false;log('exit: cleanup calls returned; GPU deletion unverified')
    else failed=true;log('exit: retained handles; setup blocked') end
end
