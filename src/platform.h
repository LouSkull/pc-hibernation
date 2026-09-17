#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "core.h"
class MediaMonitor {
    std::atomic<bool> monitoring{true};
    std::atomic<bool> video{false},unknown{false},ready{false};
    std::mutex mutex; std::condition_variable wake; bool stopping=false;
    std::thread worker;
public:
    void start();
    void setMonitoring(bool value) { monitoring=value; wake.notify_all(); }
    void stop();
    ~MediaMonitor() { stop(); }
    bool playingVideo(bool conservative) const { return video.load() || (conservative && unknown.load()); }
    // A session Windows explicitly marks as video, as opposed to "something is playing".
    bool playingRealVideo() const { return video.load(); }
    bool available() const { return ready.load(); }
};
Activity currentActivity(const MediaMonitor& media,const Settings& settings,HWND ownWindow);
class AudioGuard {
    HANDLE muteEvent{},restoreEvent{},quitEvent{},readyEvent{},process{};
public:
    bool initialize();
    bool setMuted(bool mute);
    void shutdown();
    ~AudioGuard() { shutdown(); }
};
int audioGuardianMain(DWORD parentId);
int audioProbe();
int audioSelfTest();
int audioOwnerTest();
int mediaProbe();
