#include "diagnostics.h"
#include "bridge.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <set>
#include <sstream>
#include <iomanip>
#include <algorithm>
namespace {
ULONGLONG ticks(FILETIME t) { return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
ULONGLONG cpu(HANDLE process) { FILETIME c{},e{},k{},u{}; return GetProcessTimes(process,&c,&e,&k,&u)?ticks(k)+ticks(u):0; }
double memory(HANDLE process) { PROCESS_MEMORY_COUNTERS pm{}; return GetProcessMemoryInfo(process,&pm,sizeof(pm))?pm.WorkingSetSize/1048576.0:0; }
}
double preciseSeconds() { LARGE_INTEGER q{},f{}; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return static_cast<double>(q.QuadPart)/f.QuadPart; }
void Diagnostics::reset() { *this=Diagnostics{}; }
void Diagnostics::frame(double cost,double gpu,int cap) {
    double now=preciseSeconds();
    if(!epoch) epoch=now;
    if(lastFrame && now-lastFrame>1.5/std::max(1,cap)) ++lateFrames;
    lastFrame=now; ++frames; ++totalFrames; totalCost+=cost; peakCost=std::max(peakCost,cost); gpuMs=gpu;
}
bool Diagnostics::sample(bool detailed) {
    double now=preciseSeconds(); if(sampled && now-sampled<1) return false;
    const double seconds=sampled?now-sampled:0;
    if(epoch && now>epoch) fps=frames/(now-epoch);
    else fps=0;
    frameMs=frames?totalCost/frames:0; peakMs=peakCost;
    frames=0; totalCost=peakCost=0; epoch=now;
    if(detailed) {
        SYSTEM_INFO si{}; GetSystemInfo(&si);
        const double scale=seconds>0?100.0/(seconds*1e7*si.dwNumberOfProcessors):0;
        auto own=cpu(GetCurrentProcess()); processCpu=prevCpu?static_cast<double>(own-prevCpu)*scale:0; prevCpu=own;
        memoryMb=memory(GetCurrentProcess());
        FILETIME idle{},kernel{},user{};
        if(GetSystemTimes(&idle,&kernel,&user)) {
            auto k=ticks(kernel),u=ticks(user),i=ticks(idle),delta=k-prevKernel+u-prevUser;
            systemCpu=prevKernel && delta?100.0*(delta-(i-prevIdle))/delta:0;
            prevIdle=i;prevKernel=k;prevUser=u;
        }
        // Include this process and descendants, including its WebView2 processes.
        std::map<DWORD,DWORD> parents; HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        PROCESSENTRY32W entry{sizeof(entry)};
        if(snap!=INVALID_HANDLE_VALUE) {
            if(Process32FirstW(snap,&entry)) do { parents[entry.th32ProcessID]=entry.th32ParentProcessID; } while(Process32NextW(snap,&entry));
            CloseHandle(snap);
        }
        std::set<DWORD> ids{GetCurrentProcessId()};
        bool changed=true; while(changed) { changed=false;for(auto [id,parent]:parents) if(ids.count(parent)&&ids.insert(id).second) changed=true; }
        std::map<DWORD,ULONGLONG> next; treeCpu=treeMb=0;
        for(auto id:ids) {
            HANDLE p=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,id); if(!p) continue;
            auto time=cpu(p); next[id]=time; auto it=processTimes.find(id);
            if(it!=processTimes.end() && time>=it->second) treeCpu+=(time-it->second)*scale;
            treeMb+=memory(p); CloseHandle(p);
        }
        processTimes=std::move(next);
    } else { prevCpu=prevKernel=prevUser=prevIdle=0;processTimes.clear(); }
    sampled=now; return true;
}
std::string Diagnostics::json(int cap,int quality,const std::string& renderer,int width,int height) const {
    std::ostringstream out; out<<std::fixed<<std::setprecision(2);
    out<<"{\"fps\":"<<fps<<",\"frameMs\":"<<frameMs<<",\"peakMs\":"<<peakMs<<",\"gpuMs\":"<<gpuMs
       <<",\"nativeCpu\":"<<processCpu<<",\"systemCpu\":"<<systemCpu<<",\"nativeMb\":"<<memoryMb
       <<",\"appCpu\":"<<treeCpu<<",\"appMb\":"<<treeMb<<",\"totalFrames\":"<<totalFrames<<",\"lateFrames\":"<<lateFrames
       <<",\"baselineCpu\":"<<baselineCpu<<",\"cpuDelta\":"<<(baselineCpu<0?0:treeCpu-baselineCpu)
       <<",\"memoryDelta\":"<<(baselineCpu<0?0:treeMb-baselineMemory)<<",\"systemDelta\":"<<(baselineCpu<0?0:systemCpu-baselineSystem)
       <<",\"cap\":"<<cap<<",\"quality\":"<<quality<<",\"width\":"<<width<<",\"height\":"<<height
       <<",\"renderer\":\""<<Bridge::escape(renderer)<<"\"}";
    return out.str();
}
