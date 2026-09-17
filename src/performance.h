#pragma once
#include "core.h"

// One sample per second. Fullscreen stays at native resolution; hysteresis only
// adjusts cadence so economy mode cannot make the image blurry.
struct PerformancePolicy {
    int fps=30, quality=100, overloaded=0, spare=0;
    void reset(const Settings& s, bool software=false) {
        fps=software?std::min(s.fps,10):s.fps;
        quality=100;
        overloaded=spare=0;
    }
    bool sample(const Settings& s, uint64_t activeMs, double gpuMs, double cpuFrameMs, bool software) {
        const int capFps=software?std::min(s.fps,10):s.fps;
        const int floorFps=std::min(s.minFps,capFps);
        const int oldFps=fps;
        fps=std::min(fps,capFps); quality=100;
        if(s.deepRest && activeMs>=static_cast<uint64_t>(s.restAfter)*1000) {
            fps=floorFps;
        } else if(!s.adaptive) { fps=capFps; }
        else {
            const double cost=std::max(gpuMs,cpuFrameMs);
            const double budget=1000.0/std::max(fps,1)*s.gpuBudget/100.0;
            if(cost>budget) { ++overloaded; spare=0; }
            else if(cost<budget*.55) { ++spare; overloaded=0; }
            else { overloaded=spare=0; }
            if(overloaded>=3) {
                fps=std::max(floorFps,fps-5);
                overloaded=0;
            }
            if(spare>=10) {
                fps=std::min(capFps,fps+5);
                spare=0;
            }
        }
        return fps!=oldFps;
    }
};
