#include "platform.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <cwctype>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Media::Control;
namespace {
bool audioMediaFallback(unsigned& playing) {
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> devices;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator))) || FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))) return false;
    UINT count=0; devices->GetCount(&count);
    for(UINT i=0;i<count;i++) {
        ComPtr<IMMDevice> device; ComPtr<IAudioSessionManager2> manager; ComPtr<IAudioSessionEnumerator> sessions;
        if(FAILED(devices->Item(i,&device)) || FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf()))) || FAILED(manager->GetSessionEnumerator(&sessions))) continue;
        int sessionCount=0; sessions->GetCount(&sessionCount);
        for(int j=0;j<sessionCount;j++) {
            ComPtr<IAudioSessionControl> control; ComPtr<IAudioSessionControl2> extended; AudioSessionState state{};
            if(FAILED(sessions->GetSession(j,&control)) || FAILED(control->GetState(&state)) || state!=AudioSessionStateActive || FAILED(control.As(&extended))) continue;
            DWORD pid=0; if(FAILED(extended->GetProcessId(&pid)) || !pid) continue;
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); if(!process) continue;
            wchar_t path[32768]{}; DWORD length=32768;
            if(QueryFullProcessImageNameW(process,0,path,&length)) {
                const auto source=mediaSource(path);
                // Unidentified audio can be ordinary game/system sound, not media.
                if(source!=MediaSource::Other) playing|=mediaSignal(source);
            }
            CloseHandle(process);
        }
    }
    return true;
}
}
void MediaMonitor::start() {
    if(worker.joinable()) return;
    worker=std::thread([this] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
            unsigned initial=NoMedia;
            ready=audioMediaFallback(initial); playback=initial;
            GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
            try {
                auto request=GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
                // Media-broker failure must not disable the independent Core Audio fallback.
                for(int i=0;i<20 && request.Status()==winrt::Windows::Foundation::AsyncStatus::Started;i++) {
                    std::unique_lock lock(mutex); if(wake.wait_for(lock,std::chrono::milliseconds(100),[this]{return stopping;})) { request.Cancel(); return; }
                }
                if(request.Status()==winrt::Windows::Foundation::AsyncStatus::Completed) manager=request.GetResults(); else request.Cancel();
            } catch(...) { /* Continue with active browser/player audio sessions. */ }
            while(true) {
                { std::unique_lock lock(mutex); wake.wait(lock,[this]{return stopping || monitoring.load();}); if(stopping) break; }
                unsigned playing=NoMedia; bool supported=audioMediaFallback(playing);
                try {
                    if(manager) for(const auto& session:manager.GetSessions()) {
                        try {
                            auto info=session.GetPlaybackInfo();
                            if(info.PlaybackStatus()!=GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) continue;
                            auto type=info.PlaybackType();
                            MediaSource source=MediaSource::Other;
                            try { auto id=session.SourceAppUserModelId(); source=mediaSource(std::wstring_view(id.c_str(),id.size())); } catch(...) {}
                            playing|=mediaSignal(source,type && type.Value()==MediaPlaybackType::Video);
                        } catch(...) { /* A closing session must not hide the remaining sessions. */ }
                    }
                    supported=supported || static_cast<bool>(manager);
                } catch(...) { manager=nullptr; }
                playback=playing; ready=supported;
                std::unique_lock lock(mutex); if(wake.wait_for(lock,std::chrono::seconds(2),[this]{return stopping;})) break;
            }
        } catch(...) { ready=false; }
    });
}
void MediaMonitor::stop() {
    { std::lock_guard lock(mutex); stopping=true; } wake.notify_all(); if(worker.joinable()) worker.join();
}
Activity currentActivity(const MediaMonitor& media,const Settings& s,HWND own) {
    const unsigned playing=media.playing();
    const auto desktop=[&] { return classifyActivity(playing,MediaSource::Other,false,s.mediaConservative); };
    if(playing&(VideoMedia|BrowserMedia)) return Activity::Video;
    HWND fg=GetForegroundWindow();
    if(!fg || fg==own || fg==GetDesktopWindow() || fg==GetShellWindow() || IsIconic(fg)) return desktop();
    wchar_t cls[64]{}; GetClassNameW(fg,cls,64);
    if(!wcscmp(cls,L"WorkerW") || !wcscmp(cls,L"Progman") || !wcscmp(cls,L"Shell_TrayWnd")) return desktop();
    MediaSource foreground=MediaSource::Other;
    DWORD id=0; GetWindowThreadProcessId(fg,&id);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);
    if(process) {
        wchar_t path[32768]{}; DWORD length=32768;
        if(QueryFullProcessImageNameW(process,0,path,&length)) {
            foreground=mediaSource(path);
        }
        CloseHandle(process);
    }
    bool fullscreen=false;
    RECT r{}; MONITORINFO info{sizeof(info)};
    if(GetWindowRect(fg,&r)&&GetMonitorInfoW(MonitorFromWindow(fg,MONITOR_DEFAULTTONEAREST),&info)) {
        auto m=info.rcMonitor;
        fullscreen=r.left<=m.left+2&&r.top<=m.top+2&&r.right>=m.right-2&&r.bottom>=m.bottom-2;
    }
    return classifyActivity(playing,foreground,fullscreen,s.mediaConservative);
}
namespace {
std::wstring eventName(DWORD id,const wchar_t* kind) { return L"Local\\Hibernation.Audio."+std::to_wstring(id)+L"."+kind; }
struct Endpoint { ComPtr<IAudioEndpointVolume> volume; BOOL wasMuted=FALSE; };
using Endpoints=std::map<std::wstring,Endpoint>;
bool muteEndpoints(IMMDeviceEnumerator* enumerator,Endpoints& saved,bool probe=false) {
    ComPtr<IMMDeviceCollection> devices;
    if(FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))) return false;
    UINT count=0; devices->GetCount(&count); bool ok=true;
    for(UINT i=0;i<count;i++) {
        ComPtr<IMMDevice> device; if(FAILED(devices->Item(i,&device))) { ok=false; continue; }
        LPWSTR raw=nullptr; if(FAILED(device->GetId(&raw))) { ok=false; continue; }
        std::wstring id=raw; CoTaskMemFree(raw); if(saved.contains(id)) continue;
        Endpoint endpoint;
        if(FAILED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(endpoint.volume.GetAddressOf()))) || FAILED(endpoint.volume->GetMute(&endpoint.wasMuted))) { ok=false; continue; }
        if(probe) continue;
        if(SUCCEEDED(endpoint.volume->SetMute(TRUE,nullptr))) saved.emplace(id,std::move(endpoint)); else ok=false;
    }
    return ok;
}
void restoreEndpoints(Endpoints& saved) {
    for(auto it=saved.begin();it!=saved.end();) {
        // Preserve the pre-existing mute state of each output; never change volume levels.
        if(SUCCEEDED(it->second.volume->SetMute(it->second.wasMuted,nullptr))) it=saved.erase(it); else ++it;
    }
}
}
bool AudioGuard::initialize() {
    if(process && WaitForSingleObject(process,0)==WAIT_TIMEOUT) return true;
    shutdown(); DWORD id=GetCurrentProcessId();
    muteEvent=CreateEventW(nullptr,FALSE,FALSE,eventName(id,L"mute").c_str());
    restoreEvent=CreateEventW(nullptr,FALSE,FALSE,eventName(id,L"restore").c_str());
    quitEvent=CreateEventW(nullptr,FALSE,FALSE,eventName(id,L"quit").c_str());
    readyEvent=CreateEventW(nullptr,FALSE,FALSE,eventName(id,L"ready").c_str());
    if(!muteEvent||!restoreEvent||!quitEvent||!readyEvent) { shutdown(); return false; }
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr,exe,32768);
    std::wstring command=L"\""+std::wstring(exe)+L"\" --audio-guard "+std::to_wstring(id);
    STARTUPINFOW start{sizeof(start)}; start.dwFlags=STARTF_USESHOWWINDOW; start.wShowWindow=SW_HIDE; PROCESS_INFORMATION child{};
    if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)) { shutdown(); return false; }
    CloseHandle(child.hThread); process=child.hProcess;
    HANDLE waits[]={process,readyEvent};
    if(WaitForMultipleObjects(2,waits,FALSE,3000)!=WAIT_OBJECT_0+1) { shutdown(); return false; }
    return true;
}
bool AudioGuard::setMuted(bool mute) {
    if(mute && !initialize()) return false;
    if(!process) return true;
    // Cancel a not-yet-consumed opposite request, so a fast exit cannot leave a pending mute.
    ResetEvent(mute?restoreEvent:muteEvent);
    return SetEvent(mute?muteEvent:restoreEvent)!=FALSE;
}
void AudioGuard::shutdown() {
    if(process) { if(quitEvent) SetEvent(quitEvent); WaitForSingleObject(process,3000); CloseHandle(process); process=nullptr; }
    for(auto handle:{muteEvent,restoreEvent,quitEvent,readyEvent}) if(handle) CloseHandle(handle);
    muteEvent=restoreEvent=quitEvent=readyEvent=nullptr;
}
int audioGuardianMain(DWORD parentId) {
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parentId);
    HANDLE mute=OpenEventW(SYNCHRONIZE,FALSE,eventName(parentId,L"mute").c_str());
    HANDLE restore=OpenEventW(SYNCHRONIZE,FALSE,eventName(parentId,L"restore").c_str());
    HANDLE quit=OpenEventW(SYNCHRONIZE,FALSE,eventName(parentId,L"quit").c_str());
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName(parentId,L"ready").c_str());
    if(!parent||!mute||!restore||!quit||!ready) { for(auto h:{parent,mute,restore,quit,ready}) if(h) CloseHandle(h); return 1; }
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); int result=0;
    if(SUCCEEDED(hr)) {
        {
            ComPtr<IMMDeviceEnumerator> enumerator;
            if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))) {
                SetEvent(ready);
                Endpoints saved; bool active=false; HANDLE handles[]={parent,quit,restore,mute};
                for(;;) {
                    DWORD event=WaitForMultipleObjects(4,handles,FALSE,1000);
                    if(event==WAIT_OBJECT_0 || event==WAIT_OBJECT_0+1 || event==WAIT_FAILED) { restoreEndpoints(saved); break; }
                    if(event==WAIT_OBJECT_0+2) active=false;
                    if(event==WAIT_OBJECT_0+3) active=true;
                    if(active) muteEndpoints(enumerator.Get(),saved); else restoreEndpoints(saved);
                }
            } else result=1;
        }
        CoUninitialize();
    } else result=1;
    for(auto h:{parent,mute,restore,quit,ready}) CloseHandle(h); return result;
}
int audioProbe() {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); if(FAILED(hr)) return 1;
    int result=1;
    { ComPtr<IMMDeviceEnumerator> enumerator; Endpoints none;
      if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))) result=muteEndpoints(enumerator.Get(),none,true)?0:1; }
    CoUninitialize(); return result;
}
int audioSelfTest() {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); if(FAILED(hr)) return 1;
    int result=1;
    {
        ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> devices;
        std::vector<Endpoint> snapshot;
        if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator))) &&
            SUCCEEDED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))) {
            UINT count=0; devices->GetCount(&count);
            for(UINT i=0;i<count;i++) {
                ComPtr<IMMDevice> device; Endpoint endpoint;
                if(SUCCEEDED(devices->Item(i,&device)) && SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(endpoint.volume.GetAddressOf()))) && SUCCEEDED(endpoint.volume->GetMute(&endpoint.wasMuted))) snapshot.push_back(std::move(endpoint));
            }
            AudioGuard guard;
            auto matches=[&](bool muted) {
                for(auto& endpoint:snapshot) { BOOL value=FALSE; if(FAILED(endpoint.volume->GetMute(&value)) || value!=(muted?TRUE:endpoint.wasMuted)) return false; }
                return true;
            };
            auto wait=[&](bool muted) { for(int i=0;i<100;i++) { if(matches(muted)) return true; Sleep(20); } return false; };
            if(!snapshot.empty() && guard.setMuted(true)) {
                bool muted=wait(true); guard.setMuted(false); bool restored=wait(false);
                guard.shutdown(); result=muted&&restored?0:1;
                if(result==0) {
                    // Crash a disposable owner, never the user's app, and verify its watchdog restores audio.
                    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
                    std::wstring command=L"\""+std::wstring(executable)+L"\" --audio-owner-test";
                    STARTUPINFOW start{sizeof(start)}; start.dwFlags=STARTF_USESHOWWINDOW; start.wShowWindow=SW_HIDE; PROCESS_INFORMATION child{};
                    if(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)) {
                        bool childMuted=wait(true);
                        TerminateProcess(child.hProcess,99); WaitForSingleObject(child.hProcess,1000);
                        bool recovered=wait(false); CloseHandle(child.hThread); CloseHandle(child.hProcess);
                        result=childMuted&&recovered?0:1;
                    } else result=1;
                }
            }
            // Test-level safety net restores captured states even if a guard assertion fails.
            for(auto& endpoint:snapshot) endpoint.volume->SetMute(endpoint.wasMuted,nullptr);
        }
    }
    CoUninitialize(); return result;
}
int audioOwnerTest() {
    AudioGuard guard; if(!guard.setMuted(true)) return 1;
    Sleep(5000); return 0; // Also restores automatically if the test runner disappears.
}
int mediaProbe() {
    MediaMonitor monitor; monitor.start();
    for(int i=0;i<60 && !monitor.available();i++) Sleep(100);
    bool available=monitor.available(); monitor.stop(); return available?0:1;
}
