/*
 * Copyright (c) 2020 Owen Osborn, Critter & Gutiari, Inc.
 *
 * BSD Simplified License.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 *
 */
#include "ofApp.h"
#include "CpuStatusPanel.h"
#include <cstdio>
#include <sstream>
#include <exception>



//--------------------------------------------------------------

// --- OSD NETWORK HELPER ---
static std::string haGetLanIpForOsd() {
    static std::string cached = "NO IP";
    static uint64_t lastMs = 0;

    uint64_t nowMs = ofGetElapsedTimeMillis();
    if (nowMs - lastMs < 2500) {
        return cached;
    }
    lastMs = nowMs;

    FILE* pipe = popen("hostname -I 2>/dev/null", "r");
    if (!pipe) {
        cached = "NO IP";
        return cached;
    }

    char buf[512];
    std::string out;
    while (fgets(buf, sizeof(buf), pipe) != NULL) {
        out += buf;
    }
    pclose(pipe);

    std::stringstream ss(out);
    std::string ip;
    while (ss >> ip) {
        if (ip.find("127.") == 0) continue;
        if (ip.find(":") != std::string::npos) continue;
        cached = ip;
        return cached;
    }

    cached = "NO IP";
    return cached;
}
// --- end OSD NETWORK HELPER ---

// Single ofApp instance owns this helper; no GL work in its constructor.
static CpuStatusPanel haCpuStatus;

void ofApp::setup() {
   
    // listen on the given port
    cout << "listening for osc messages on port " << PORT << "\n";
    receiver.setup(PORT);    
    
    knobRecorderOscSender.setup("127.0.0.1", 4010);
    ofSetVerticalSync(true);
    ofSetFrameRate(60);
    ofSetLogLevel("ofxLua", OF_LOG_VERBOSE);

    ofHideCursor();

    ofSetBackgroundColor(0,0,0);
        
    // setup audio
    soundStream.printDeviceList();

    int bufferSize = 256;

    left.assign(bufferSize, 0.0);
    right.assign(bufferSize, 0.0);

    bufferCounter    = 0;

    ofSoundStreamSettings settings;

    auto allDevices = soundStream.getDeviceList();
    cout << "=== Audio input devices ===" << endl;
    for(size_t i = 0; i < allDevices.size(); i++){
        cout << "  [" << i << "] " << allDevices[i].name
             << " (in:" << allDevices[i].inputChannels
             << ", out:" << allDevices[i].outputChannels << ")" << endl;
    }

    vector<string> searchOrder = {
        "audioinjector",
        "AudioInjector",
        "wm8731",
        "hw:2,0",
        "audioinjector-pi-soundcard"
    };

    bool foundInput = false;
    for(auto &term : searchOrder){
        auto devices = soundStream.getMatchingDevices(term);
        for(auto &device : devices){
            if(device.inputChannels >= 2){
                settings.setInDevice(device);
                cout << "Audio: matched '" << term << "' -> " << device.name << endl;
                foundInput = true;
                break;
            }
        }
        if(foundInput) break;
    }

    if(!foundInput){
        cout << "WARNING: no AudioInjector input match found; using default input device" << endl;
    }

    settings.setInListener(this);
    settings.sampleRate = 44100;
    settings.numOutputChannels = 0;
    settings.numInputChannels = 2;
    settings.bufferSize = bufferSize;
    soundStream.setup(settings);    

    //some path, may be absolute or relative to bin/data
    string path = "/sdcard/Modes/oFLua"; 
    ofDirectory dir(path);
    dir.listDir();

    //go through and print out all the paths
    for(int i = 0; i < dir.size(); i++){
        ofLogNotice(dir.getPath(i) + "/main.lua");
        scripts.push_back(dir.getPath(i) + "/main.lua");
    }
        
    // scripts to run
    currentScript = 0;
    
    // init the lua state
    lua.init(true); // true because we want to stop on an error
    // --- NATIVE BRIDGE: populate once, register after init ---
    if (nativeGenerators.empty()) {
        nativeGenerators.push_back(&shaderHostGenerator);
        nativeGenerators.push_back(&lowResShaderHostGenerator);
        nativeGenerators.push_back(&markovMoshGenerator);
        nativeGenerators.push_back(&meltLockGenerator);
        nativeGenerators.push_back(&bauhausFormGenerator);
        nativeGenerators.push_back(&vectorImpactGenerator);
        nativeGenerators.push_back(&massStWalkGenerator);
        nativeGenerators.push_back(&murmurationFieldGenerator);
        nativeGenerators.push_back(&architectureGenerator);
        nativeGenerators.push_back(&robotFightGenerator);

        // Interference consumes this texture even though Markov uses direct OF/Lua.
        shaderHostGenerator.bindExternalTexture("u_markovMoshTex", &markovMoshGenerator.getTexture());
        // SHADERHOST LAZY EXTERNAL ASSET 01
        // These are FBO-backed assets. Register prepare/peek callbacks
        // instead of allocating both generators during engine startup.
        shaderHostGenerator.bindExternalTextureProvider(
            "u_bauhausTex",
            [this]() {
                bauhausFormGenerator.prepareTexture();
            },
            [this]() -> ofTexture* {
                return bauhausFormGenerator.getTextureIfAllocated();
            });

        shaderHostGenerator.bindExternalTextureProvider(
            "u_meltlockTex",
            [this]() {
                meltLockGenerator.prepareTexture();
            },
            [this]() -> ofTexture* {
                return meltLockGenerator.getTextureIfAllocated();
            });
    }
    registerAllGenerators(lua, nativeGenerators);
    // --- end NATIVE BRIDGE ---



    
    // listen to error events
    lua.addListener(this);
    
    // run a script
    // true = change working directory to the script's parent dir
    // so lua will find scripts with relative paths via require
    // note: changing dir does *not* affect the OF data path
    lua.doScript(scripts[currentScript], true);
    
    // call the script's setup() function
    lua.scriptSetup();

    // clear main screen
    ofClear(0,0,0);

    osdEnabled = 0;
    haCpuStatus.setup();
    dummyAudio = 0;
    audioPeak               = 0.0f;
    audioTrigThreshold      = 0.60f;
    audioTrigResetThreshold = 0.35f;
    audioTrigArmed          = true;
    lastAudioTrigMs         = 0;
    audioTrigCooldownMs     = 100;
    button3LastPressMs    = 0;
    shiftHeld = false;
    persistPressSuppressed = false;

    // --- KNOB RECORDER ---
    sceneAHeld = false;
    sceneBPhysicalHeld = false;
    sceneBTapCount = 0;
    sceneBLastTapMs = 0;
    knobRecStateLastSentMs = 0;
    for (int i = 0; i < 5; i++) {
        rawKnob01[i] = 0.0f;
        effectiveKnob01[i] = 0.0f;
        clearBaselineRaw01[i] = 0.0f;
        clearGestureMoved[i] = false;
    }
    // --- end KNOB RECORDER ---
    persistEnabled          = false;
    persistFboReady         = false;
    persistFboAllocAttempted = false;
    persistClearRequested   = false;
    persistFboW             = 1280;
    persistFboH             = 720;
    persistFadeAlpha        = 12;
}


void ofApp::sendKnobRecEvent(const std::string& eventName, int laneOneBased) {
    ofxOscMessage m;
    m.setAddress("/knobrec/event");
    m.addStringArg(eventName);
    m.addIntArg(laneOneBased);

    int active = 0;
    int armed = 0;
    int loopMs = 0;
    int points = 0;

    if (laneOneBased >= 1 && laneOneBased <= KnobRecorderManager::kLanes) {
        int idx = laneOneBased - 1;
        active = knobRecorder.laneActive(idx) ? 1 : 0;
        armed  = knobRecorder.laneArmed(idx) ? 1 : 0;
        loopMs = (int)knobRecorder.laneLoopDurationMs(idx);
        points = (int)knobRecorder.lanePointCount(idx);
    }

    m.addIntArg(active);
    m.addIntArg(armed);
    m.addIntArg(loopMs);
    m.addIntArg(points);
    knobRecorderOscSender.sendMessage(m, false);
}

void ofApp::sendKnobRecStateIfDue(uint64_t nowMs) {
    if (nowMs - knobRecStateLastSentMs < kKnobRecStateIntervalMs) return;
    knobRecStateLastSentMs = nowMs;

    ofxOscMessage m;
    m.setAddress("/knobrec/state");

    for (int i = 0; i < KnobRecorderManager::kLanes; i++) {
        m.addIntArg(knobRecorder.laneActive(i) ? 1 : 0);
    }
    for (int i = 0; i < KnobRecorderManager::kLanes; i++) {
        m.addIntArg(knobRecorder.laneArmed(i) ? 1 : 0);
    }

    m.addIntArg(shiftHeld ? 1 : 0);
    m.addIntArg(sceneAHeld ? 1 : 0);
    knobRecorderOscSender.sendMessage(m, false);
}

//--------------------------------------------------------------
void ofApp::update() {

    // check for waiting messages
    while(receiver.hasWaitingMessages()){
        // get the next message
        ofxOscMessage m;
        receiver.getNextMessage(m);
        //cout << "new message on port " << PORT << m.getAddress() << "\n";
        // The inspected beta controller emits /shift i; the custom controller
        // emits /key 2 i. Normalize only these explicit routes. Unknown keys
        // retain their existing behavior; malformed control packets are ignored.
        const bool keyMessage = m.getAddress() == "/key" && m.getNumArgs() == 2 &&
            m.getArgType(0) == OFXOSC_TYPE_INT32 && m.getArgType(1) == OFXOSC_TYPE_INT32;
        const bool shiftMessage = m.getAddress() == "/shift" && m.getNumArgs() == 1 &&
            m.getArgType(0) == OFXOSC_TYPE_INT32 &&
            (m.getArgAsInt32(0) == 0 || m.getArgAsInt32(0) == 1);
        if (keyMessage || shiftMessage) {
            int keyId = shiftMessage ? 2 : m.getArgAsInt32(0);
            int keyVal = m.getArgAsInt32(shiftMessage ? 0 : 1);

            // --- KNOB RECORDER: Shift edge tracking ---
            if (keyId == 2) {
                bool wasHeld = shiftHeld;
                shiftHeld = keyVal > 0;
                uint64_t nowMs = ofGetElapsedTimeMillis();

                if (shiftHeld && !wasHeld) {
                    knobRecorder.shiftDown(nowMs);
                    sendKnobRecEvent("shift_down", 0);
                }

                if (!shiftHeld && wasHeld) {
                    std::vector<int> committed = knobRecorder.shiftUp(nowMs);
                    sendKnobRecEvent("shift_up", 0);
                    for (int laneIdx : committed) {
                        sendKnobRecEvent("lane_commit", laneIdx + 1);
                    }

                    sceneAHeld = false;
                    sceneBPhysicalHeld = false;
                    sceneBTapCount = 0;
                    sceneBLastTapMs = 0;
                    for (int i = 0; i < 5; i++) {
                        clearGestureMoved[i] = false;
                    }
                }
            }
            // --- end KNOB RECORDER: Shift edge tracking ---

            // Scene A / Back.
            // With Shift held, this becomes the clear-one-lane modifier.
            // Without Shift, existing mode navigation is unchanged.
            //
            // Hardware note:
            // The physical Scene A / Back button reports keyId 4 normally,
            // but keyId 6 when Shift is already held. Treat both as Scene A
            // for shifted clear-one behavior. Only keyId 4 can navigate back.
            if (keyId == 4 || keyId == 6) {
                if (shiftHeld) {
                    bool pressedNow = keyVal > 0;

                    if (pressedNow && !sceneAHeld) {
                        for (int i = 0; i < 5; i++) {
                            clearBaselineRaw01[i] = rawKnob01[i];
                            clearGestureMoved[i] = false;
                        }
                    }

                    sceneAHeld = pressedNow;
                } else if (keyId == 4 && keyVal > 0) {
                    cout << "back" << "\n";
                    if (persistEnabled) { persistClearRequested = true; }
                    prevScript();
                }
            }

            // Scene B / Forward.
            // With Shift held, double-tap clears all knob recorder lanes.
            // Without Shift, existing mode navigation is unchanged.
            //
            // Hardware note:
            // The physical Scene B / Forward button reports keyId 5 normally,
            // but keyId 7 when Shift is already held. Treat both as Scene B
            // for shifted clear-all behavior. Only keyId 5 can navigate fwd.
            if (keyId == 5 || keyId == 7) {
                bool pressedNow = keyVal > 0;

                if (shiftHeld) {
                    if (pressedNow && !sceneBPhysicalHeld) {
                        uint64_t nowMs = ofGetElapsedTimeMillis();
                        sceneBTapCount =
                            (nowMs - sceneBLastTapMs <= kSceneBDoubleTapWindowMs)
                                ? sceneBTapCount + 1
                                : 1;
                        sceneBLastTapMs = nowMs;

                        if (sceneBTapCount >= 2) {
                            knobRecorder.clearAll();
                            sendKnobRecEvent("clear_all", 0);
                            sceneBTapCount = 0;
                        }
                    }
                    sceneBPhysicalHeld = pressedNow;
                } else {
                    sceneBPhysicalHeld = pressedNow;
                    if (keyId == 5 && pressedNow) {
                        cout << "fwd" << "\n";
                        if (persistEnabled) { persistClearRequested = true; }
                        nextScript();
                    }
                }
            }
            if (keyId == 9 && keyVal > 0) {
                img.grabScreen(0,0,ofGetWidth(),ofGetHeight());
                string fileName = "snapshot_"+ofToString(10000+snapCounter)+".png";
                cout << "saving " + fileName + "...";
                img.save("/sdcard/Grabs/" + fileName);
                cout << "saved\n";
                snapCounter++;
            }
            if (keyId == 10 && keyVal == 0) dummyAudio = 0;
            if (keyId == 10 && keyVal > 0) {
		dummyAudio = 1;
                cout << "trig" << "\n";
                lua.setBool("trig", true);
            }
	    if (keyId == 3) {
                uint64_t nowButton3Ms = ofGetElapsedTimeMillis();

                // Retired shortcut: shifted PERSIST presses have no action.
                // Keep suppression until release even if Shift is released first.
                // Normal PERSIST, recorder Shift edges and mode-owned memory are separate.
                if (keyVal <= 0) {
                    persistPressSuppressed = false;
                } else {
                    if (shiftHeld) persistPressSuppressed = true;
                    if (!persistPressSuppressed) {
                        if (nowButton3Ms - button3LastPressMs >= 350) {
                            button3LastPressMs = nowButton3Ms;
                            persistEnabled = !persistEnabled;
                            persistClearRequested = persistEnabled;
                            if (persistEnabled && !persistFboReady) {
                                persistFboAllocAttempted = false;
                            }
                            cout << "persist: " << (persistEnabled ? "ON" : "OFF") << "\n";
                        }
                    }
                }
            }
       	    if (keyId == 1 && keyVal > 0) {
		osdEnabled = osdEnabled ? 0 : 1;
            	cout << "change OSD: " << osdEnabled << "\n";
            } 

	
	}
        if(m.getAddress() == "/knobs") {
            for (int i = 0; i < 5; i++) {
                float raw = (float)m.getArgAsInt32(i) / 1023.0f;
                if (raw < 0.0f) raw = 0.0f;
                if (raw > 1.0f) raw = 1.0f;
                rawKnob01[i] = raw;
            }
        }
        if (m.getAddress() == "/set") {
            string requested = "";
            if (m.getNumArgs() > 0) requested = m.getArgAsString(0);
            setScriptByName(requested);
        }

        if(m.getAddress() == "/reload") {
            cout << "reloading\n";
            reloadScript();
        }
    }
    

    // Apply knob recorder every frame, using the latest raw knob values.
    // This is required because /knobs may only arrive when physical knobs move,
    // but recorded playback needs to advance continuously.
    {
        uint64_t nowMs = ofGetElapsedTimeMillis();
        static const char* knobNames[5] = {"knob1", "knob2", "knob3", "knob4", "knob5"};

        for (int i = 0; i < 5; i++) {
            float eff = rawKnob01[i];

            if (shiftHeld && sceneAHeld) {
                float delta = rawKnob01[i] - clearBaselineRaw01[i];
                if (delta < 0.0f) delta = -delta;

                if (!clearGestureMoved[i] && delta > KnobRecorderManager::kDeadband) {
                    clearGestureMoved[i] = true;
                    knobRecorder.clearLane(i);
                    knobRecorder.suppressLaneForCurrentShift(i);
                    sendKnobRecEvent("lane_clear", i + 1);
                }

                eff = rawKnob01[i];
            } else {
                bool justArmed = false;
                eff = knobRecorder.effectiveValue(i, rawKnob01[i], nowMs, &justArmed);
                if (justArmed) {
                    sendKnobRecEvent("lane_armed", i + 1);
                }
            }

            effectiveKnob01[i] = eff;
            lua.setNumber(knobNames[i], eff);
        }
    }

    sendKnobRecStateIfDue(ofGetElapsedTimeMillis());

    // call the script's update() function
    lua.scriptUpdate();

}

//--------------------------------------------------------------
void ofApp::draw() {
    
    lua.setNumberVector("inL", left);
    lua.setNumberVector("inR", right);
    lua.setNumber("peak", audioPeak);

    // Audio trig: one-frame pulse with re-arm and cooldown
    uint64_t nowMs = ofGetElapsedTimeMillis();
    if (audioPeak < audioTrigResetThreshold) {
        audioTrigArmed = true;
    }
    if (audioTrigArmed &&
        audioPeak >= audioTrigThreshold &&
        (nowMs - lastAudioTrigMs) >= audioTrigCooldownMs) {
        lua.setBool("trig", true);
        audioTrigArmed  = false;
        lastAudioTrigMs = nowMs;
    }
    
    // Patch3D always-on internal render canvas.
    //
    // Important design rule:
    //   Lua always draws into the same 1280x720 canvas.
    //   The engine always draws that canvas to the HDMI/window.
    //   PERSIST only changes the Lua global `persist`.
    //
    // The engine does NOT draw a black fade rectangle anymore.
    // Persist-compatible Lua modes own their own background/fade color.
    //
    // Lua convention:
    //   renderW/renderH = internal canvas size
    //   screenW/screenH = HDMI/window size
    //   persist = true/false from the hardware PERSIST button
    //
    // Mode convention:
    //   if persist == false: draw/clear normal background
    //   if persist == true: avoid hard clear, or draw a translucent fade using the mode's own bg color

    if (!persistFboReady && !persistFboAllocAttempted) {
        persistFboAllocAttempted = true;
        persistFboW = 1280;
        persistFboH = 720;

        ofLogNotice("renderCanvas") << "Trying single FBO " << persistFboW << "x" << persistFboH;

        ofFboSettings s;
        s.width = persistFboW;
        s.height = persistFboH;
        s.internalformat = GL_RGBA;
        s.useDepth = false;
        s.useStencil = false;
        s.numSamples = 0;
        s.textureTarget = GL_TEXTURE_2D;

        persistFbo.clear();
        persistFbo.allocate(s);

        if (persistFbo.isAllocated()) {
            persistFbo.begin();

            GLenum canvasStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (canvasStatus == GL_FRAMEBUFFER_COMPLETE) {
                ofClear(0, 0, 0, 255);
                persistFbo.end();

                persistFboReady = true;
                persistClearRequested = false;

                ofLogNotice("renderCanvas") << "FBO ready at " << persistFboW << "x" << persistFboH;
            } else {
                persistFbo.end();

                persistFboReady = false;
                persistFboAllocAttempted = false;
                persistEnabled = false;
                persistClearRequested = false;

                ofLogError("renderCanvas") << "FBO incomplete status " << canvasStatus << "; using direct fallback";
            }
        } else {
            persistFboReady = false;
            persistFboAllocAttempted = false;
            persistEnabled = false;
            persistClearRequested = false;

            ofLogError("renderCanvas") << "FBO allocation FAILED at " << persistFboW << "x" << persistFboH << "; using direct fallback";
        }
    }

    if (persistFboReady) {
        persistFbo.begin();

        GLenum canvasStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (canvasStatus != GL_FRAMEBUFFER_COMPLETE) {
            persistFbo.end();

            persistFboReady = false;
            persistFboAllocAttempted = false;
            persistEnabled = false;
            persistClearRequested = false;

            ofLogError("renderCanvas") << "FBO incomplete status " << canvasStatus << "; using direct fallback";

            lua.setBool("persist", false);
            lua.setBool("persistClear", false);
            lua.setNumber("renderW", ofGetWidth());
            lua.setNumber("renderH", ofGetHeight());
            lua.setNumber("screenW", ofGetWidth());
            lua.setNumber("screenH", ofGetHeight());
            lua.setNumber("persistBufferW", persistFboW);
            lua.setNumber("persistBufferH", persistFboH);
            lua.setNumber("persistFade", persistFadeAlpha);
            lua.scriptDraw();
        } else {
            // Patch3D3:
            // The engine no longer clears or fades the internal canvas here.
            // Lua modes own their own background/fade behavior.
            //
            // Convention:
            //   persist == false: Lua draws an opaque full-frame background.
            //   persist == true:  Lua draws a translucent full-frame fade/background, or intentionally leaves trails.
            //
            // This matches the OSv3 idea more closely: PERSIST changes auto-clear behavior,
            // but the mode owns the color/aesthetic.

            lua.setBool("persist", persistEnabled);
            lua.setBool("persistClear", persistClearRequested);
            lua.setNumber("renderW", persistFboW);
            lua.setNumber("renderH", persistFboH);
            lua.setNumber("screenW", ofGetWidth());
            lua.setNumber("screenH", ofGetHeight());
            lua.setNumber("persistBufferW", persistFboW);
            lua.setNumber("persistBufferH", persistFboH);
            lua.setNumber("persistFade", persistFadeAlpha);

            // Lua draws natively into the internal 1280x720 canvas.
            // The same canvas path is used whether persist is ON or OFF,
            // so toggling persist cannot change scale.
            lua.scriptDraw();

            persistFbo.end();

            ofPushStyle();
            ofDisableAlphaBlending();
            ofSetColor(255, 255, 255, 255);
            persistFbo.draw(0, 0, ofGetWidth(), ofGetHeight());
            ofPopStyle();

            persistClearRequested = false;
        }
    } else {
        // Safe fallback: if the internal canvas cannot be created,
        // draw directly exactly like the older baseline.
        lua.setBool("persist", false);
        lua.setBool("persistClear", false);
        lua.setNumber("renderW", ofGetWidth());
        lua.setNumber("renderH", ofGetHeight());
        lua.setNumber("screenW", ofGetWidth());
        lua.setNumber("screenH", ofGetHeight());
        lua.setNumber("persistBufferW", persistFboW);
        lua.setNumber("persistBufferH", persistFboH);
        lua.setNumber("persistFade", persistFadeAlpha);
        lua.scriptDraw();
    }
    // clear flags    
    lua.setBool("trig", false);

    /*ofClear(255);
    ofFill();
    ofSetColor(0);
    ofDrawRectangle(500,500,100,100);
    ofSetColor(255,0,0);
    ofDrawRectangle(600,600,100,100);
    ofNoFill();
    */
    
    if (osdEnabled) {
        haCpuStatus.draw(scripts[currentScript], audioPeak, ofGetFrameRate(),
                         ofGetElapsedTimeMillis(), haGetLanIpForOsd);
    } else {
        haCpuStatus.hide();
    }

}
//--------------------------------------------------------------
void ofApp::audioIn(ofSoundBuffer & input){
   
    float rawPeak = 0.0f;
    if (!dummyAudio){	
        for (size_t i = 0; i < input.getNumFrames(); i++){
            float rawL = input[i*2];
            float rawR = input[i*2+1];
            if (fabs(rawL) > rawPeak) rawPeak = fabs(rawL);
            if (fabs(rawR) > rawPeak) rawPeak = fabs(rawR);
            left[i]  = rawL * 0.5f;
            right[i] = rawR * 0.5f;
        }
    } else {
        for (size_t i = 0; i < input.getNumFrames(); i++){
            left[i]  = sin((i*TWO_PI)/input.getNumFrames());
            right[i] = cos((i*TWO_PI)/input.getNumFrames());
        }
    }
    
    audioPeak = rawPeak;
    bufferCounter++;
    
}

//--------------------------------------------------------------
void ofApp::exit() {
    haCpuStatus.clear(); // GL context is still alive; no per-mode texture ownership.
    // call the script's exit() function
    lua.scriptExit();
    
    // clear the lua state
    lua.clear();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    /*
    switch(key) {
    
        case 'r':
            reloadScript();
            break;
    
        case OF_KEY_LEFT:
            prevScript();
            break;
            
        case OF_KEY_RIGHT:
            nextScript();
            break;
            
        case ' ':
            lua.doString("print(\"this is a lua string saying you hit the space bar!\")");
            cout << "fps: " << ofGetFrameRate() << "\n";    
            break;
    }
    
    lua.scriptKeyPressed(key);
    */
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) {
    //lua.scriptMouseMoved(x, y);
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {
    //lua.scriptMouseDragged(x, y, button);
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
   // lua.scriptMousePressed(x, y, button);
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {
//    lua.scriptMouseReleased(x, y, button);
}

//--------------------------------------------------------------
void ofApp::errorReceived(std::string& msg) {
    ofLogNotice() << "got a script error: " << msg;
}

//--------------------------------------------------------------
void ofApp::reloadScript() {
    // Release native resources even if outgoing Lua exit fails.
    try {
        lua.scriptExit();
    } catch (const std::exception& error) {
        ofLogError("MODE_RECLAIM") << "Lua exit exception: " << error.what();
    } catch (...) {
        ofLogError("MODE_RECLAIM") << "Unknown Lua exit exception";
    }
    shaderHostGenerator.releaseGpuResources();

    // Reclaim retained native resources before the incoming mode allocates.
    meltLockGenerator.releaseGpuResources();

    // MODE GPU RECLAIM 02
    vectorImpactGenerator.releaseGpuResources();
    markovMoshGenerator.releaseGpuResources();
    bauhausFormGenerator.releaseGpuResources();
    architectureGenerator.releaseGpuResources();

    
    // Destroy all outgoing Lua userdata before retiring legacy VideoCore work.
    // This covers Lua FBO modes as well as native generators, once per switch.
    lua.clear();
    glFinish();

    // init OF
    ofSetupScreen();
    ofSetupGraphicDefaults();
    ofSetBackgroundColor(0,0,0);

    // load new
    lua.init();
    // --- NATIVE BRIDGE: re-register after mode-switch reinit ---
    registerAllGenerators(lua, nativeGenerators);
    // --- end NATIVE BRIDGE ---

    lua.doScript(scripts[currentScript], true);
    lua.scriptSetup();
}


static string haModeNameFromPath(string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();

    size_t slash = s.find_last_of("/\\");
    string last = (slash == string::npos) ? s : s.substr(slash + 1);

    if (last == "main.lua") {
        string parent = (slash == string::npos) ? "" : s.substr(0, slash);
        while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\')) parent.pop_back();

        size_t pslash = parent.find_last_of("/\\");
        last = (pslash == string::npos) ? parent : parent.substr(pslash + 1);
    }

    return last;
}

bool ofApp::setScriptByName(string requested) {
    cout << "set mode request: " << requested << "\n";

    string want = haModeNameFromPath(requested);

    for (size_t i = 0; i < scripts.size(); i++) {
        string modeName = haModeNameFromPath(scripts[i]);

        if (requested == scripts[i] || want == modeName) {
            currentScript = i;
            reloadScript();
            cout << "set mode OK: " << modeName << "\n";
            return true;
        }
    }

    cout << "set mode FAILED: " << requested << "\n";
    return false;
}


void ofApp::nextScript() {
    currentScript++;
    if(currentScript > scripts.size()-1) {
        currentScript = 0;
    }
    reloadScript();
}

void ofApp::prevScript() {
    if(currentScript == 0) {
        currentScript = scripts.size()-1;
    }
    else {
        currentScript--;
    }
    reloadScript();
}

