#pragma once
#include <windows.h>
#include <string>
#include <map>
double preciseSeconds();
struct Diagnostics {
    double fps=0,frameMs=0,peakMs=0,gpuMs=-1,processCpu=0,systemCpu=0,memoryMb=0,treeCpu=0,treeMb=0;
    double baselineCpu=-1,baselineMemory=0,baselineSystem=0;
    unsigned frames=0,totalFrames=0,lateFrames=0;
    double epoch=0,lastFrame=0,totalCost=0,peakCost=0,sampled=0;
    ULONGLONG prevCpu=0,prevIdle=0,prevKernel=0,prevUser=0;
    std::map<DWORD,ULONGLONG> processTimes;
    void frame(double cost,double gpu,int cap);
    bool sample(bool detailed);
    void baseline() { baselineCpu=treeCpu; baselineMemory=treeMb; baselineSystem=systemCpu; }
    void reset();
    std::string json(int cap,int quality,const std::string& renderer,int width,int height) const;
};
