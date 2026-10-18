#pragma once
#include "ofMain.h"
#include "CpuStatusGlyphs.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

// One app-owned status texture. No OSD FBO, font texture, glyph draw or shader.
// setup/clear are called by ofApp while its GL context is alive.
class CpuStatusPanel {
public:
    void setup() {
        clear();
        pixels.allocate(500, 100, OF_PIXELS_RGB);
        pixels.set(0);
        texture.allocate(pixels, false); // GLES2 GL_TEXTURE_2D; initial black upload.
        if(!texture.isAllocated()) {
            ofLogError("cpuStatus") << "texture allocation failed";
            return;
        }
        texture.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        texture.setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    }
    void clear() {
        texture.clear();pixels.clear();mode.clear();
        shown={};visible=false;haveContent=false;
        lastTick=lastFps=0;heldPeak=displayFps=0;
    }
    void hide() { visible=false;heldPeak=0; }
    void draw(const std::string& currentMode, float peak, float fps, uint64_t now,
              std::string (*address)()) {
        if(!texture.isAllocated()) return; // setup logs failure; validation must reject it.
        if(!std::isfinite(peak) || peak<0) peak=0;
        if(peak>1) peak=1;
        if(peak>heldPeak) heldPeak=peak;
        const bool immediate=!visible || currentMode!=mode || !haveContent;
        visible=true;
        if(immediate || now<lastTick || now-lastTick>=50) {
            if(immediate || now<lastFps || now-lastFps>=500) {
                displayFps=(std::isfinite(fps) && fps>=0)?fps:0;
                lastFps=now;
            }
            Lines next={};
            std::snprintf(next[0].data(),64,"%s",currentMode.c_str());
            std::snprintf(next[1].data(),64,"FPS: %.1f",displayFps);
            const std::string ip=address(); // existing cached helper, unchanged 2.5s policy.
            std::snprintf(next[2].data(),64,"IP:  %s",ip.c_str());
            char bar[25]={};
            const int filled=static_cast<int>(heldPeak*24+0.5f);
            for(int i=0;i<24;++i) bar[i]=(i==19)?'|':((i<filled)?'#':'-');
            std::snprintf(next[3].data(),64,"IN:  [%s] %d%%",bar,
                          static_cast<int>(heldPeak*100+0.5f));
            mode=currentMode;lastTick=now;heldPeak=0;
            if(!haveContent || next!=shown) {
                pixels.set(0);
                for(int i=0;i<4;++i) raster(next[i].data(),10,15+17*i);
                texture.loadData(pixels); // same allocation, only on displayed-content change.
                shown=next;haveContent=true;
            }
        }
        // Match tested host window coordinates without inheriting a mode transform.
        // Canonical host depth test must be off, as in prior accepted state captures.
        ofPushView();ofPushStyle();
        ofViewport(0,0,ofGetWidth(),ofGetHeight(),true);
        ofSetupScreenOrtho(ofGetWidth(),ofGetHeight(),-1,1);
        ofDisableAlphaBlending();ofSetRectMode(OF_RECTMODE_CORNER);
        ofSetColor(255,255,255,255);
        texture.draw(0,0,500,100);
        ofPopStyle();ofPopView();
    }
private:
    typedef std::array<std::array<char,64>,4> Lines;
    ofPixels pixels;
    ofTexture texture;
    Lines shown={};
    std::string mode;
    bool visible=false,haveContent=false;
    uint64_t lastTick=0,lastFps=0;
    float heldPeak=0,displayFps=0;
    void raster(const char* text,int x,int baseline) {
        // OF SIMPLE/v-flipped path: initial y=baseline-8, then glyph y-=3.
        const int top=baseline-11;
        for(size_t n=0;text[n] && x<500;++n,x+=8) {
            unsigned c=static_cast<unsigned char>(text[n]);
            if(c<32 || c>126) c='?'; // existing font is ASCII; one status line only.
            for(int row=0;row<14;++row) {
                const int y=top+row;
                if(y<0 || y>=100) continue;
                const unsigned bits=cpuStatusGlyphs[c-32][row];
                for(int col=0;col<8 && x+col<500;++col) {
                    if(x+col<0 || !(bits & (128u>>col))) continue;
                    const size_t index=(static_cast<size_t>(y)*500+x+col)*3;
                    pixels[index]=pixels[index+1]=pixels[index+2]=255;
                }
            }
        }
    }
};
