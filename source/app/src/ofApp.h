/*
 * Copyright (c) 2020 Owen Osborn, Critter & Gutiari, Inc.
 *
 * BSD Simplified License.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 *
 */
#pragma once

#include "ofMain.h"
#include "ofxLua.h"
#include "bridge/NativeBridge.h"
#include "bridge/KnobRecorder.h"
#include "generators/ShaderHostGenerator.h"
#include "generators/LowResShaderHostGenerator.h"
#include "generators/MarkovMoshGenerator.h"
#include "generators/MeltLockGenerator.h"
#include "generators/BauhausFormGenerator.h"
#include "generators/VectorImpactGenerator.h"
#include "generators/MassStWalkGenerator.h"
#include "generators/MurmurationFieldGenerator.h"
#include "generators/ArchitectureGenerator.h"
#include "generators/RobotFightGenerator.h"
#include "ofxOsc.h"

#define PORT 4000

class ofApp : public ofBaseApp, ofxLuaListener {

    public:

        // main
        void setup();
        void update();
        void draw();
        void exit();
        
        // input
        void keyPressed(int key);
        void mouseMoved(int x, int y);
        void mouseDragged(int x, int y, int button);
        void mousePressed(int x, int y, int button);
        void mouseReleased(int x, int y, int button);
        
        // ofxLua error callback
        void errorReceived(std::string& msg);
        
        // script control
        void reloadScript();
	bool setScriptByName(string requested);
        void nextScript();
        void prevScript();
    
        ofxLua lua;

        // Native implementations required by the captured live mode roster.
        std::vector<NativeGenerator*> nativeGenerators;
        ShaderHostGenerator shaderHostGenerator;
        LowResShaderHostGenerator lowResShaderHostGenerator;
        MarkovMoshGenerator markovMoshGenerator;
        MeltLockGenerator meltLockGenerator;
        BauhausFormGenerator bauhausFormGenerator;
        VectorImpactGenerator vectorImpactGenerator;
        MassStWalkGenerator massStWalkGenerator;
        MurmurationFieldGenerator murmurationFieldGenerator;
        ArchitectureGenerator architectureGenerator;
        RobotFightGenerator robotFightGenerator;

        vector<string> scripts;
        size_t currentScript;

        // osc control
        ofxOscReceiver receiver;

        // audio stuff
        void audioIn(ofSoundBuffer & input);
    
        vector <lua_Number> left;
        vector <lua_Number> right;

        int     bufferCounter;
        int     drawCounter;
    
        float smoothedVol;
        float scaledVol;
        
        ofSoundStream soundStream;

        int                 snapCounter;
        string              snapString;
        ofImage             img;

	int osdEnabled;
	ofFbo osdFbo;
        bool               persistEnabled;
        bool               persistFboReady;
        bool               persistFboAllocAttempted;
        bool               persistClearRequested;
        ofFbo              persistFbo;
        int                persistFboW;
        int                persistFboH;
        int                persistFadeAlpha;
	int dummyAudio;
	float              audioPeak;
	float              audioTrigThreshold;
	float              audioTrigResetThreshold;
	bool               audioTrigArmed;
	uint64_t           lastAudioTrigMs;
	uint64_t           audioTrigCooldownMs;
	uint64_t           button3LastPressMs;

        bool               shiftHeld;
        bool               persistPressSuppressed;

        // --- KNOB RECORDER ---
        KnobRecorderManager knobRecorder;
        ofxOscSender        knobRecorderOscSender;

        bool     sceneAHeld;
        bool     sceneBPhysicalHeld;
        int      sceneBTapCount;
        uint64_t sceneBLastTapMs;
        uint64_t knobRecStateLastSentMs;
        float    rawKnob01[5];
        float    effectiveKnob01[5];
        float    clearBaselineRaw01[5];
        bool     clearGestureMoved[5];
        static constexpr uint64_t kKnobRecStateIntervalMs = 250;
        static constexpr uint64_t kSceneBDoubleTapWindowMs = 400;

        void sendKnobRecEvent(const std::string& eventName, int laneOneBased);
        void sendKnobRecStateIfDue(uint64_t nowMs);
        // --- end KNOB RECORDER ---

};
