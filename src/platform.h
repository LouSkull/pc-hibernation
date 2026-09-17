#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "activity.h"
class MediaMonitor {
    std::atomic<bool> monitoring{true};
    std::atomic<unsigned> playback{NoMedia};
    std::atomic<bool> ready{false};
    std::mutex mutex; std::condition_variable wake; bool stopping=false;
    std::thread worker;
public:
    void start();
    void setMonitoring(bool value) { monitoring=value; wake.notify_all(); }
    void stop();
    ~MediaMonitor() { stop(); }
    unsigned playing() const { return playback.load(); }
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
