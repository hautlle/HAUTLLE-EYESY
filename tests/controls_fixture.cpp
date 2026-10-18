// CPU-only harness. The runner inserts the actual packaged ofApp::update()
// at the marker and links the unchanged real KnobRecorder.cpp. No OF/GL/audio.
#include <cassert>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "KnobRecorder.h"
using namespace std;
static uint64_t clockMs = 1000;
uint64_t ofGetElapsedTimeMillis() { return clockMs; }
int ofGetWidth() { return 1280; }
int ofGetHeight() { return 720; }
string ofToString(int v) { return to_string(v); }
enum { OFXOSC_TYPE_INT32 = 1, BAD_TYPE = 2 };
struct ofxOscMessage {
    string address; vector<int> args; int type = OFXOSC_TYPE_INT32;
    string getAddress() const { return address; }
    int getNumArgs() const { return int(args.size()); }
    int getArgType(int n) const { args.at(n); return type; }
    int getArgAsInt32(int n) const { return args.at(n); }
    string getArgAsString(int) const { return "mode"; }
};
struct Receiver {
    deque<ofxOscMessage> queue;
    bool hasWaitingMessages() { return !queue.empty(); }
    void getNextMessage(ofxOscMessage& m) { m=queue.front(); queue.pop_front(); }
};
struct Lua {
    map<string,float> numbers; map<string,bool> flags;
    void setNumber(const char* n, float v) { numbers[n]=v; }
    void setBool(const char* n, bool v) { flags[n]=v; }
    void scriptUpdate() {}
};
struct Image { void grabScreen(int,int,int,int) {} void save(const string&) {} };
class ofApp {
public:
    Receiver receiver; Lua lua; Image img; KnobRecorderManager knobRecorder;
    bool shiftHeld=false, persistPressSuppressed=false, sceneAHeld=false, sceneBPhysicalHeld=false;
    bool persistEnabled=false, persistClearRequested=false, persistFboReady=false, persistFboAllocAttempted=true;
    uint64_t button3LastPressMs=0, sceneBLastTapMs=0;
    int sceneBTapCount=0, snapCounter=0, dummyAudio=0, osdEnabled=0, next=0, prev=0;
    float rawKnob01[5]={}, effectiveKnob01[5]={}, clearBaselineRaw01[5]={};
    bool clearGestureMoved[5]={};
    static constexpr uint64_t kSceneBDoubleTapWindowMs=400;
    vector<string> events;
    void sendKnobRecEvent(const string& n, int) { events.push_back(n); }
    void sendKnobRecStateIfDue(uint64_t) {}
    void prevScript() { ++prev; } void nextScript() { ++next; }
    void setScriptByName(string) {} void reloadScript() {}
    void update();
    void send(string address, vector<int> args, uint64_t at, int type=OFXOSC_TYPE_INT32) {
        clockMs=at; ofxOscMessage m; m.address=address; m.args=args; m.type=type;
        receiver.queue.push_back(m); update();
    }
    void key(int id,int value,uint64_t at) { send("/key",{id,value},at); }
    void shift(bool beta,int value,uint64_t at) {
        if(beta) send("/shift",{value},at); else key(2,value,at);
    }
};
// INSERT_ACTUAL_UPDATE_HERE

static void record(ofApp& a, bool beta, uint64_t at) {
    a.shift(beta,1,at);
    a.send("/knobs",{0,0,0,0,0,0},at+1);
    a.send("/knobs",{400,0,0,0,0,0},at+30);
    a.send("/knobs",{800,0,0,0,0,0},at+80);
    a.shift(beta,0,at+120);
    assert(a.knobRecorder.laneActive(0));
}
int main() {
    for(bool beta : {false,true}) {
        ofApp a;
        a.key(3,1,1000); assert(a.persistEnabled && a.persistClearRequested);
        assert(!a.persistFboAllocAttempted);
        a.key(3,0,1001); a.key(3,1,1100); assert(a.persistEnabled); // debounce
        a.key(3,0,1101); a.key(3,1,1400); assert(!a.persistEnabled && !a.persistClearRequested);
        a.key(3,0,1401);
        a.shift(beta,1,1500); a.key(3,1,1600); a.key(3,0,1650); // short shifted press
        assert(!a.persistEnabled);
        a.key(3,1,2000); a.key(3,0,3000); assert(!a.persistEnabled); // long shifted hold
        a.key(3,1,3100); a.shift(beta,0,3150); a.key(3,1,3500); // release Shift first + duplicate down
        assert(!a.persistEnabled); a.key(3,0,3510);
        a.key(3,1,3900); assert(a.persistEnabled); a.key(3,0,3910);
        record(a,beta,4000);
        float v=a.effectiveKnob01[0]; clockMs=4160; a.update();
        assert(a.knobRecorder.laneActive(0)); assert(a.effectiveKnob01[0]!=v); // playback advances without OSC
        a.shift(beta,1,4300); size_t eventCount=a.events.size();
        a.shift(beta,1,4301); assert(a.events.size()==eventCount); // repeated shift edge ignored
        a.key(6,1,4310); a.send("/knobs",{0,0,0,0,0,0},4340);
        assert(!a.knobRecorder.laneActive(0)); a.key(6,0,4350); a.shift(beta,0,4360);
        record(a,beta,4500);
        a.shift(beta,1,4700); a.key(7,1,4710); a.key(7,0,4720);
        a.key(7,1,4800); assert(!a.knobRecorder.laneActive(0));
        a.key(7,0,4810); a.shift(beta,0,4820);
        assert(a.next==0 && a.prev==0);
        a.key(4,1,5000); a.key(5,1,5010); assert(a.next==1 && a.prev==1);
        a.key(8,1,5020); a.key(99,1,5030); assert(a.next==1 && a.prev==1 && !a.shiftHeld);
        a.send("/shift",{},5040); a.send("/shift",{2},5050);
        a.send("/shift",{1,0},5060); a.send("/shift",{1},5070,BAD_TYPE);
        a.send("/key",{2},5080); a.send("/key",{2,1},5090,BAD_TYPE);
        assert(!a.shiftHeld);
        a.key(1,1,5100); assert(a.osdEnabled==1);
        a.key(10,1,5110); assert(a.dummyAudio==1 && a.lua.flags["trig"]);
        a.key(10,0,5120); assert(a.dummyAudio==0);
    }
    cout << "PASS controls for /key 2 and /shift; normal PERSIST, retired shortcut, recorder and navigation\n";
}
