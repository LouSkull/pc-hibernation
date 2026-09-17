#include "../src/performance.h"
#include <stdexcept>
#include <iostream>
void check(bool v,const char* text) { if(!v) throw std::runtime_error(text); }
int main() {
    try {
        Settings s;s.fps=60;s.quality=100;s.minFps=10;s.minQuality=100;s.deepRest=false;
        PerformancePolicy p;p.reset(s);
        p.sample(s,0,20,1,false);p.sample(s,0,20,1,false);
        check(p.quality==100,"hysteresis prevents transient changes");
        p.sample(s,0,20,1,false);check(p.quality==100 && p.fps==55,"lower FPS while preserving native resolution");
        for(int i=0;i<120;i++)p.sample(s,0,100,100,false);
        check(p.quality==100 && p.fps==10,"never below FPS floor or native resolution");
        for(int i=0;i<10;i++)p.sample(s,0,0.1,0.1,false);
        check(p.fps==15 && p.quality==100,"recover slowly with spare capacity");
        s.deepRest=true;p.sample(s,60000,0,0,false);
        check(p.fps==10 && p.quality==100,"deep rest clamps FPS without blur");
        s.deepRest=false;s.adaptive=false;p.sample(s,0,999,999,false);
        check(p.fps==60 && p.quality==100,"manual settings honored");
        p.reset(s,true);p.sample(s,0,0,0,true);
        check(p.fps<=10 && p.quality==100,"software fallback caps FPS and stays native");
        s.fps=0;s.quality=0;s.minFps=999;s.minQuality=999;s.previewFps=999;s.previewWidth=0;s.backgroundPoll=0;s.sanitize();
        check(s.fps==5 && s.minFps==5 && s.quality==100 && s.minQuality==100 && s.previewFps==30 && s.previewWidth==800 && s.backgroundPoll==500,"clamp dependent bounds");
        std::cout<<"Performance policy checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what();return 1; }
}
