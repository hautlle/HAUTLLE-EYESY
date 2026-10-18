local fake=dofile(TEST_ROOT..'/fake_of.lua')
local helper=dofile(HELPER_PATH)
local results={}
local function test(name,body)
    local ok,e=pcall(body)
    assert(ok,name..': '..tostring(e))
    results[#results+1]=name
end
local function fixture(w,h)
    local s=fake.new();of=s.of
    local logs={};local layer=helper.new(function(msg) logs[#logs+1]=msg end,{width=w or 1280,height=h or 720})
    return s,layer,logs
end
local function paint(layer) return layer:paint(function() of.drawRectangle(4,5,6,7) end) end
local function closedAndInert(s,l)
    assert(l.closed)
    local before=fake.json(s.counts)
    assert(not l:paint(function() error('closed callback ran') end))
    assert(not l:shift(0,0,10,10,1,1))
    assert(not l:mirror(0,0,10,10))
    assert(not l:draw())
    assert(fake.json(s.counts)==before,'closed layer made OF calls')
end
test('constructor, first paint and first transform remain lazy',function()
    local s,l=fixture();assert(#s.trace==0)
    assert(l:draw() and #s.trace==0) -- Empty new layer is a successful no-op.
    assert(paint(l));assert(l.initialized and #s.fbos==2 and #s.shaders==0)
    assert(l:shift(0,0,1280,720,2,-1));assert(#s.fbos==2 and #s.shaders==1)
    assert(l:mirror(0,0,1280,720));assert(l:draw());assert(s.uninitializedReads==0)
    s:assertBalanced();assert(l:release());s:assertRetired();closedAndInert(s,l)
end)
test('repeated release retires each owned handle once',function()
    local s,l=fixture();assert(paint(l));assert(l:shift(0,0,1280,720,1,0));assert(l:release())
    local before=fake.json(s.counts)
    for _=1,5 do assert(l:release()) end
    assert(fake.json(s.counts)==before and s.counts.clearFbo==2 and s.counts.unloadShader==1)
    s:assertRetired();closedAndInert(s,l)
end)
test('release before first paint is terminal without allocation',function()
    local s,l=fixture();assert(l:release());assert(l:release());closedAndInert(s,l);assert(#s.trace==0)
end)
for _,operation in ipairs({'shift','mirror'}) do
    test(operation..' before successful initial paint fails without allocation',function()
        local s,l=fixture()
        local ok=operation=='shift' and l:shift(0,0,10,10,1,1) or l:mirror(0,0,10,10)
        assert(not ok and l.failed and not l.initialized and #s.trace==0)
        closedAndInert(s,l);assert(l:release())
    end)
end
test('fresh instances support 50 release/reentry cycles with bounded resources',function()
    local s=fake.new();of=s.of
    for _=1,50 do
        local l=helper.new(function() end,{width=1280,height=720})
        assert(paint(l));assert(l:shift(0,0,1280,720,1,0));assert(l:draw());assert(l:release())
        s:assertRetired();s:assertBalanced();closedAndInert(s,l)
    end
    assert(#s.fbos==100 and #s.shaders==50 and s.counts.clearFbo==100 and s.counts.unloadShader==50)
end)
for _,dimensions in ipairs({{320,180},{640,360},{960,540},{1280,720}}) do
    test('supported dimensions '..dimensions[1]..'x'..dimensions[2],function()
        local s,l=fixture(dimensions[1],dimensions[2]);assert(paint(l))
        assert(l:shift(0,0,dimensions[1],dimensions[2],-1,1));assert(l:release());s:assertBalanced();s:assertRetired()
    end)
end
for _,kind in ipairs({'second allocation throws','partial allocation throws','allocation returns false','backing mismatch','initial clear throws'}) do
    test(kind..' closes, unwinds and retires constructed owners',function()
        local s,l=fixture()
        if kind=='second allocation throws' then s:fail('allocate',2)
        elseif kind=='partial allocation throws' then s:fail('allocate.after')
        elseif kind=='allocation returns false' then s.rejectAllocation=true
        elseif kind=='backing mismatch' then s.badTarget=34037
        else s:fail('clearPixels') end
        local ran=false
        assert(not l:paint(function() ran=true end));assert(not ran and l.failed and not l.initialized)
        assert(#s.shaders==0);s:assertBalanced();s:assertRetired();closedAndInert(s,l)
        local before=fake.json(s.counts);assert(l:release());assert(fake.json(s.counts)==before)
    end)
end
for _,warmed in ipairs({false,true}) do
    test('painting callback error '..(warmed and 'after initialization' or 'during initial paint'),function()
        local s,l=fixture();if warmed then assert(paint(l)) end
        assert(not l:paint(function() of.drawRectangle(1,2,3,4);error('paint failed') end))
        assert(l.failed and l.initialized==warmed);s:assertBalanced();s:assertRetired();closedAndInert(s,l)
        assert(s.parentWrites==0)
    end)
end
for _,kind in ipairs({'compile throws','compile false','link false','uniform throws'}) do
    test('shader '..kind..' closes and retires owners',function()
        local s,l=fixture();assert(paint(l))
        if kind=='compile throws' then s:fail('compile') elseif kind=='compile false' then s.rejectCompile=true
        elseif kind=='link false' then s.rejectLink=true else s:fail('uniform4f') end
        assert(not l:shift(0,0,1280,720,1,0));s:assertBalanced();s:assertRetired();closedAndInert(s,l)
    end)
end
test('failed FBO cleanup retains only failed handle for explicit retry',function()
    local s,l=fixture();assert(paint(l));local first=l.owners[1];s:fail('clearFbo')
    assert(not l:release());assert(l.owners[1]==first and l.owners[2]==nil)
    closedAndInert(s,l);assert(l:release());assert(l.owners[1]==nil and s.counts.clearFbo==3);s:assertRetired()
end)
test('failed shader cleanup retains shader for explicit retry',function()
    local s,l=fixture();assert(paint(l));assert(l:shift(0,0,1280,720,1,0));local sh=l.shader;s:fail('unloadShader')
    assert(not l:release());assert(l.shader==sh and l.owners[1]==nil and l.owners[2]==nil)
    closedAndInert(s,l);assert(l:release());assert(l.shader==nil and s.counts.unloadShader==2);s:assertRetired()
end)
test('paint failure plus cleanup failure preserves retry handle',function()
    local s,l=fixture();assert(paint(l));s:fail('clearFbo')
    assert(not l:paint(function() error('paint') end));assert(l.owners[1] and not l.owners[2])
    closedAndInert(s,l);assert(l:release());s:assertRetired();s:assertBalanced()
end)
for _,operation in ipairs({'endFbo','popStyle'}) do
    test('uncertain '..operation..' unwind retains owners and prevents reuse',function()
        local s,l=fixture();assert(paint(l));s:fail(operation)
        assert(not paint(l));assert(l.uncertain and l.owners[1] and l.owners[2])
        closedAndInert(s,l);local before=fake.json(s.counts)
        assert(not l:release());assert(fake.json(s.counts)==before)
        -- Do not pretend the failed host scope was repaired by this Lua helper.
    end)
end
return fake.json({passed=#results,cases=results,scope='Actual Lua helper with deterministic OF stubs; no GPU-driver, pixel, timing or native retirement proof.'})
