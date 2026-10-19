// Copyright (c) 2026, HAUTLLE. BSD-3-Clause; see ../LICENSE.txt.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
let source=fs.readFileSync(process.argv[2],'utf8'), requests=[],prompts=[],status='',value='saved',confirm=true;
function $(x){if(typeof x==='function')return;return {text:s=>{status=s;},html:()=>{}};}
$.post=(url,data)=>{let req={url,data,done:null,fail:null};requests.push(req);let chain={done:f=>{req.done=f;return chain;},fail:f=>{req.fail=f;return chain;}};return chain;};
$.get=(url,f)=>{requests.push({url,get:f});};
const c={$,location:{host:'fixture'},window:{confirm:m=>{prompts.push(m);return confirm;}},console};
vm.createContext(c);vm.runInContext(source,c);c.editor={getValue:()=>value,setValue:s=>{value=s;},gotoLine:()=>{}};
const path='/sdcard/Modes/oFLua/HAUTLLE_MARKOV_LIVE_720_V1/main.lua';
function reset(p=path,dirty=false){requests=[];prompts=[];status='';value=dirty?'edited':'saved';confirm=true;c.currentEditorFile=p;c.loadedEditorContents='saved';c.workingDir='/sdcard/Modes/oFLua/OTHER_MODE';}
reset();c.loadDisplayedMode();assert.equal(requests.length,1);assert.equal(requests[0].data.mode,path);assert.equal(requests[0].url,'http://fixture/mode_set');assert.equal(prompts.length,0);
reset(path,true);confirm=false;c.loadDisplayedMode();assert.equal(requests.length,0);assert.equal(value,'edited');assert(prompts[0].includes('saved version'));
reset(path,true);c.loadDisplayedMode();assert.equal(requests.length,1);assert.equal(value,'edited');assert(!('contents' in requests[0].data));
for(const p of ['/sdcard/Modes/oFLua/X/core.lua','/sdcard/Modes/oFLua/X/nested/main.lua','/sdcard/Modes/oFLua/../main.lua','/etc/main.lua','...']){reset(p);c.loadDisplayedMode();assert.equal(requests.length,0,p);}
reset('sdcard/Modes/oFLua/GAUSSIAN GLITCH/main.lua');c.loadDisplayedMode();assert.equal(requests[0].data.mode,'/sdcard/Modes/oFLua/GAUSSIAN GLITCH/main.lua');
reset(path,true);c.saveMode();assert.equal(c.loadedEditorContents,'saved');requests[0].done('SAVED');assert.equal(c.loadedEditorContents,'edited');requests=[];c.loadDisplayedMode();assert.equal(prompts.length,0);
reset(path,true);c.saveMode();c.currentEditorFile='/sdcard/Modes/oFLua/OTHER/main.lua';c.loadedEditorContents='other';requests[0].done('SAVED');assert.equal(c.loadedEditorContents,'other');
reset(path,true);c.saveMode();value='newer edits';requests[0].done('SAVED');requests=[];c.loadDisplayedMode();assert.equal(prompts.length,1);assert.equal(value,'newer edits');
reset();c.loadDisplayedMode();requests[0].fail();assert(status.startsWith('Load failed'));
reset();c.getFile(path);requests[0].get('loaded content');assert.equal(c.loadedEditorContents,'loaded content');assert.equal(c.currentEditorFile,path);
console.log('PASS: displayed-file targeting, clean load, unsaved cancel/confirm, invalid paths, spaces/relative path, successful save baseline, save/file race, edits during save, request failure and load baseline. No network calls.');
