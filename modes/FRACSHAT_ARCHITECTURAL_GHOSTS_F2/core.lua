-- Architectural Ghosts v3.1 arithmetic/ordering translated from verified 4f4aa4b0bf.
-- No renderer, host, or file access. R supplies random()/uniform(); D supplies drawing.
local tau=math.pi*2
local function clamp(v,a,b) return math.max(a,math.min(b,v)) end
local function int(v) return v<0 and math.ceil(v) or math.floor(v) end
local function round(v) local n=math.floor(v);if v-n==.5 then return n+n%2 end;return math.floor(v+.5) end
return function(R,D,W,H,now)
 local S={start_time=now,last_time=now,clock=0,seqs={},anchors={},arcs={},trail_boost=0}
 for _,n in ipairs({5,7,13,23}) do
  local s={i=0,v=0,values={},length=n};for j=1,n do s.values[j]=R.random() end
  S.seqs[#S.seqs+1]=s
 end
 local function anchor(a)
  a.x=R.random();a.y=R.random();a.r=R.uniform(.08,.18);a.life=R.uniform(60,140)
 end
 for i=1,10 do S.anchors[i]={};anchor(S.anchors[i]) end
 S.era_bias=R.random()*tau
 local function step(s) s.i=(s.i+1)%s.length;s.v=s.values[s.i+1] end
 local function color(fg,b) b=clamp(b,0,1);return {int(fg[1]*b),int(fg[2]*b),int(fg[3]*b)} end
 local core={state=S}
 function core.frame(t,k,audio,bass,keep,clear,palette,paint,displace)
  local dt=clamp(t-S.last_time,0,.1);S.last_time=t
  if int((t-S.start_time)/90)~=int((t-dt-S.start_time)/90) then S.era_bias=R.random()*tau end
  local contrast=clamp(1+.18*math.sin(t*.1),.6,1.15)
  local p=(k[5]+(S.seqs[4].v-.5)*.45+.015*S.seqs[2].v)%1
  local fg=palette(p);S.palette_pos=p
  S.trail_boost=math.max(bass*.9,S.trail_boost-dt*.8)
  local eff=clamp(clamp(k[2]*k[2],0,1)+S.trail_boost*.8,0,1.2)
  -- pygame Surface.set_alpha clamps a negative integer to zero.
  S.fade=clamp(int(1+18*(1-eff)),0,255)
  paint(function() if not keep or clear then D.clear() end;if keep then D.fade(S.fade) end end)
  S.clock=S.clock+dt*(.06+audio*2.2*k[1])
  if S.clock>=1 then
   S.clock=S.clock-1;step(S.seqs[1])
   if R.random()<.7 then step(S.seqs[2]) end
   if S.seqs[2].v>.6 then step(S.seqs[3]) end
   if S.seqs[3].v>.7 then step(S.seqs[4]) end
   if R.random()<.05+k[3]*.2 then
    S.arcs[#S.arcs+1]={x=R.uniform(W*.25,W*.75),y=R.uniform(H*.25,H*.75),r=R.uniform(40,120),a=R.random()*tau,life=R.uniform(25,70)}
    if #S.arcs>12 then table.remove(S.arcs,1) end
   end
  end
  for _,s in ipairs(S.seqs) do for j=1,s.length do s.values[j]=clamp(s.values[j]+(R.random()-.5)*(.0006+.002*k[3]),0,1) end end
  local melt=int(clamp(2+k[4]*4+audio*6,0,12));S.melt=melt
  local patches={}
  if melt>0 then
   local base=math.max(80,int(math.min(W,H)*.22))
   local angles={S.seqs[1].v*tau,S.seqs[3].v*tau+math.pi*.5,S.seqs[4].v*tau+S.era_bias}
   local dirs={};for j,a in ipairs(angles) do dirs[j]={int(math.cos(a)*melt),int(math.sin(a)*melt)} end
   local i,y=0,0
   while y<H do
    local h=int(base*(.6+.8*S.seqs[(i+1)%4+1].v));local x=0
    while x<W do
     local w=int(base*(.6+.8*S.seqs[(i+2)%4+1].v));local d=dirs[i%3+1]
     -- pygame clips source rect to the snapshot before destination clipping.
     patches[#patches+1]={x,y,math.min(w,W-x),math.min(h,H-y),x+d[1],y+d[2]}
     x=x+w;i=i+1
    end
    y=y+h
   end
  end
  S.patches=patches
  -- Fresh geometry MUST follow the frozen-snapshot displacement.
  displace(patches)
  paint(function()
   local cx,cy=math.floor(W/2),math.floor(H/2)
   for i,a in ipairs(S.anchors) do
    a.life=a.life-dt;if a.life<=0 then anchor(a) end
    D.circle(color(fg,(.45+.55*S.seqs[(i-1)%4+1].v)*contrast),int(cx+(a.x-.5)*W*.85),int(cy+(a.y-.5)*H*.85),int(a.r*math.min(W,H)))
   end
   local alive={}
   for _,arc in ipairs(S.arcs) do
    arc.life=arc.life-dt
    if arc.life>0 then
     alive[#alive+1]=arc;arc.a=arc.a+dt*(.12+.1*S.seqs[3].v)
     local ang=round((arc.a+S.era_bias)/(math.pi/4))*(math.pi/4)
     D.arc(color(fg,(.35+.65*arc.life/(arc.life+10))*contrast),int(arc.x-arc.r),int(arc.y-arc.r),int(arc.r*2),int(arc.r*2),ang,ang+math.pi*.75)
    end
   end
   S.arcs=alive
   for i,s in ipairs(S.seqs) do
    local ang=s.v*tau+t*.2*(.25+(i-1)*.08)+S.era_bias
    D.line(color(fg,(.4+.6*s.v)*contrast),cx+math.cos(ang)*W*.45,cy+math.sin(ang)*H*.45,cx-math.cos(ang)*W*.45,cy-math.sin(ang)*H*.45)
   end
  end)
 end
 return core
end
