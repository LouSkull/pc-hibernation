#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
constexpr int EffectCount=12;
constexpr int PaletteCount=8;
enum class Activity { Desktop, Game, Video };
struct Settings {
    int previewFps = 20;
    int previewWidth = 960;
    int minFps = 10;
    // Legacy render-scale fields stay in the config for compatibility. Fullscreen
    // rendering is always native-resolution so Windows never stretches a soft frame.
    int minQuality = 100;
    int restAfter = 60;
    int gpuBudget = 20;
    int backgroundPoll = 1000;
    int maxRenderHeight = 2160;
    int mediaId = 0;
    int mediaFit = 0;
    bool mediaRest = true;
    bool adaptive = true;
    bool deepRest = true;
    bool ecoPriority = true;
    bool animateThumbs = true;
    bool debugEnabled = false;
    bool debugOverlay = false;
    bool debugLog = false;
    bool debugFreeze = false;
    bool softwareRenderer = false;
    int delay = 5;
    int wakeMouseThreshold = 24;
    int speed = 45;
    int brightness = 80;
    int palette = 0;
    int effect = 0;
    int scale = 100;
    int turbulence = 55;
    int glow = 65;
    int fps = 30;
    int quality = 100;
    int cycleSeconds = 60;
    int fadeSeconds = 2;
    int gameDelay = 60;
    int videoDelay = 120;
    int clockStyle = 0;
    int clockSize = 100;
    int clockOpacity = 80;
    int clockPosition = 0;
    int accent = 0;
    int tone = 0;
    int roundness = 12;
    int lockGraceMinutes = 0;
    int clockWeight = 1;                 // 0 thin, 1 regular, 2 bold
    int clockColor = 0;                  // tint index for the on-screen clock
    int hotkeyToggleKey = 'H';           // customisable global shortcuts
    int hotkeyToggleMods = 3;            // MOD_ALT(1) | MOD_CONTROL(2) | MOD_SHIFT(4) | MOD_WIN(8)
    int hotkeyPauseKey = 'P';
    int hotkeyPauseMods = 3;
    int cursorStyle = 0;                 // 0 Windows pointer, 1 built-in Orbit ring, 2 the user's own file
    int cursorColor = 1;                 // Orbit tint, same list as the clock colours
    int cursorSize = 1;                  // 0 small, 1 medium, 2 large
    int cursorTip = 0;                   // PNG files: 0 the centre clicks, 1 the top-left corner clicks
    bool cursorTrail = true;             // fading trail and click ripple inside Hibernation windows
    bool wakeProtection = true;          // ignore small net mouse movement while fullscreen is active
    bool wakeKeyboard = true;            // keyboard input may dismiss or open the fullscreen view
    bool enabled = true;
    bool grain = true;
    bool vignette = true;
    bool clock = false;
    bool allMonitors = true;
    bool pauseOnBattery = true;
    bool fullscreenGuard = true;
    bool playlist = false;
    bool gameAllowed = false;
    bool videoAllowed = false;
    bool clockPrimaryOnly = true;
    bool muteAudio = false;
    bool hideCursor = true;
    bool mediaConservative = true;
    bool clockFormat24 = true;           // false shows a 12-hour clock
    bool clockSeconds = false;
    bool hotkeysEnabled = true;
    bool showPreview = true;             // live preview panel on the Scenes tab
    bool lockEnabled = false;
    bool securityError = false;           // unreadable security data: refuse to replace it or start an unprotected screen
    bool autoStart = false;              // launch with Windows (HKCU Run entry)
    bool startShowUI = false;            // on that launch, open the window instead of the tray
    // PBKDF2-SHA256 credential ("iterations:saltBase64:hashBase64"); empty means no password set.
    std::wstring credential;
    // Copy of the user's own cursor (.png, .cur or .ani) inside the app data folder.
    std::wstring cursorFile;
    bool hasPassword() const { return !credential.empty(); }
    void sanitize() {
        delay = std::clamp(delay, 5, 300);
        wakeMouseThreshold = std::clamp(wakeMouseThreshold, 2, 200);
        speed = std::clamp(speed, 10, 100);
        brightness = std::clamp(brightness, 15, 100);
        palette = std::clamp(palette, 0, 7);
        effect = std::clamp(effect, 0, EffectCount-1);
        scale = std::clamp(scale, 50, 180);
        turbulence = std::clamp(turbulence, 0, 100);
        glow = std::clamp(glow, 0, 100);
        fps = std::clamp(fps, 5, 120);
        quality = 100;
        previewFps=std::clamp(previewFps,10,30); previewWidth=std::clamp(previewWidth,800,960);
        // Scene cards are only rendered while the settings window is visible. Keeping the
        // selected/hovered card alive makes the gallery useful without adding background load.
        animateThumbs=true;
        minFps=std::clamp(minFps,5,fps); minQuality=100;
        restAfter=std::clamp(restAfter,10,600); gpuBudget=std::clamp(gpuBudget,5,80);
        backgroundPoll=std::clamp(backgroundPoll,500,5000); maxRenderHeight=std::clamp(maxRenderHeight,360,2160);
        mediaId=std::max(0,mediaId); mediaFit=std::clamp(mediaFit,0,2);
        cycleSeconds = std::clamp(cycleSeconds, 15, 300);
        fadeSeconds = std::clamp(fadeSeconds, 0, 5);
        gameDelay=std::clamp(gameDelay,5,1800); videoDelay=std::clamp(videoDelay,5,1800);
        clockStyle=std::clamp(clockStyle,0,2); clockSize=std::clamp(clockSize,50,170);
        clockWeight=std::clamp(clockWeight,0,2); clockColor=std::clamp(clockColor,0,5);
        hotkeyToggleKey=std::clamp(hotkeyToggleKey,0,255); hotkeyPauseKey=std::clamp(hotkeyPauseKey,0,255);
        hotkeyToggleMods=std::clamp(hotkeyToggleMods,0,15); hotkeyPauseMods=std::clamp(hotkeyPauseMods,0,15);
        clockOpacity=std::clamp(clockOpacity,20,100); clockPosition=std::clamp(clockPosition,0,2);
        accent=std::clamp(accent,0,5); tone=std::clamp(tone,0,2); roundness=std::clamp(roundness,4,22);
        lockGraceMinutes=std::clamp(lockGraceMinutes,0,30);
        cursorStyle=std::clamp(cursorStyle,0,2); cursorColor=std::clamp(cursorColor,0,5);
        cursorSize=std::clamp(cursorSize,0,2); cursorTip=std::clamp(cursorTip,0,1);
    }
};
// One gesture per pointing device. Opposite movement cancels vibration; a pause
// starts a fresh gesture so unrelated nudges can never add up across idle periods.
struct WakeFilter {
    static constexpr uint64_t StopMs = 200;
    int64_t mouseX = 0;
    int64_t mouseY = 0;
    uint64_t lastMotion = 0;
    bool moving = false;
    bool pending = false;
    bool absoluteKnown = false;
    int absoluteX = 0, absoluteY = 0;
    void resetMotion() { mouseX=mouseY=0; lastMotion=0; moving=false; }
    void reset() { resetMotion(); pending=false; absoluteKnown=false; absoluteX=absoluteY=0; }
    void expire(uint64_t now) {
        if(moving && (now<lastMotion || now-lastMotion>=StopMs)) resetMotion();
    }
    void mouse(int x,int y,bool deliberate,const Settings& s,uint64_t now,bool absolute=false) {
        expire(now);
        if(pending) return;
        if(deliberate) { pending=true; return; }
        int64_t dx=x,dy=y;
        if(absolute) {
            // The first absolute report establishes position; it is not movement.
            if(!absoluteKnown) { resetMotion(); dx=dy=0; }
            else { dx=static_cast<int64_t>(x)-absoluteX; dy=static_cast<int64_t>(y)-absoluteY; }
            absoluteX=x; absoluteY=y; absoluteKnown=true;
        } else if(absoluteKnown) { resetMotion(); absoluteKnown=false; }
        if(dx==0 && dy==0) return;
        if(!s.wakeProtection) { pending=true; return; }
        moving=true; lastMotion=now;
        mouseX=std::clamp<int64_t>(mouseX+dx,-1000000,1000000);
        mouseY=std::clamp<int64_t>(mouseY+dy,-1000000,1000000);
        const int64_t threshold=s.wakeMouseThreshold;
        pending=mouseX*mouseX+mouseY*mouseY>=threshold*threshold;
    }
    void keyboard(const Settings& s) { if(!s.wakeProtection || s.wakeKeyboard) pending=true; }
    bool consume(uint64_t now) { expire(now); const bool result=pending; if(result) { resetMotion(); pending=false; } return result; }
};
// The lock engages when a password exists, the lock is enabled, the screensaver has
// been running for at least the grace period, and at least that long has passed since
// the last successful unlock. The second condition breaks the unlock/re-lock loop that
// happens when the screensaver restarts moments after the user came back. A grace of 0
// asks for the password on every return, which is what a zero grace means.
inline bool shouldLock(uint64_t activeMs, uint64_t sinceUnlockMs, const Settings& s) {
    if(!s.lockEnabled || !s.hasPassword()) return false;
    const uint64_t grace = static_cast<uint64_t>(s.lockGraceMinutes) * 60000ull;
    return activeMs >= grace && sinceUnlockMs >= grace;
}
inline void applyPreset(Settings& s, int preset) {
    // Presets affect appearance, preserving the user's activation preferences.
    const int values[4][7]={{0,0,35,80,110,45,65},{1,2,20,55,85,70,45},
        {2,1,55,85,110,60,80},{8,5,25,60,100,25,50}};
    preset=std::clamp(preset,0,3);
    s.effect=values[preset][0]; s.palette=values[preset][1]; s.speed=values[preset][2];
    s.brightness=values[preset][3]; s.scale=values[preset][4];
    s.turbulence=values[preset][5]; s.glow=values[preset][6];
}
inline int activityDelay(const Settings& s,Activity activity) {
    if(activity==Activity::Game) return s.gameAllowed?s.gameDelay:0;
    if(activity==Activity::Video) return s.videoAllowed?s.videoDelay:0;
    return s.delay;
}
inline bool shouldActivateInContext(uint32_t now,uint32_t lastInput,const Settings& s,
    Activity activity,bool visible,bool active) {
    int delay=activityDelay(s,activity);
    return s.enabled && !visible && !active && delay>0 &&
        static_cast<uint32_t>(now-lastInput)>=static_cast<uint32_t>(delay)*1000u;
}
inline Settings forMonitor(const Settings& s,bool primary) {
    Settings result=s; result.clock=s.clock && (!s.clockPrimaryOnly || primary); return result;
}
inline bool shouldActivate(uint32_t now, uint32_t lastInput, const Settings& s,
                           bool settingsVisible, bool active) {
    return s.enabled && !settingsVisible && !active &&
        static_cast<uint32_t>(now - lastInput) >= static_cast<uint32_t>(s.delay) * 1000u;
}
