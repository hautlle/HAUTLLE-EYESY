-- FRACSHAT / ARCHITECTURAL GHOSTS F1 -- isolated audition candidate, no host edits.
local W,H=1280,720
local layer,core,started,failed,lastPersist,nextAt,lastNow
local K={.7,.8,.35,.5,.5}
inL={};inR={}
local function finite(v) return type(v)=='number' and v==v and math.abs(v)~=math.huge end
local function yes(v) return v==true or (finite(v) and v>0) end
local function clamp(v,a,b) return math.max(a,math.min(b,v)) end
local rng=20260417
local R={}
function R.random() rng=rng*48271%2147483647;return rng/2147483647 end
function R.uniform(a,b) return a+(b-a)*R.random() end
local function ri(n) return math.floor(R.random()*n) end
local D={}
local function color(c) of.setColor(c[1],c[2],c[3],255) end
function D.clear() of.clear(0,0,0,255) end
function D.fade(a)
 of.enableAlphaBlending();of.fill();of.setColor(0,0,0,a);of.drawRectangle(0,0,W,H);of.disableAlphaBlending()
end
function D.circle(c,x,y,r) color(c);of.noFill();of.setLineWidth(1);of.setCircleResolution(128);of.drawCircle(x,y,r) end
function D.line(c,x,y,xx,yy) color(c);of.setLineWidth(1);of.drawLine(x,y,xx,yy) end
function D.arc(c,x,y,w,h,a,b)
 color(c);of.noFill();of.setLineWidth(1)
 -- A single bounded line strip, not hundreds of separate Lua draw calls.
 local n=48;of.beginShape()
 for i=0,n do local t=a+(b-a)*i/n;of.vertex(x+w/2+math.cos(t)*w/2,y+h/2-math.sin(t)*h/2) end
 of.endShape(false)
end
local function palette(p)
 local host=FRACSHAT_HOST or {}
 if type(host.color_picker_bg)=='function' then host.color_picker_bg(p) end
 if type(host.color_picker)=='function' then return host.color_picker(p) end
 -- Original EYESY OS3.1 slot-0 foreground picker, including random thresholds.
 local g=ri(2)*255;local c={g,g,g}
 if p>.02 then g=ri(255);c={g,g,g} end
 for _,v in ipairs({{.04,50},{.06,100},{.08,150},{.10,150},{.12,200},{.14,250}}) do if p>v[1] then c={v[2],v[2],v[2]} end end
 if p>.16 then c={(math.sin(p*2*math.pi)*.5+.5)*255,(math.sin(p*4*math.pi)*.5+.5)*255,(math.sin(p*8*math.pi)*.5+.5)*255} end
 if p>.96 then c={ri(255),ri(255),ri(255)} end
 if p>.98 then c={ri(2)*255,ri(2)*255,ri(2)*255} end
 return c
end
local function audio()
 local host=FRACSHAT_HOST or {};local a,b=0,0
 -- Explicit normalized scalar adapters preserve recovered callable semantics.
 if type(host.audio)=='function' then local ok,v=pcall(host.audio);if ok and finite(v) then a=math.abs(v) end
 else
  -- Modern OF bridge: mean absolute first100 normalized left samples.
  -- This is an explicit compatibility mapping, not a recovered Python API.
  local n=math.min(100,#inL);for i=1,n do if finite(inL[i]) then a=a+math.abs(inL[i]) end end
  if n>0 then a=a/n end
 end
 b=a*.6
 if type(host.audio_low)=='function' then local ok,v=pcall(host.audio_low);if ok and finite(v) then b=math.abs(v) end end
 return a,b
end
local function stop(err) failed=true;print('[FracShat F1] STOP '..tostring(err));if layer then layer:release() end end
function setup()
 if started or layer then return end;started=true;failed=false;lastPersist=nil
 local ok,e=pcall(function()
  local cfg=FRACSHAT_CONFIG or {};rng=cfg.seed or 20260417
  assert(finite(rng) and rng>=1 and rng<2147483647 and rng==math.floor(rng),'invalid seed')
  -- Full source resolution preserves 1px strokes, integer displacement and tile sizes.
  for _,name in ipairs({'noFill','setLineWidth','setCircleResolution','drawCircle','drawLine','beginShape','vertex','endShape','enableAlphaBlending'}) do assert(type(of[name])=='function','missing OF API '..name) end
  lastNow=of.getElapsedTimef();assert(finite(lastNow),'invalid clock');nextAt=lastNow
  core=dofile('core.lua')(R,D,W,H,lastNow)
  layer=dofile('region_pair.lua').new(print,{width=W,height=H})
 end)
 if not ok then stop(e) end
end
function update() end
function draw()
 if failed or not layer then return end
 local ok,e=pcall(function()
  for i=1,5 do local v=_G['knob'..i];if finite(v) then K[i]=clamp(v,0,1) end end
  local now=of.getElapsedTimef();assert(finite(now),'invalid clock')
  local keep= yes(persist);local explicit= yes(persistClear)
  local transition=lastPersist~=nil and keep~=lastPersist
  if now<lastNow then nextAt=now;core.state.last_time=now end;lastNow=now
  -- Transition/clear callbacks force a frame, even between scheduled ticks.
  -- The old accumulated frame is never presented after switching PERSIST off.
  if now+1e-6>=nextAt or transition or explicit then
   -- Advance the existing deadline by whole periods; keep fractional remainder.
   -- Skip missed slots without catch-up loops. Early transition/clear keeps phase.
   if now+1e-6>=nextAt then
    nextAt=nextAt+(math.floor((now-nextAt+1e-6)*30)+1)/30
   end
   local a,b=audio()
   core.frame(now,K,a,b,keep,explicit,palette,
    function(cb) assert(layer:paint(cb),'paint failed') end,
    function(patches) assert(layer:displace(patches),'displacement failed') end)
   if type(FRACSHAT_OBSERVE)=='function' then FRACSHAT_OBSERVE(core.state) end
  end
  lastPersist=keep;assert(layer:draw(),'presentation failed')
 end)
 if not ok then stop(e) end
end
function exit()
 if not layer or layer:release() then layer=nil;core=nil;started=false;failed=false;lastPersist=nil else failed=true end
end
