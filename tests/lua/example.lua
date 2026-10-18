local fake=dofile(TEST_ROOT..'/fake_of.lua')
local s=fake.new();of=s.of
function print() end
assert(loadfile(EXAMPLE_PATH))()
setup();assert(#s.trace==0)
trig=false;draw();assert(#s.fbos==2 and #s.shaders==0);s:assertBalanced()
trig=true;draw();assert(#s.shaders==1);s:assertBalanced()
exit();exit();s:assertRetired()
setup();s:fail('allocate',2);draw();s:assertRetired();s:assertBalanced()
local count=#s.trace;draw();assert(#s.trace==count,'example retries failed initialization')
exit();setup();s:fail('drawRectangle');draw();s:assertRetired();s:assertBalanced()
count=#s.trace;draw();assert(#s.trace==count,'example retries failed paint')
exit();setup();draw();s:assertBalanced();exit();s:assertRetired()
return fake.json({load_from_packaged_data_path=true,lazy_setup=true,triggered_shift=true,reentry=true,initialization_and_paint_error=true})
