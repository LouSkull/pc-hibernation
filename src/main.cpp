#include "core.h"
#include "performance.h"
#include "diagnostics.h"
#include "media.h"
#include <sstream>
#include <deque>
#include <iomanip>
#include "config.h"
#include "renderer.h"
#include "platform.h"
#include "lock.h"
#include "bridge.h"
#include "webview.h"
#include "cursor.h"
#include "update.h"
#include "version.h"
#include "resource.h"
#include <windowsx.h>
#include <thread>
#include <mutex>
#include <optional>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <lmcons.h>
#include <objidl.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <cmath>
#include <memory>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <unordered_map>

namespace {
constexpr UINT TrayMessage=WM_APP+1;
constexpr UINT UnlockMessage=WM_APP+2;
constexpr UINT MediaFailure=WM_APP+3;
constexpr UINT MediaDialogMessage=WM_APP+5;
constexpr UINT CursorDialogMessage=WM_APP+6;
constexpr UINT UpdateResultMessage=WM_APP+7;
constexpr int ThumbWidth=278,ThumbHeight=112;

HINSTANCE instance{};
HWND mainWindow{},previewWindow{},galleryWindow{},lockWindow{},trayPanel{},debugWindow{};
HICON appIcon{};
Settings settings;
PerformancePolicy performance;
Diagnostics diagnostics;
MediaLibrary mediaLibrary;
bool previewVisible=false, previewsReady=false, settingsDirty=false, debugLogFull=false;
int lastCadence=0;
double lastPresented=0;
std::string lastDiagnostics, videoStats;
std::deque<std::string> runHistory;
double videoFps=0;
bool mediaResting=false;
std::wstring overlayText;
std::ofstream debugCsv;
Bridge::Status status;
Renderer preview, gallery;
int hoverScene=-1, nextThumbnail=0;
bool thumbPending=false, lockResting=false;
ULONGLONG thumbSentAt=0;
DWORD restInput=0;
WakeFilter wakeFilter;
std::unordered_map<HANDLE,WakeFilter> mouseWakeFilters;
bool wakeInputRegistered=false;
ULONGLONG wakeArmedAt=0;
WebView appView,lockView,trayView;
struct Screen { HWND window{}; std::unique_ptr<Renderer> renderer; bool primary=false; std::unique_ptr<WebView> media; };
std::vector<Screen> screens;
std::wstring configPath,presetsDir,userName;
// --data-dir <folder>: settings, presets, media and browser data live there instead of
// %LOCALAPPDATA%, and the instance runs beside a normal one (used by the runtime checks).
std::wstring dataFolder,webProfile;
std::filesystem::path executableDir;
float dpiScale=1.f,animationTime=0;
bool sessionLocked=false,smoke=false,uiTest=false,smokeFailed=false,trayAdded=false,closing=false,launchPending=false;
bool thumbnailsDirty=true,pageReady=false;
MediaMonitor mediaMonitor;
AudioGuard audioGuard;
bool smokeRenderOK=false,smokeNativeResolution=true;
int smokeStage=0,fullscreenFrames=0,smokeMonitors=0;
ULONGLONG activatedAt=0,startedAt=0,lastFrame=0,cooldownUntil=0,lastScene=0;
DWORD activationInput=0;
UINT taskbarCreated=0;
std::mt19937 randomEngine{std::random_device{}()};
// Return-from-screensaver lock.
bool locked=false;
bool unlockVerified=false;
ULONGLONG securityGrantUntil=0;
int authAttempts=0;
ULONGLONG authCooldownUntil=0;
HHOOK keyHook{};
int lockAttempts=0;
ULONGLONG lockCooldownUntil=0,lastUnlockAt=0;
ULONGLONG lockLoadingSince=0;
ULONGLONG windowsLockRequestedAt=0;
// Live preview and eased window dragging.
bool framePending=false;
bool clockPreviewRequested=false;
ULONGLONG frameSentAt=0,lastGalleryFrame=0;
std::string currentTab="scenes";
ULONGLONG lastPreviewFrame=0;
bool dragging=false,dragEase=true;
POINT dragGrab{},dragTarget{},dragCurrent{};
// Custom pointer: native handle for the fullscreen surfaces (null means the Windows arrow)
// and the message that gives every HTML window the same picture.
HCURSOR appCursor{};
std::string cursorPayload="cursor:{\"on\":false}";
bool ownIcon=false;
std::wstring pendingUpdateUrl;   // release page from the last "update available" result
struct UpdateState {
    std::mutex mutex;
    bool busy=false,closed=false;
    std::optional<Update::Result> result;
};
auto updateState=std::make_shared<UpdateState>();

void engageLock();
void refreshCursor();
HCURSOR pointer() { return appCursor?appCursor:LoadCursorW(nullptr,IDC_ARROW); }
void disengageLock();
void postLockTheme();
void pushState();
bool previewLive();
void pushLibrary();
void updateDiagnostics(ULONGLONG now);
bool mediaActive() { return !screens.empty() && screens.front().media!=nullptr; }
bool ensurePreviews() {
    if(previewsReady) return true;
    std::wstring error;
    previewsReady=preview.init(previewWindow,error) && gallery.init(galleryWindow,error);
    return previewsReady;
}
void releasePreviews() { if(previewsReady) { preview=Renderer{}; gallery=Renderer{}; previewsReady=false; framePending=thumbPending=false; } }
void sizeScreens() {
    for(auto& screen:screens) if(screen.renderer) {
        RECT r{}; GetClientRect(screen.window,&r);
        const UINT width=std::max(1L,r.right),height=std::max(1L,r.bottom);
        screen.renderer->resize(width,height);
        if(smoke && (screen.renderer->renderWidth()!=width || screen.renderer->renderHeight()!=height))
            smokeNativeResolution=false;
    }
}

int px(float v) { return static_cast<int>(std::lround(v*dpiScale)); }
void cadence() {
    if(closing || !mainWindow) return;
    bool visible=IsWindowVisible(mainWindow) && !IsIconic(mainWindow) && screens.empty();
    appView.setActive(visible);
    mediaMonitor.setMonitoring(screens.empty() && !sessionLocked && settings.enabled);
    bool eco=settings.ecoPriority && !visible;
    SetPriorityClass(GetCurrentProcess(),eco?BELOW_NORMAL_PRIORITY_CLASS:NORMAL_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE power{PROCESS_POWER_THROTTLING_CURRENT_VERSION,PROCESS_POWER_THROTTLING_EXECUTION_SPEED,eco?PROCESS_POWER_THROTTLING_EXECUTION_SPEED:0ul};
    SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&power,sizeof(power));
    if(!visible && !smoke) releasePreviews();
    const bool animate=visible && pageReady && (previewLive() || currentTab=="scenes");
    int interval=!screens.empty()?(mediaActive()?200:std::max(8,1000/std::max(5,performance.fps))):
        (animate?std::max(16,1000/settings.previewFps):settings.backgroundPoll);
    if(smoke) interval=std::min(interval,100);
    if(interval!=lastCadence) { lastCadence=interval; SetTimer(mainWindow,1,interval,nullptr); }
}
void toast(const std::string& text) { appView.post("toast:"+text); }   // queued until the page is live
void save() {
    settings.sanitize();
    if(!smoke && !writeSettings(configPath,settings)) toast("Settings could not be saved. Check access to the app folder.");
    cadence();
}
void load() {
    if(smoke) return;
    PWSTR local=nullptr;
    if(!dataFolder.empty()) {
        CreateDirectoryW(dataFolder.c_str(),nullptr);
        configPath=dataFolder+L"\\settings.ini"; presetsDir=dataFolder+L"\\presets";
    } else if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) {
        std::wstring dir=std::wstring(local)+L"\\Hibernation"; CoTaskMemFree(local); CreateDirectoryW(dir.c_str(),nullptr);
        configPath=dir+L"\\settings.ini"; presetsDir=dir+L"\\presets";
    } else { configPath=(executableDir/L"settings.ini").wstring(); presetsDir=(executableDir/L"presets").wstring(); }
    CreateDirectoryW(presetsDir.c_str(),nullptr);
    settings=readSettings(configPath);
    if(settings.securityError) status.message="Protected password data cannot be read. Restore settings from this Windows account. Fullscreen start is disabled.";
    else if(settings.hasPassword() && !writeSettings(configPath,settings)) {
        settings.securityError=true; settings.enabled=false;
        status.message="Could not protect the password file. Check access to the settings folder.";
    }
    mediaLibrary.open(std::filesystem::path(configPath).parent_path()/L"Media");
}
void resetWakeInput() { wakeFilter.reset(); mouseWakeFilters.clear(); }
bool consumeWakeInput(ULONGLONG now) {
    bool wake=wakeFilter.consume(now);
    for(auto& device:mouseWakeFilters) if(device.second.consume(now)) wake=true;
    // Keep absolute positions and device entries across accepted gestures.
    if(wake) for(auto& device:mouseWakeFilters) device.second.resetMotion();
    return wake;
}
bool registerWakeInput() {
    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage=0x01; devices[0].usUsage=0x02; devices[0].dwFlags=RIDEV_INPUTSINK|RIDEV_DEVNOTIFY; devices[0].hwndTarget=mainWindow;
    devices[1].usUsagePage=0x01; devices[1].usUsage=0x06; devices[1].dwFlags=RIDEV_INPUTSINK; devices[1].hwndTarget=mainWindow;
    wakeInputRegistered=RegisterRawInputDevices(devices,2,sizeof(RAWINPUTDEVICE))!=FALSE;
    return wakeInputRegistered;
}
void unregisterWakeInput() {
    if(wakeInputRegistered) {
        RAWINPUTDEVICE devices[2]{};
        devices[0].usUsagePage=0x01; devices[0].usUsage=0x02; devices[0].dwFlags=RIDEV_REMOVE;
        devices[1].usUsagePage=0x01; devices[1].usUsage=0x06; devices[1].dwFlags=RIDEV_REMOVE;
        RegisterRawInputDevices(devices,2,sizeof(RAWINPUTDEVICE));
    }
    wakeInputRegistered=false; resetWakeInput();
}
void recordWakeInput(HRAWINPUT handle) {
    const ULONGLONG now=GetTickCount64();
    if(!wakeInputRegistered || screens.empty() || now<wakeArmedAt) return;
    // Use the event's time, not the time a busy render thread finally reads it.
    // Unsigned subtraction expands the 32-bit message tick across its rollover.
    const DWORD eventAge=static_cast<DWORD>(now)-static_cast<DWORD>(GetMessageTime());
    const ULONGLONG eventTime=now-std::min<ULONGLONG>(eventAge,now);
    if(eventTime<wakeArmedAt) return;
    // Only mouse/keyboard devices are registered, so their fixed-size reports fit
    // on the stack. No heap allocation per packet, even for a high-polling mouse.
    RAWINPUT raw{}; UINT size=sizeof(raw);
    const UINT read=GetRawInputData(handle,RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER));
    if(read==static_cast<UINT>(-1) || read<sizeof(RAWINPUTHEADER)) return;
    if(raw.header.dwType==RIM_TYPEKEYBOARD) {
        if(read<sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)) return;
        if((!locked || lockResting) && !(raw.data.keyboard.Flags&RI_KEY_BREAK)) wakeFilter.keyboard(settings);
        return;
    }
    if(raw.header.dwType!=RIM_TYPEMOUSE || read<sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)) return;
    const auto& mouse=raw.data.mouse;
    constexpr USHORT deliberateButtons=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_RIGHT_BUTTON_DOWN|
        RI_MOUSE_MIDDLE_BUTTON_DOWN|RI_MOUSE_BUTTON_4_DOWN|RI_MOUSE_BUTTON_5_DOWN|RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL;
    const bool absolute=(mouse.usFlags&MOUSE_MOVE_ABSOLUTE)!=0;
    int x=mouse.lLastX,y=mouse.lLastY;
    if(absolute) {
        const bool desktop=(mouse.usFlags&MOUSE_VIRTUAL_DESKTOP)!=0;
        x=MulDiv(std::clamp<LONG>(mouse.lLastX,0,65535),GetSystemMetrics(desktop?SM_CXVIRTUALSCREEN:SM_CXSCREEN)-1,65535);
        y=MulDiv(std::clamp<LONG>(mouse.lLastY,0,65535),GetSystemMetrics(desktop?SM_CYVIRTUALSCREEN:SM_CYSCREEN)-1,65535);
    }
    mouseWakeFilters[raw.header.hDevice].mouse(x,y,(mouse.usButtonFlags&deliberateButtons)!=0,settings,eventTime,absolute);
}
// Presets are plain .ini files in one folder, so they can be shared by copying a file.
bool validPresetName(const std::wstring& name) {
    if(name.empty() || name.size()>48) return false;
    for(wchar_t c:name) if(c<32 || wcschr(L"\\/:*?\"<>|",c)) return false;
    return true;
}
std::wstring presetPath(const std::wstring& name) { return presetsDir+L"\\"+name+L".ini"; }
std::vector<std::wstring> presetNames() {
    std::vector<std::wstring> names; std::error_code error;
    for(std::filesystem::directory_iterator it(presetsDir,error),end; !error && it!=end; it.increment(error)) {
        const auto& entry=*it;
        if(entry.is_regular_file(error) && entry.path().extension()==L".ini") names.push_back(entry.path().stem().wstring());
    }
    return names;
}
void stopScreens() {
    windowsLockRequestedAt=0;
    unregisterWakeInput();
    if(debugWindow) { DestroyWindow(debugWindow); debugWindow=nullptr; }
    disengageLock();
    audioGuard.setMuted(false);
    for(auto& screen:screens) { screen.media.reset(); screen.renderer.reset(); DestroyWindow(screen.window); }
    screens.clear();
    if(!runHistory.empty()) { std::string history="history:[";for(const auto& frame:runHistory) { if(history.back()!='[')history+=",";history+=frame; }appView.post(history+"]");appView.post("lastRun:"+runHistory.back());runHistory.clear(); }
    SetCursor(LoadCursorW(nullptr,IDC_ARROW)); cooldownUntil=GetTickCount64()+1000; cadence();
}
bool passwordRequired() {
    const auto now=GetTickCount64();
    return !screens.empty() && !smoke && (locked || shouldLock(now-activatedAt,now-lastUnlockAt,settings));
}
void requestReturn() {
    if(passwordRequired()) {
        if(!locked) engageLock();
        else if(lockWindow) { lockResting=false; lockView.setActive(true); ShowWindow(lockWindow,SW_SHOW); SetForegroundWindow(lockWindow); lockView.focus(); lockView.post("reveal"); }
    } else stopScreens();
}
bool stopScreensSafely() {
    // LockWorkStation is asynchronous. Keep the surfaces until Windows confirms
    // WTS_SESSION_LOCK; a successful request alone is not proof that the session locked.
    if(passwordRequired()) {
        const auto now=GetTickCount64();
        if(!windowsLockRequestedAt || now-windowsLockRequestedAt>=5000) {
            windowsLockRequestedAt=now; LockWorkStation();
        }
        return false;
    }
    stopScreens(); return true;
}
void showSettings() {
    if(locked) return;
    requestReturn(); if(!screens.empty()) return;
    ShowWindow(mainWindow,SW_RESTORE); SetForegroundWindow(mainWindow); appView.focus(); cadence();
}
void hideSettings() {
    if(!trayAdded) { toast("The tray icon is unavailable. Use minimize or System - Quit."); return; }
    save(); ShowWindow(mainWindow,SW_HIDE); cadence();
}
BOOL CALLBACK addScreen(HMONITOR monitor,HDC,LPRECT,LPARAM) {
    MONITORINFO info{sizeof(info)}; if(!GetMonitorInfoW(monitor,&info)) return FALSE;
    if(!settings.allMonitors && !(info.dwFlags&MONITORINFOF_PRIMARY)) return TRUE;
    auto r=info.rcMonitor;
    HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"HibernationSurface",L"Hibernation",WS_POPUP,
        r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    if(!window) return FALSE;
    auto item=mediaLibrary.find(settings.mediaId);
    if(item.id && !smoke) {
        Screen screen{window,nullptr,(info.dwFlags&MONITORINFOF_PRIMARY)!=0,std::make_unique<WebView>()};
        auto* view=screen.media.get(); bool primary=screen.primary;
        view->setProfileFolder(webProfile);
        view->setMediaFolder(mediaLibrary.folder().wstring());
        view->setMessageHandler([primary](const std::string& raw) {
            if(raw=="mediaError" || raw.rfind("webviewError:",0)==0) PostMessageW(mainWindow,MediaFailure,0,0);
            if(primary && raw.rfind("videoStats:",0)==0) {
                videoStats=raw.substr(11);
                auto offset=videoStats.find("\"fps\":");
                if(offset!=std::string::npos) try { videoFps=std::stod(videoStats.substr(offset+6)); } catch(...) { videoFps=0; }
            }
        });
        view->setReadyHandler([view,item,primary] {
            const auto visual=forMonitor(settings,primary);
            std::string data="media:{\"url\":\"https://hibernation-media.local/"+Bridge::narrow(item.file)+"?v="+std::to_string(GetTickCount64())+"\",\"fit\":"+std::to_string(settings.mediaFit)+",\"debug\":"+(settings.debugEnabled?"true":"false")+",\"hideCursor\":"+(settings.hideCursor?"true":"false")+
                ",\"clock\":"+(visual.clock?"true":"false")+",\"clockSize\":"+std::to_string(settings.clockSize)+",\"clockOpacity\":"+std::to_string(settings.clockOpacity)+
                ",\"clockPosition\":"+std::to_string(settings.clockPosition)+",\"clockSeconds\":"+(settings.clockSeconds?"true":"false")+",\"clockFormat24\":"+(settings.clockFormat24?"true":"false")+"}";
            view->post(data);
            view->post(cursorPayload);
        });
        std::wstring error;
        if(!view->create(window,loadHtmlResource(IDR_MEDIA_HTML),error)) { DestroyWindow(window); return FALSE; }
        screens.push_back(std::move(screen)); return TRUE;
    }
    auto renderer=std::make_unique<Renderer>(); std::wstring error;
    if(!renderer->init(window,error,100,settings.softwareRenderer)) { DestroyWindow(window); return FALSE; }
    if(renderer->isSoftware()) performance.reset(settings,true);
    screens.push_back({window,std::move(renderer),(info.dwFlags&MONITORINFOF_PRIMARY)!=0,nullptr}); return TRUE;
}
void startScreens() {
    if(!screens.empty() || sessionLocked || closing) return;
    if(settings.securityError) { toast("Password protection is unavailable. Restore the protected settings file first."); return; }
    securityGrantUntil=0;
    performance.reset(settings,settings.softwareRenderer); videoStats.clear();videoFps=0;mediaResting=false;
    diagnostics.frames=0;diagnostics.totalCost=diagnostics.peakCost=0;diagnostics.gpuMs=-1; diagnostics.epoch=preciseSeconds(); diagnostics.lastFrame=0; lastPresented=0;runHistory.clear();
    if(settings.mediaId && !mediaLibrary.find(settings.mediaId).id) { settings.mediaId=0; toast("Media is missing. Using a built-in scene."); }
    if(!EnumDisplayMonitors(nullptr,nullptr,addScreen,0) || screens.empty()) {
        stopScreens();
        if(smoke) { smokeFailed=true; DestroyWindow(mainWindow); return; }
        settings.enabled=false; save(); showSettings();
        toast("The screens could not be started. Automatic start is paused."); return;
    }
    LASTINPUTINFO input{sizeof(input)}; GetLastInputInfo(&input); activationInput=input.dwTime;
    resetWakeInput(); wakeArmedAt=GetTickCount64()+500; registerWakeInput();
    sizeScreens();
    activatedAt=lastScene=GetTickCount64(); bool ok=true;
    for(auto& screen:screens) {
        bool rendered=!screen.renderer || screen.renderer->draw(animationTime,forMonitor(settings,screen.primary),smoke?1.f:0.f); ok=rendered&&ok;
        if(smoke && rendered) fullscreenFrames++;
        ShowWindow(screen.window,SW_SHOW);
    }
    if(!ok) { smokeFailed=true; stopScreens(); if(!smoke) toast("Rendering failed. Try a lower resolution."); return; }
    if(smoke) smokeMonitors=static_cast<int>(screens.size());
    SetForegroundWindow(screens.front().window);
    SetCursor(settings.hideCursor?nullptr:pointer());
    if(!smoke && settings.muteAudio && !audioGuard.setMuted(true)) toast("Audio could not be muted.");
    cadence();
}
void addTray() {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=mainWindow; data.uID=1;
    data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; data.uCallbackMessage=TrayMessage;
    data.hIcon=appIcon; wcscpy_s(data.szTip,L"Hibernation");
    trayAdded=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
}
bool blockedByEnvironment() {
    if(settings.pauseOnBattery) { SYSTEM_POWER_STATUS power{}; if(GetSystemPowerStatus(&power)&&power.ACLineStatus==0) return true; }
    return false;
}

/* ---------------- scene thumbnails ---------------- */
bool pngEncoder(CLSID& clsid) {
    UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
    if(!size) return false;
    std::vector<BYTE> bytes(size);
    auto encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
    Gdiplus::GetImageEncoders(count,size,encoders);
    for(UINT i=0;i<count;i++) if(!wcscmp(encoders[i].MimeType,L"image/png")) { clsid=encoders[i].Clsid; return true; }
    return false;
}
std::string pngDataUrl(std::vector<unsigned>& pixels,UINT w,UINT h) {
    CLSID png{}; if(!pngEncoder(png) || pixels.empty()) return {};
    Gdiplus::Bitmap bitmap(w,h,w*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(pixels.data()));
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)) || !stream) return {};
    std::string url;
    if(bitmap.Save(stream,&png)==Gdiplus::Ok) {
        HGLOBAL memory=nullptr;
        if(SUCCEEDED(GetHGlobalFromStream(stream,&memory)) && memory) {
            SIZE_T size=GlobalSize(memory);
            if(auto* data=static_cast<uint8_t*>(GlobalLock(memory))) {
                url="data:image/png;base64,"+Bridge::narrow(Lock::base64Encode(std::vector<uint8_t>(data,data+size)));
                GlobalUnlock(memory);
            }
        }
    }
    stream->Release(); return url;
}
// Preview frames go out as JPEG: a PNG of the same frame is several times larger and
// would choke the bridge at a dozen frames a second.
std::string jpegDataUrl(std::vector<unsigned>& pixels,UINT w,UINT h,LONG quality) {
    if(pixels.empty()) return {};
    UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
    if(!size) return {};
    std::vector<BYTE> bytes(size);
    auto encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
    Gdiplus::GetImageEncoders(count,size,encoders);
    CLSID jpeg{}; bool found=false;
    for(UINT i=0;i<count;i++) if(!wcscmp(encoders[i].MimeType,L"image/jpeg")) { jpeg=encoders[i].Clsid; found=true; }
    if(!found) return {};
    Gdiplus::Bitmap bitmap(w,h,w*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(pixels.data()));
    Gdiplus::EncoderParameters params{};
    params.Count=1; params.Parameter[0].Guid=Gdiplus::EncoderQuality;
    params.Parameter[0].Type=Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues=1; params.Parameter[0].Value=&quality;
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)) || !stream) return {};
    std::string url;
    if(bitmap.Save(stream,&jpeg,&params)==Gdiplus::Ok) {
        HGLOBAL memory=nullptr;
        if(SUCCEEDED(GetHGlobalFromStream(stream,&memory)) && memory) {
            SIZE_T length=GlobalSize(memory);
            if(auto* data=static_cast<uint8_t*>(GlobalLock(memory))) {
                url="data:image/jpeg;base64,"+Bridge::narrow(Lock::base64Encode(std::vector<uint8_t>(data,data+length)));
                GlobalUnlock(memory);
            }
        }
    }
    stream->Release(); return url;
}
bool previewLive() {
    return pageReady && !smoke && screens.empty() && ((settings.mediaId==0 && previewVisible && settings.showPreview && (currentTab=="scenes" || currentTab=="appearance" || currentTab=="behavior" || currentTab=="performance")) || clockPreviewRequested) &&
           mainWindow && IsWindowVisible(mainWindow) && !IsIconic(mainWindow);
}
void pushPreviewFrame() {
    if(!previewLive() || !ensurePreviews()) return;
    const double renderStart=preciseSeconds();
    if(framePending && GetTickCount64()-frameSentAt<1500) return;
    if(!preview.resize(settings.previewWidth,settings.previewWidth*9/16)) return;
    Settings view=settings;
    if(clockPreviewRequested) view.clock=true;
    if(!preview.draw(animationTime,view,1.f,false)) return;
    std::vector<unsigned> pixels; UINT w=0,h=0;
    if(!preview.readPixels(pixels,w,h)) return;
    std::string url=jpegDataUrl(pixels,w,h,86);
    if(!url.empty()) { framePending=true; frameSentAt=GetTickCount64(); appView.post("frame:"+url); diagnostics.frame((preciseSeconds()-renderStart)*1000,-1,settings.previewFps); }
}
void pushThumbnails() {
    if(!pageReady || smoke || !ensurePreviews() || (thumbPending && GetTickCount64()-thumbSentAt<1500)) return;
    // Complete the initial gallery pass, then keep the selected or hovered card alive.
    // Only one small card is rendered per tick, so this does not multiply GPU work by 12.
    const int effect=thumbnailsDirty?nextThumbnail:(hoverScene>=0?hoverScene:settings.effect);
    if(effect<0 || effect>=EffectCount) return;
    Settings view=settings; view.clock=false; view.effect=effect;
    if(!gallery.draw(animationTime,view,1.f,false)) return;
    std::vector<unsigned> pixels; UINT w=0,h=0;
    if(!gallery.readPixels(pixels,w,h)) return;
    std::string url=jpegDataUrl(pixels,w,h,80);
    if(url.empty()) return;
    thumbPending=true; thumbSentAt=GetTickCount64();
    appView.post("thumb:"+std::to_string(effect)+":"+url);
    if(thumbnailsDirty && ++nextThumbnail>=EffectCount) { nextThumbnail=0; thumbnailsDirty=false; cadence(); }
}
void pushPresets() {
    std::string list;
    for(const auto& name:presetNames()) { if(!list.empty()) list+="|"; list+=Bridge::narrow(name); }
    status.hasPreset=!list.empty();
    appView.post("presets:"+list);
}
// Global shortcuts come from settings, so they can be rebound or switched off entirely.
void applyHotkeys() {
    if(!mainWindow || smoke) return;
    UnregisterHotKey(mainWindow,1); UnregisterHotKey(mainWindow,2);
    if(!settings.hotkeysEnabled) { status.hotkeysOk=true; return; }
    UINT toggle=static_cast<UINT>(settings.hotkeyToggleMods)|MOD_NOREPEAT;
    UINT pause=static_cast<UINT>(settings.hotkeyPauseMods)|MOD_NOREPEAT;
    bool a=settings.hotkeyToggleKey==0 || RegisterHotKey(mainWindow,1,toggle,static_cast<UINT>(settings.hotkeyToggleKey))!=FALSE;
    bool b=settings.hotkeyPauseKey==0 || RegisterHotKey(mainWindow,2,pause,static_cast<UINT>(settings.hotkeyPauseKey))!=FALSE;
    status.hotkeysOk=a&&b;
}
void pushState() {
    // No ready() guard here: WebView::post queues until the page is live. The page's own
    // "ready" message can arrive before NavigationCompleted, and dropping the state at
    // that moment leaves the whole interface empty.
    status.monitors=GetSystemMetrics(SM_CMONITORS);
    status.running=!screens.empty();
    std::string json=Bridge::toJson(settings,status);
    appView.post(json);
    if(trayPanel) trayView.post(json);   // the tray panel mirrors the same state
    status.message.clear();
}
#include "main_support.h"

/* ---------------- pointer ---------------- */
// One picture for every window: the native cursor of the fullscreen surfaces and the CSS
// cursor of the HTML pages are both built here from the same settings.
std::filesystem::path cursorFolder() { return std::filesystem::path(configPath).parent_path()/L"Cursors"; }
// Only a copy inside the app's own folder is ever loaded, so an imported .ini cannot
// point the app at another path (a network share, say) just by naming it.
bool ownCursorFile(const std::wstring& file) {
    if(file.empty() || configPath.empty()) return false;
    std::error_code error;
    const auto folder=std::filesystem::weakly_canonical(cursorFolder(),error);
    if(error) return false;
    const auto path=std::filesystem::weakly_canonical(file,error);
    if(error) return false;
    return _wcsicmp(path.parent_path().c_str(),folder.c_str())==0 && std::filesystem::is_regular_file(path,error);
}
void refreshCursor() {
    HCURSOR previous=appCursor;
    appCursor=nullptr;
    const int base=CursorArt::baseSize(settings.cursorSize);
    const int native=std::max(12,static_cast<int>(std::lround(base*dpiScale)));
    const bool centred=settings.cursorTip==0;
    CursorArt::Image single,twice;
    std::string color;
    if(settings.cursorStyle==1) {
        const uint32_t tint=CursorArt::tint(settings.cursorColor);
        single=CursorArt::orbit(base,tint); twice=CursorArt::orbit(base*2,tint);
        appCursor=CursorArt::toCursor(CursorArt::orbit(native,tint));
        color=CursorArt::hex(tint);
    } else if(settings.cursorStyle==2 && ownCursorFile(settings.cursorFile) &&
              CursorArt::loadFile(settings.cursorFile,base,centred,single) &&
              CursorArt::loadFile(settings.cursorFile,base*2,centred,twice)) {
        appCursor=CursorArt::loadNative(settings.cursorFile,native,centred);
    }
    std::string one=CursorArt::pngDataUrl(single),two=CursorArt::pngDataUrl(twice);
    if(!appCursor || one.empty() || two.empty()) {
        if(appCursor) { DestroyCursor(appCursor); appCursor=nullptr; }
        cursorPayload="cursor:{\"on\":false,\"trail\":false}";
    } else {
        cursorPayload="cursor:{\"on\":true,\"url1\":\""+one+"\",\"url2\":\""+two+"\",\"x\":"+std::to_string(single.hotX)+
            ",\"y\":"+std::to_string(single.hotY)+",\"size\":"+std::to_string(base)+",\"trail\":"+(settings.cursorTrail?"true":"false")+
            ",\"color\":\""+color+"\"}";
    }
    appView.post(cursorPayload); trayView.post(cursorPayload);
    if(lockWindow) lockView.post(cursorPayload);
    for(auto& screen:screens) if(screen.media) screen.media->post(cursorPayload);
    if(previous) {
        if(GetCursor()==previous) SetCursor(pointer());
        DestroyCursor(previous);
    }
}
void chooseCursor() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) { toast("Windows could not open the file picker."); return; }
    const COMDLG_FILTERSPEC filters[]={
        {L"Cursors and images",L"*.png;*.cur;*.ani"},{L"PNG image",L"*.png"},{L"Windows cursor",L"*.cur;*.ani"}
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)),filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(L"Choose a cursor for Hibernation");
    FILEOPENDIALOGOPTIONS options{};
    if(SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    selectingMedia=true;   // keeps the screensaver from starting behind the dialog
    SetForegroundWindow(mainWindow);
    HRESULT shown=dialog->Show(mainWindow);
    selectingMedia=false;
    appView.focus();
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR picked=nullptr;
    if(FAILED(shown) || FAILED(dialog->GetResult(&item)) || !item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&picked)) || !picked) {
        if(picked) CoTaskMemFree(picked);
        toast("The selected file could not be read."); return;
    }
    const std::filesystem::path source(picked);
    CoTaskMemFree(picked);
    std::error_code error;
    const auto bytes=std::filesystem::file_size(source,error);
    if(error || bytes==0 || bytes>4*1024*1024) { toast("Pick a PNG, CUR or ANI file under 4 MB."); return; }
    CursorArt::Image probe;
    if(!CursorArt::loadFile(source.wstring(),32,true,probe)) { toast("Hibernation cannot read that file as a cursor."); return; }
    std::filesystem::create_directories(cursorFolder(),error);
    const auto target=cursorFolder()/source.filename();
    if(!std::filesystem::equivalent(source,target,error) && !CopyFileW(source.c_str(),target.c_str(),FALSE)) {
        toast("The cursor could not be copied into the app folder."); return;
    }
    // One copy at a time: the file this one replaces is no longer needed.
    if(ownCursorFile(settings.cursorFile) && _wcsicmp(settings.cursorFile.c_str(),target.c_str())!=0) DeleteFileW(settings.cursorFile.c_str());
    settings.cursorFile=target.wstring(); settings.cursorStyle=2; save();
    refreshCursor(); pushState(); toast("Cursor added.");
}

/* ---------------- start with Windows ---------------- */
// A per-user HKCU Run entry: no admin rights, and it only affects this account. The registry
// is the source of truth for whether it is on; the stored setting just remembers the intent.
const wchar_t* const RunKeyPath=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const RunValueName=L"Hibernation";
std::wstring exePath() { wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768); return path; }
bool autostartActive() {
    HKEY key;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,RunKeyPath,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return false;
    DWORD type=0,size=0;
    bool present=RegQueryValueExW(key,RunValueName,nullptr,&type,nullptr,&size)==ERROR_SUCCESS && type==REG_SZ;
    RegCloseKey(key);
    return present;
}
void applyAutostart() {
    if(smoke) return;
    HKEY key;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,RunKeyPath,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) {
        toast("Startup settings could not be changed."); return;
    }
    if(settings.autoStart) {
        // Quotes matter: the path can contain spaces. Without --tray the window opens on login.
        std::wstring command=L"\""+exePath()+L"\""+(settings.startShowUI?L"":L" --tray");
        RegSetValueExW(key,RunValueName,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    } else RegDeleteValueW(key,RunValueName);
    RegCloseKey(key);
    status.autoStart=autostartActive();
}

/* ---------------- update check ---------------- */
// Asks GitHub for the latest release on a background thread, then posts the result back to
// this window. Only a github.com release page is ever handed to the browser.
void beginUpdateCheck() {
    if(smoke) return;
    const auto state=updateState;
    { std::lock_guard guard(state->mutex); if(state->busy || state->closed) return; state->busy=true; }
    appView.post("update:{\"state\":\"checking\"}");
    try {
        std::thread([state,window=mainWindow]{
            Update::Result result;
            try { result=Update::fetch(Bridge::widen(APP_REPO_OWNER),Bridge::widen(APP_REPO_NAME),Bridge::widen(APP_VERSION_STRING)); }
            catch(...) { result.error=L"The update check could not finish."; }
            std::lock_guard guard(state->mutex);
            if(state->closed) return;
            state->result=std::move(result);
            if(!PostMessageW(window,UpdateResultMessage,0,0)) { state->result.reset(); state->busy=false; }
        }).detach();
    } catch(...) {
        { std::lock_guard guard(state->mutex); state->busy=false; }
        appView.post("update:{\"state\":\"error\",\"message\":\"The update check could not start.\"}");
    }
}
void deliverUpdateResult() {
    std::optional<Update::Result> owned;
    { std::lock_guard guard(updateState->mutex);
      if(!updateState->result) return;
      owned=std::move(updateState->result); updateState->result.reset(); updateState->busy=false; }
    pendingUpdateUrl.clear();
    std::string json="update:{";
    if(!owned->ok) {
        json+="\"state\":\"error\",\"message\":\""+Bridge::escape(Bridge::narrow(owned->error))+"\"";
    } else {
        const bool safeUrl=Update::safeReleaseUrl(owned->url,Bridge::widen(APP_REPO_OWNER),Bridge::widen(APP_REPO_NAME));
        if(owned->newer && safeUrl) pendingUpdateUrl=owned->url;
        json+="\"state\":\""+std::string(owned->newer?"available":"current")+"\"";
        json+=",\"latest\":\""+Bridge::escape(Bridge::narrow(owned->latest))+"\"";
        json+=",\"current\":\""+Bridge::escape(Bridge::narrow(owned->current))+"\"";
        json+=",\"canOpen\":"+std::string(pendingUpdateUrl.empty()?"false":"true");
        json+=",\"notes\":\""+Bridge::escape(Bridge::narrow(owned->notes))+"\"";
    }
    json+="}";
    appView.post(json);
}

/* ---------------- settings page messages ---------------- */
void chooseFile(bool exporting) {
    wchar_t path[32768]=L"hibernation.ini";
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=mainWindow; dialog.lpstrFilter=L"Hibernation settings (*.ini)\0*.ini\0\0";
    dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.lpstrDefExt=L"ini";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(exporting?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(exporting?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))) return;
    if(exporting) {
        std::error_code error;
        if(std::filesystem::equivalent(path,configPath,error)) { toast("Choose a different file from the active settings."); return; }
        toast(writeSettings(path,settings,false)?"Settings exported without password data.":"The file could not be written."); return;
    }
    wchar_t section[8]{};
    if(!GetPrivateProfileStringW(L"Settings",nullptr,L"",section,8,path)) { toast("That file has no [Settings] section."); return; }
    settings=readSettings(path,settings,false); thumbnailsDirty=true; save();
    applyHotkeys(); refreshCursor();
    status.message="Settings imported."; pushState();
}
void command(const std::string& name) {
    if(locked || passwordRequired()) return;
    if(name=="debugBaseline") { if(!settings.debugEnabled || diagnostics.processTimes.empty()) { toast("Enable diagnostics and wait for a sample first.");return; } diagnostics.baseline();toast("Current app and system CPU / memory saved as baseline.");return; }
    if(name=="debugReset") { diagnostics.reset();toast("Counters reset.");return; }
    if(name=="debugCopy") { copyDiagnostics();return; }
    if(name=="mediaFolder") { ShellExecuteW(nullptr,L"open",mediaLibrary.folder().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return; }
    if(name=="debugFolder") { auto dir=std::filesystem::path(configPath).parent_path()/L"Diagnostics";std::error_code error;std::filesystem::create_directories(dir,error);if(error) toast("The diagnostics folder could not be opened.");else ShellExecuteW(nullptr,L"open",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return; }
    if(name=="dragStart") {
        POINT p{}; GetCursorPos(&p); RECT r{}; GetWindowRect(mainWindow,&r);
        dragGrab={p.x-r.left,p.y-r.top}; dragCurrent={r.left,r.top}; dragTarget=dragCurrent;
        dragEase=true; dragging=true;
        SetTimer(mainWindow,5,16,nullptr); return;
    }
    if(name=="dragEnd") {
        dragging=false; KillTimer(mainWindow,5);
        SetWindowPos(mainWindow,nullptr,dragTarget.x,dragTarget.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE); return;
    }
    if(name=="minimize") { ShowWindow(mainWindow,SW_MINIMIZE); cadence(); return; }
    if(name=="tray") { hideSettings(); return; }
    if(name=="exit") { DestroyWindow(mainWindow); return; }
    if(name=="launch") { startScreens(); return; }
    if(name=="export") { chooseFile(true); return; }
    if(name=="import") { chooseFile(false); return; }
    if(name=="presets") { pushPresets(); return; }
    if(name=="presetFolder") {
        ShellExecuteW(nullptr,L"open",presetsDir.c_str(),nullptr,nullptr,SW_SHOWNORMAL); return;
    }
    if(name.rfind("presetSave:",0)==0) {
        std::wstring preset=Bridge::widen(name.substr(11));
        if(!validPresetName(preset)) { toast("That preset name is not allowed."); return; }
        Settings appearance; copyAppearance(appearance,settings);
        status.message=writeSettings(presetPath(preset),appearance,false)?"Preset saved.":"The preset could not be saved.";
        pushPresets(); pushState(); return;
    }
    if(name.rfind("presetApply:",0)==0) {
        std::wstring preset=Bridge::widen(name.substr(12));
        if(!validPresetName(preset) || GetFileAttributesW(presetPath(preset).c_str())==INVALID_FILE_ATTRIBUTES) { toast("That preset is gone."); pushPresets(); return; }
        settings.mediaId=0;copyAppearance(settings,readSettings(presetPath(preset))); thumbnailsDirty=true; save();
        status.message="Preset applied."; pushState(); return;
    }
    if(name.rfind("presetDelete:",0)==0) {
        std::wstring preset=Bridge::widen(name.substr(13));
        if(validPresetName(preset)) DeleteFileW(presetPath(preset).c_str());
        status.message="Preset deleted."; pushPresets(); pushState(); return;
    }
    if(name=="shuffle") {
        settings.mediaId=0;
        settings.effect=std::uniform_int_distribution<int>(0,EffectCount-1)(randomEngine);
        settings.palette=std::uniform_int_distribution<int>(0,PaletteCount-1)(randomEngine);
        settings.turbulence=std::uniform_int_distribution<int>(15,85)(randomEngine);
        thumbnailsDirty=true; save(); status.message="New combination ready."; pushState(); return;
    }
    if(name=="reset") {
        settings.mediaId=0;
        copyAppearance(settings,Settings{}); thumbnailsDirty=true; save();
        status.message="Look reset. Start-up settings kept."; pushState(); return;
    }
    if(name.rfind("preset",0)==0 && name.size()==7) {
        settings.mediaId=0;applyPreset(settings,name[6]-'0'); thumbnailsDirty=true; save();
        status.message="Preset applied."; pushState(); return;
    }
    if(name=="clearPassword") {
        if(!settings.hasPassword()) { toast("No password is set."); return; }
        if(settings.securityError || GetTickCount64()>=securityGrantUntil) { toast("Verify the current password first."); return; }
        Settings next=settings; Lock::wipe(next.credential); next.lockEnabled=false;
        if(!writeSettings(configPath,next)) { toast("Password removal could not be saved."); return; }
        Lock::wipe(settings.credential); settings=std::move(next); securityGrantUntil=0;
        status.message="Password removed. The lock is off."; pushState(); return;
    }
    if(name=="testLock") {
        if(!settings.hasPassword()) { toast("Set a password first."); return; }
        startScreens(); if(!screens.empty()) engageLock(); return;
    }
    // While the page is capturing a shortcut, Windows must not swallow the combination.
    if(name=="captureStart") {
        if(mainWindow) { UnregisterHotKey(mainWindow,1); UnregisterHotKey(mainWindow,2); }
        return;
    }
    if(name=="captureEnd") { applyHotkeys(); return; }
    // Same deferral as the media picker: a modal dialog must not open inside the
    // WebView2 message callback.
    if(name=="cursorFile") {
        if(!PostMessageW(mainWindow,CursorDialogMessage,0,0)) toast("The file picker could not be opened.");
        return;
    }
    if(name=="cursorClear") {
        if(ownCursorFile(settings.cursorFile)) DeleteFileW(settings.cursorFile.c_str());
        settings.cursorFile.clear();
        if(settings.cursorStyle==2) settings.cursorStyle=0;
        save(); refreshCursor(); status.message="Cursor file removed."; pushState(); return;
    }
    // Re-scan the media folder for files added or removed outside the app.
    if(name=="refreshLibrary") {
        mediaLibrary.open(std::filesystem::path(configPath).parent_path()/L"Media");
        if(settings.mediaId && !mediaLibrary.find(settings.mediaId).id) { settings.mediaId=0; save(); }
        pushLibrary(); pushState(); toast("Library refreshed."); return;
    }
    if(name=="checkUpdate") { beginUpdateCheck(); return; }
    if(name=="openUpdate") {
        if(Update::safeReleaseUrl(pendingUpdateUrl,Bridge::widen(APP_REPO_OWNER),Bridge::widen(APP_REPO_NAME)))
            ShellExecuteW(nullptr,L"open",pendingUpdateUrl.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        return;
    }
    // Start with Windows: the registry is the truth, the stored flag is the remembered intent.
    if(name.rfind("autostart:",0)==0) {
        settings.autoStart=name.substr(10)=="1"; save(); applyAutostart();
        status.message=settings.autoStart?(status.autoStart?"Hibernation will start with Windows.":"Windows refused the startup entry."):"Startup entry removed.";
        pushState(); return;
    }
}
void onAppMessage(const std::string& raw) {
    if(raw.rfind("webviewError:",0)==0) { pageReady=false; releasePreviews(); cadence(); return; }
    if(locked || passwordRequired() || raw.size()>4096) return;
    Bridge::Message message=Bridge::parse(raw);
    if(message.kind=="auth") {
        std::wstring typed=Bridge::widen(message.key); Lock::wipe(message.key);
        const auto now=GetTickCount64();
        bool allowed=now>=authCooldownUntil && !settings.securityError && settings.hasPassword();
        const bool ok=allowed && Lock::verify(typed,settings.credential); Lock::wipe(typed);
        if(ok) { authAttempts=0; securityGrantUntil=now+60000; appView.post("authorized"); }
        else {
            if(allowed && ++authAttempts%5==0) authCooldownUntil=now+15000;
            appView.post(now<authCooldownUntil?"authError:Too many attempts. Wait 15 seconds.":"authError:Current password is incorrect.");
        }
        return;
    }
    if(mediaCommand(message)) return;
    if(message.kind=="thumbReady") { thumbPending=false; return; }
    if(message.kind=="previewVisible") { previewVisible=message.key=="1"; cadence(); return; }
    if(message.kind=="hover") { try { hoverScene=std::clamp(std::stoi(message.key),-1,EffectCount-1); } catch(...) { hoverScene=-1; } cadence(); return; }
    if(message.kind=="frameReady") { framePending=false; return; }
    if(message.kind=="clockPreview") { clockPreviewRequested=message.key=="1"; framePending=false; cadence(); return; }
    if(message.kind=="ready") { pageReady=true; appView.post(cursorPayload); pushState(); pushPresets(); pushLibrary(); pushThumbnails(); cadence(); return; }
    if(message.kind=="nav") {
        currentTab=message.key; cadence();
        if(message.key=="scenes" && thumbnailsDirty) pushThumbnails();
        return;
    }
    if(message.kind=="set") {
        const bool security=message.key=="lockEnabled" || message.key=="lockGraceMinutes";
        if(security) {
            if(settings.securityError || (settings.hasPassword() && GetTickCount64()>=securityGrantUntil)) { toast("Verify the current password first."); pushState(); return; }
            Settings next=settings;
            if(!Bridge::applySetting(next,message.key,message.value)) return;
            if(next.lockEnabled && !next.hasPassword()) { toast("Set a password first."); pushState(); return; }
            if(!writeSettings(configPath,next)) { toast("Security settings could not be saved."); pushState(); return; }
            settings=std::move(next); securityGrantUntil=0; pushState(); return;
        }
        if(!Bridge::applySetting(settings,message.key,message.value)) return;
        if(message.key.rfind("wake",0)==0) { resetWakeInput(); postLockTheme(); }
        static const std::set<std::string> visual={"effect","palette","speed","brightness","scale",
                                                   "turbulence","glow","grain","vignette"};
        if(visual.count(message.key)) { thumbnailsDirty=true; nextThumbnail=0; }
        if(message.key.rfind("hotkey",0)==0 || message.key=="hotkeysEnabled") { applyHotkeys(); pushState(); }
        if(message.key=="showPreview") cadence();
        if(message.key.rfind("cursor",0)==0) refreshCursor();
        if(message.key=="startShowUI" && settings.autoStart) applyAutostart();   // rewrite the Run command
        if(message.key=="debugLog") debugLogFull=false;
        settingsDirty=true; SetTimer(mainWindow,6,500,nullptr);
        if(message.key=="fps" || message.key=="quality") performance.reset(settings);
        cadence(); // Persist once after a slider drag, not dozens of INI writes per pixel.
        return;
    }
    if(message.kind=="pw") {
        std::wstring password=Bridge::widen(message.key); Lock::wipe(message.key);
        if(settings.securityError || (settings.hasPassword() && GetTickCount64()>=securityGrantUntil)) { Lock::wipe(password); toast("Verify the current password first."); return; }
        if(password.size()<4 || password.size()>6) { Lock::wipe(password); toast("Use 4 to 6 characters."); return; }
        std::wstring credential=Lock::create(password);
        Lock::wipe(password);
        if(credential.empty()) { toast("The password could not be hashed."); return; }
        Settings next=settings; next.credential=std::move(credential); next.lockEnabled=true;
        if(!writeSettings(configPath,next)) { toast("The password could not be saved. Check folder permissions."); return; }
        Lock::wipe(settings.credential); settings=std::move(next); securityGrantUntil=0;
        status.message="Password set. The lock is armed."; pushState(); return;
    }
    if(message.kind=="act") command(message.key);
}

/* ---------------- lock ---------------- */
void onLockMessage(const std::string& raw) {
    if(!locked || raw.size()>2048) return;
    Bridge::Message message=Bridge::parse(raw);
    if(message.kind=="idle" && locked) { resetWakeInput(); return; }
    if(message.kind=="rest" && locked && lockWindow) {
        LASTINPUTINFO input{sizeof(input)}; GetLastInputInfo(&input); restInput=input.dwTime;
        resetWakeInput(); wakeArmedAt=GetTickCount64()+250;
        lockResting=true; ShowWindow(lockWindow,SW_HIDE); lockView.setActive(false);
        if(!screens.empty()) SetForegroundWindow(screens.front().window);
        // The lock page hid its pointer; the scene underneath follows the Hide cursor setting.
        SetCursor(settings.hideCursor?nullptr:pointer());
        return;
    }
    if(message.kind=="ready") { postLockTheme(); lockView.post(cursorPayload); lockView.post("user:"+Bridge::narrow(userName)); lockView.focus(); return; }
    // Every submission is a real attempt. The page sends one by itself the moment the typed
    // text reaches the stored length, so wrong guesses still walk into the cooldown.
    if(message.kind!="try") return;
    ULONGLONG now=GetTickCount64();
    if(now<lockCooldownUntil) { Lock::wipe(message.key); lockView.post("cooldown:"+std::to_string(lockCooldownUntil-now)); return; }
    std::wstring typed=Bridge::widen(message.key); Lock::wipe(message.key);
    const bool verified=Lock::verify(typed,settings.credential);
    if(verified) {
        // A password saved by an older version learns its length here, so the next lock
        // can finish on its own.
        if(Lock::needsUpgrade(settings.credential)) {
            auto upgraded=Lock::create(typed); if(!upgraded.empty()) { settings.credential=std::move(upgraded); save(); }
        } else if(!Lock::passwordLength(settings.credential)) { settings.credential=Lock::withLength(settings.credential,typed.size()); save(); }
        Lock::wipe(typed); unlockVerified=true;
        PostMessageW(mainWindow,UnlockMessage,0,0); return;
    }
    Lock::wipe(typed);
    lockAttempts++;
    if(lockAttempts%5==0) { lockCooldownUntil=now+15000; lockView.post("cooldown:15000"); }
    else lockView.post("wrong:"+std::to_string(lockAttempts));
}
void postLockTheme() {
    if(!lockWindow) return;
    std::ostringstream json;
    json<<"theme:{\"palette\":"<<settings.palette<<",\"roundness\":"<<settings.roundness
        <<",\"length\":"<<Lock::passwordLength(settings.credential)
        <<",\"hideCursor\":"<<(settings.hideCursor?"true":"false")
        <<",\"nativeWake\":"<<(wakeInputRegistered?"true":"false")
        <<",\"wakeKeyboard\":"<<(!settings.wakeProtection || settings.wakeKeyboard?"true":"false")<<"}";
    lockView.post(json.str());
}
LRESULT CALLBACK keyboardHook(int code,WPARAM wp,LPARAM lp) {
    // While locked, swallow the usual ways out of a foreground window. Ctrl+Alt+Del and
    // Win+L cannot be blocked (Windows owns them); Ctrl+Shift+Esc is left free on purpose
    // as an escape hatch to the Task Manager.
    if(code==HC_ACTION && locked && (wp==WM_KEYDOWN || wp==WM_SYSKEYDOWN)) {
        auto* key=reinterpret_cast<KBDLLHOOKSTRUCT*>(lp); DWORD vk=key->vkCode;
        bool alt=(key->flags&LLKHF_ALTDOWN)!=0;
        bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
        bool shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
        if(vk==VK_LWIN || vk==VK_RWIN) return 1;
        if(vk==VK_TAB && alt) return 1;
        if(vk==VK_ESCAPE && alt) return 1;
        if(vk==VK_ESCAPE && ctrl && !shift) return 1;
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
void disengageLock() {
    if(keyHook) { UnhookWindowsHookEx(keyHook); keyHook=nullptr; }
    lockView.destroy();
    if(lockWindow) { KillTimer(lockWindow,4); DestroyWindow(lockWindow); lockWindow=nullptr; }
    locked=false; unlockVerified=false; lockResting=false; lockAttempts=0; lockCooldownUntil=0;
}
void engageLock() {
    if(locked || screens.empty() || closing || smoke) return;
    resetWakeInput();
    locked=true; unlockVerified=false; lockAttempts=0; lockCooldownUntil=0; lockLoadingSince=GetTickCount64();
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromWindow(screens.front().window,MONITOR_DEFAULTTOPRIMARY),&info);
    RECT r=info.rcMonitor;
    lockWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"HibernationLock",L"",WS_POPUP,
        r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    std::wstring error;
    lockView.setMessageHandler(onLockMessage);
    lockView.setReadyHandler([]{
        if(!locked || !lockWindow) return;
        postLockTheme();
        lockView.post(cursorPayload);
        lockView.post("user:"+Bridge::narrow(userName));
        ShowWindow(lockWindow,SW_SHOW); SetForegroundWindow(lockWindow); lockView.focus();
        lockView.post("reveal");
    });
    // Fall back to Windows sign-in if the visual password UI cannot be created.
    if(!lockWindow || !lockView.create(lockWindow,loadHtmlResource(IDR_LOCK_HTML),error)) {
        stopScreensSafely(); return;
    }
    keyHook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboardHook,instance,0);
    SetWindowPos(lockWindow,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    SetTimer(lockWindow,4,400,nullptr);
}
LRESULT CALLBACK lockProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_TIMER:
        if(!lockView.ready() && GetTickCount64()-lockLoadingSince>15000) { stopScreensSafely(); return 0; }
        if(lockResting) return 0;
        // Stay above the fullscreen surfaces without stealing focus back from Task Manager.
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        lockView.resize(); return 0;
    case WM_SIZE: lockView.resize(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: return 0;                      // ignore Alt+F4 while locked
    }
    return DefWindowProcW(window,message,wp,lp);
}

/* ---------------- fullscreen surfaces ---------------- */
LRESULT CALLBACK surfaceProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_SETCURSOR && window!=previewWindow) { SetCursor(settings.hideCursor?nullptr:pointer()); return TRUE; }
    const bool keyboardWakes=!settings.wakeProtection || settings.wakeKeyboard;
    if(message==WM_KEYDOWN && wp==VK_ESCAPE && keyboardWakes) { requestReturn(); return 0; }
    if(message==WM_CLOSE) { if(keyboardWakes) requestReturn(); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}

/* ---------------- tray panel ---------------- */
// Right-clicking the tray icon opens a small HTML panel instead of a system menu,
// so the whole app speaks one visual language.
constexpr int TrayPanelWidth=300,TrayPanelHeight=356;
void hideTrayPanel() { if(trayPanel && IsWindowVisible(trayPanel)) ShowWindow(trayPanel,SW_HIDE); trayView.setActive(false); }
void showTrayPanel() {
    if(locked || passwordRequired() || !trayPanel) return;
    POINT p{}; GetCursorPos(&p);
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST),&mi);
    int w=px(static_cast<float>(TrayPanelWidth)),h=px(static_cast<float>(TrayPanelHeight));
    int x=std::clamp<int>(p.x-w/2,mi.rcWork.left+8,std::max<int>(mi.rcWork.left+8,mi.rcWork.right-w-8));
    // Near the taskbar the panel opens upwards, the way a tray menu does.
    int y=(p.y>mi.rcWork.bottom-h-16)?mi.rcWork.bottom-h-8:p.y+8;
    y=std::clamp<int>(y,mi.rcWork.top+8,std::max<int>(mi.rcWork.top+8,mi.rcWork.bottom-h-8));
    SetWindowPos(trayPanel,HWND_TOPMOST,x,y,w,h,SWP_SHOWWINDOW);
    trayView.setActive(true); trayView.post(Bridge::toJson(settings,status)); trayView.post(cursorPayload);
    SetForegroundWindow(trayPanel); trayView.focus();
}
void onTrayMessage(const std::string& raw) {
    if(locked || passwordRequired()) return;
    Bridge::Message message=Bridge::parse(raw);
    if(message.kind=="ready") { trayView.post(Bridge::toJson(settings,status)); trayView.post(cursorPayload); return; }
    if(message.kind!="act") return;
    hideTrayPanel();
    if(message.key=="open") showSettings();
    else if(message.key=="start") startScreens();
    else if(message.key=="toggle") { settings.enabled=!settings.enabled; save(); pushState(); }
    else if(message.key=="quit") DestroyWindow(mainWindow);
}
LRESULT CALLBACK trayProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE) hideTrayPanel(); return 0;   // click away to dismiss
    case WM_SIZE: trayView.resize(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: hideTrayPanel(); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
// Writes one frame per clock style next to the exe, so the clock can be checked without
// screen-capturing a fullscreen swap chain (GDI capture of one comes back black).
bool renderClockFrames() {
    if(!preview.resize(960,540)) return false;
    CLSID png{}; if(!pngEncoder(png)) return false;
    Settings view=settings;
    view.clock=true; view.clockPosition=1; view.clockSize=150; view.clockOpacity=100; view.clockSeconds=true;
    for(int style=0;style<3;style++) {
        view.clockStyle=style;
        if(!preview.draw(8.f,view,1.f,false)) return false;
        std::vector<unsigned> pixels; UINT w=0,h=0;
        if(!preview.readPixels(pixels,w,h)) return false;
        Gdiplus::Bitmap bitmap(w,h,w*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(pixels.data()));
        std::wstring path=(executableDir/(L"clock-"+std::to_wstring(style)+L".png")).wstring();
        if(bitmap.Save(path.c_str(),&png)!=Gdiplus::Ok) return false;
    }
    return true;
}
bool checkRenderMatrix() {
    bool ok=true; Settings test=settings; test.clock=false; std::set<uint64_t> hashes;
    if(!preview.resize(311,177)) return false;
    for(int effect=0;effect<EffectCount;effect++) for(int palette=0;palette<PaletteCount;palette++) {
        test.effect=effect; test.palette=palette;
        if(!preview.draw(10.f,test,1,false)) return false;
        std::vector<unsigned> pixels; UINT w=0,h=0;
        if(!preview.readPixels(pixels,w,h)||pixels.empty()) return false;
        unsigned low=0xffffff,high=0; uint64_t hash=1469598103934665603ull;
        for(size_t i=0;i<pixels.size();i+=31) { auto c=pixels[i]&0xffffff; low=std::min(low,c); high=std::max(high,c); hash=(hash^c)*1099511628211ull; }
        hashes.insert(hash); ok=ok&&(low!=high);
    }
    test.clock=true;
    for(int style=0;style<3;style++) { test.clockStyle=style; ok=preview.draw(3.f,test,1,false)&&ok; }
    UINT w=0,h=0; std::vector<unsigned> pixels; ok=preview.readPixels(pixels,w,h)&&ok;
    ok=ok&&w==311&&h==177;
    test.clock=false;
    for(int effect=0;effect<EffectCount;++effect) {
        test.effect=effect; std::vector<unsigned> first,secondFrame;
        if(!preview.draw(1.f,test,1,false) || !preview.readPixels(first,w,h) ||
           !preview.draw(6.f,test,1,false) || !preview.readPixels(secondFrame,w,h) || first==secondFrame) return false;
    }
    return ok&&hashes.size()==EffectCount*PaletteCount;
}
LRESULT CALLBACK mainProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(taskbarCreated && message==taskbarCreated) { addTray(); return 0; }
    if(message==WM_APP+4) { cadence();return 0; }
    if(message==MediaDialogMessage) { completePendingMediaImport(); return 0; }
    if(message==CursorDialogMessage) { chooseCursor(); return 0; }
    if(message==UpdateResultMessage) { deliverUpdateResult(); return 0; }
    if(message==MediaFailure) { settings.enabled=false; settings.mediaId=0; save(); if(!stopScreensSafely()) return 0; showSettings(); pushState(); toast("This file cannot be decoded. Automatic start is paused. Try H.264 MP4, WebM, GIF or WebP."); return 0; }
    if(message==UnlockMessage) {
        if(!locked || !unlockVerified) return 0;
        bool visible=IsWindowVisible(window)&&!IsIconic(window);
        stopScreens();
        // Hand the desktop back for a while. Without this the screensaver can restart
        // seconds later and ask for the password again, over and over.
        lastUnlockAt=GetTickCount64(); cooldownUntil=lastUnlockAt+15000;
        if(visible) { SetForegroundWindow(window); appView.focus(); }
        return 0;
    }
    switch(message) {
    case WM_NCCALCSIZE: return 0;
    case WM_NCLBUTTONDBLCLK: return 0;
    case WM_SYSCOMMAND:
        if((wp&0xfff0)==SC_SIZE || (wp&0xfff0)==SC_MAXIMIZE) return 0;
        break;
    case WM_SIZE: appView.resize(); cadence(); return 0;
    case WM_SHOWWINDOW: PostMessageW(window,WM_APP+4,0,0);break;
    case WM_SETFOCUS: appView.focus(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_INPUT: recordWakeInput(reinterpret_cast<HRAWINPUT>(lp)); break;
    case WM_INPUT_DEVICE_CHANGE:
        if(wp==GIDC_REMOVAL) mouseWakeFilters.erase(reinterpret_cast<HANDLE>(lp));
        return 0;
    case WM_GETMINMAXINFO: {
        // The layout is designed for one size; pin the window so it cannot be resized.
        auto* m=reinterpret_cast<MINMAXINFO*>(lp);
        POINT fixed{px(1120),px(752)};
        m->ptMinTrackSize=fixed; m->ptMaxTrackSize=fixed; return 0;
    }
    case WM_TIMER: {
        if(wp==6) { KillTimer(window,6); settingsDirty=false; save(); return 0; }
        if(wp==5) {   // eased window drag
            if(!dragging || !(GetAsyncKeyState(VK_LBUTTON)&0x8000)) { dragging=false; KillTimer(window,5); return 0; }
            POINT p{}; GetCursorPos(&p);
            dragTarget={p.x-dragGrab.x,p.y-dragGrab.y};
            if(dragEase) {
                dragCurrent.x+=static_cast<LONG>((dragTarget.x-dragCurrent.x)*0.35f);
                dragCurrent.y+=static_cast<LONG>((dragTarget.y-dragCurrent.y)*0.35f);
            } else dragCurrent=dragTarget;
            SetWindowPos(window,nullptr,dragCurrent.x,dragCurrent.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            return 0;
        }
        ULONGLONG now=GetTickCount64(); float dt=std::min((now-lastFrame)/1000.f,.25f); lastFrame=now;
        if(!settings.debugEnabled || !settings.debugFreeze) animationTime+=dt*(settings.speed/45.f);
        if(launchPending && !(GetAsyncKeyState(VK_CONTROL)&0x8000) && !(GetAsyncKeyState(VK_MENU)&0x8000) &&
           !(GetAsyncKeyState(VK_SHIFT)&0x8000) && !(GetAsyncKeyState(VK_LWIN)&0x8000) && !(GetAsyncKeyState(VK_RWIN)&0x8000) &&
           !(GetAsyncKeyState(settings.hotkeyToggleKey)&0x8000)) { launchPending=false; startScreens(); now=GetTickCount64(); }
        LASTINPUTINFO input{sizeof(input)}; bool inputOK=GetLastInputInfo(&input)!=FALSE;
        if(!screens.empty()) {
            const bool fallbackWake=!wakeInputRegistered && inputOK && input.dwTime!=(locked&&lockResting?restInput:activationInput);
            const bool wake=wakeInputRegistered?consumeWakeInput(now):fallbackWake;
            if(locked && lockResting && wake) {
                lockResting=false; lockView.setActive(true); ShowWindow(lockWindow,SW_SHOW); SetForegroundWindow(lockWindow);
                lockView.focus(); postLockTheme(); lockView.post("reveal");
            } else if(locked && wake && wakeInputRegistered) lockView.post("wake");
            if(wake && !locked) { if(shouldLock(now-activatedAt,now-lastUnlockAt,settings)) engageLock(); else stopScreens(); }
            if(!screens.empty()) {
                if(!mediaActive() && settings.playlist && now-lastScene>=static_cast<ULONGLONG>(settings.cycleSeconds)*1000) {
                    settings.effect=(settings.effect+1)%EffectCount; settings.palette=(settings.palette+1)%PaletteCount;
                    lastScene=now; thumbnailsDirty=true;
                }
                float fade=settings.fadeSeconds==0?1.f:std::min((now-activatedAt)/(settings.fadeSeconds*1000.f),1.f);
                bool ok=true;
                double before=preciseSeconds();
                if(!mediaActive() && before>=lastPresented) {
                    lastPresented=std::max(before,lastPresented+1.0/std::max(5,performance.fps)); double gpu=-1;
                    for(auto& screen:screens) { ok=screen.renderer->draw(animationTime,forMonitor(settings,screen.primary),fade)&&ok; if(screen.renderer->gpuMs()>=0) gpu=std::max(0.0,gpu)+screen.renderer->gpuMs(); }
                    diagnostics.frame((preciseSeconds()-before)*1000,gpu,performance.fps);
                }
                if(smoke) fullscreenFrames++;
                if(!ok) { smokeFailed=true; stopScreensSafely(); settings.enabled=false; save(); showSettings(); toast("Direct3D failed. Automatic start is paused."); }
            }
        } else if(IsWindowVisible(window) && !IsIconic(window) && GetAncestor(GetForegroundWindow(),GA_ROOT)==window) {
            if(pageReady && currentTab=="scenes" && (now-lastGalleryFrame>=static_cast<ULONGLONG>(1000/settings.previewFps))) {
                lastGalleryFrame=now; pushThumbnails();
            }
            if(now-lastPreviewFrame>=static_cast<ULONGLONG>(1000/settings.previewFps)) { lastPreviewFrame=now; pushPreviewFrame(); }
        } else if(inputOK && !selectingMedia && !sessionLocked && !smoke && now>=cooldownUntil && !blockedByEnvironment() &&
                  shouldActivateInContext(GetTickCount(),input.dwTime,settings,currentActivity(mediaMonitor,settings,window),false,false)) startScreens();
        updateDiagnostics(now);
        static ULONGLONG lastContext=0;
        if(now-lastContext>2000 && screens.empty() && IsWindowVisible(window)) {
            lastContext=now; Activity context=currentActivity(mediaMonitor,settings,window);
            std::string name=context==Activity::Video?"Video":context==Activity::Game?"Game":"Desktop";
            if(status.context!=name || status.mediaAvailable!=mediaMonitor.available()) {
                status.context=name; status.mediaAvailable=mediaMonitor.available(); pushState();
            }
        }
        if(smoke) {
            if(smokeStage==0 && now-startedAt>400) { smokeStage=1; startScreens(); return 0; }
            if(smokeStage==1 && now-activatedAt>1100) { smokeStage=2; stopScreens(); }
            if(smokeStage==2 && now-activatedAt>1600) {
                smokeFailed=smokeFailed || fullscreenFrames==0 || smokeMonitors==0 || !smokeNativeResolution;
                std::ofstream report(executableDir/L"smoke-report.txt");
                report<<"Render matrix: "<<(smokeRenderOK?"PASS":"FAIL")<<" ("<<EffectCount*PaletteCount
                      <<" distinct frames; 12 animated scenes; 3 clocks and resize)\nMonitors: "<<smokeMonitors
                      <<"\nFullscreen frames: "<<fullscreenFrames
                      <<"\nNative fullscreen resolution: "<<(smokeNativeResolution?"PASS":"FAIL")
                      <<"\nResult: "<<(smokeFailed?"FAIL":"PASS")<<"\n";
                DestroyWindow(window);
            }
        }
        return 0;
    }
    case WM_HOTKEY:
        if(locked) return 0;
        if(wp==1) { if(!screens.empty()) requestReturn(); else launchPending=true; }
        if(wp==2) { settings.enabled=!settings.enabled; save(); pushState(); }
        return 0;
    case WM_DPICHANGED: {
        auto* r=reinterpret_cast<RECT*>(lp);
        dpiScale=HIWORD(wp)/96.f;
        SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        appView.resize(); refreshCursor(); return 0;
    }
    case WM_DISPLAYCHANGE: stopScreensSafely(); pushState(); return 0;
    case WM_WTSSESSION_CHANGE:
        if(wp==WTS_SESSION_LOCK || wp==WTS_REMOTE_DISCONNECT || wp==WTS_CONSOLE_DISCONNECT) { sessionLocked=true; launchPending=false; stopScreens(); }
        if(wp==WTS_SESSION_UNLOCK || wp==WTS_SESSION_LOGON || wp==WTS_REMOTE_CONNECT || wp==WTS_CONSOLE_CONNECT) { sessionLocked=false; cooldownUntil=GetTickCount64()+5000; }
        return 0;
    case WM_POWERBROADCAST:
        if(wp==PBT_APMSUSPEND) { launchPending=false; stopScreensSafely(); }
        if(wp==PBT_APMRESUMEAUTOMATIC) cooldownUntil=GetTickCount64()+5000;
        if(wp==PBT_APMPOWERSTATUSCHANGE && settings.pauseOnBattery) { SYSTEM_POWER_STATUS power{}; if(GetSystemPowerStatus(&power)&&power.ACLineStatus==0) stopScreensSafely(); }
        return TRUE;
    case TrayMessage:
        if(locked) return 0;
        if(lp==WM_LBUTTONUP) { hideTrayPanel(); showSettings(); }
        if(lp==WM_RBUTTONUP) showTrayPanel();
        return 0;
    case WM_CLOSE: if(uiTest) DestroyWindow(window); else hideSettings(); return 0;
    case WM_DESTROY: {
        { std::lock_guard guard(updateState->mutex); updateState->closed=true; updateState->result.reset(); }
        if(settingsDirty && !smoke) writeSettings(configPath,settings);
        closing=true; KillTimer(window,1);KillTimer(window,6); stopScreens(); appView.destroy();
        trayView.destroy(); if(trayPanel) { DestroyWindow(trayPanel); trayPanel=nullptr; }
        if(appCursor) { SetCursor(LoadCursorW(nullptr,IDC_ARROW)); DestroyCursor(appCursor); appCursor=nullptr; }
        WTSUnRegisterSessionNotification(window); UnregisterHotKey(window,1); UnregisterHotKey(window,2);
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=window; data.uID=1; Shell_NotifyIconW(NIM_DELETE,&data);
        PostQuitMessage(smokeFailed?1:0); return 0;
    }
    }
    return DefWindowProcW(window,message,wp,lp);
}
HICON createIcon() {
    Gdiplus::Bitmap bitmap(64,64,PixelFormat32bppARGB); Gdiplus::Graphics g(&bitmap); g.Clear(Gdiplus::Color(255,20,31,30));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::Font font(L"Segoe UI",55,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255,151,243,206));
    g.DrawString(L"h",-1,&font,Gdiplus::PointF(12,-8),&brush); HICON icon{}; bitmap.GetHICON(&icon); return icon;
}
}

int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR command,int) {
    if(auto guard=wcsstr(command,L"--audio-guard ")) return audioGuardianMain(static_cast<DWORD>(wcstoul(guard+wcslen(L"--audio-guard "),nullptr,10)));
    if(wcsstr(command,L"--audio-probe")) return audioProbe();
    if(wcsstr(command,L"--audio-self-test")) return audioSelfTest();
    if(wcsstr(command,L"--audio-owner-test")) return audioOwnerTest();
    if(wcsstr(command,L"--media-probe")) return mediaProbe();
    instance=app; smoke=wcsstr(command,L"--smoke-test")!=nullptr;
    bool clockShots=wcsstr(command,L"--render-clocks")!=nullptr;
    uiTest=wcsstr(command,L"--ui-test")!=nullptr;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768); executableDir=std::filesystem::path(executable).parent_path();
    int argc=0;
    if(LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc)) {
        for(int i=1;i+1<argc;i++) if(!wcscmp(argv[i],L"--data-dir") && argv[i+1][0]) {
            std::error_code error;
            dataFolder=std::filesystem::absolute(argv[i+1],error).wstring();
            webProfile=dataFolder+L"\\WebView2";
        }
        LocalFree(argv);
    }
    appView.setProfileFolder(webProfile); trayView.setProfileFolder(webProfile); lockView.setProfileFolder(webProfile);
    const wchar_t* instanceName=(smoke||clockShots||uiTest)?L"Local\\Hibernation.Tests.2":
                                !dataFolder.empty()?L"Local\\Hibernation.DataDir.1":L"Local\\Hibernation.Liquid.1";
    HANDLE mutex=CreateMutexW(nullptr,FALSE,instanceName);
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(L"HibernationSettings",nullptr);
        if(existing && !smoke && !clockShots && !uiTest && dataFolder.empty()) { ShowWindow(existing,SW_RESTORE); SetForegroundWindow(existing); }
        CloseHandle(mutex); return (smoke||clockShots||uiTest)?2:0;
    }
    // WebView2 needs a single-threaded apartment on the UI thread.
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdiplusInput; ULONG_PTR token{};
    if(Gdiplus::GdiplusStartup(&token,&gdiplusInput,nullptr)!=Gdiplus::Ok) { CloseHandle(mutex); return 1; }
    load(); if(smoke && wcsstr(command,L"--software")) settings.softwareRenderer=true; performance.reset(settings);
    status.version=APP_VERSION_STRING;
    // Registry is the truth for startup; make the stored flag match what is actually there.
    if(!smoke) { status.autoStart=autostartActive(); settings.autoStart=status.autoStart; }
    // Prefer the multi-size icon compiled into the exe; fall back to the drawn one.
    appIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_APPICON),IMAGE_ICON,0,0,LR_DEFAULTSIZE));
    if(!appIcon) { appIcon=createIcon(); ownIcon=true; }
    { wchar_t name[UNLEN+1]{}; DWORD size=UNLEN+1; userName=(GetUserNameW(name,&size)&&name[0])?name:L""; }
    if(!smoke) mediaMonitor.start();

    POINT cursor{}; GetCursorPos(&cursor);
    HMONITOR monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY);
    dpiScale=GetDpiForSystem()/96.f;
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info);
    if(!smoke && !clockShots) refreshCursor();

    WNDCLASSW wc{}; wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=appIcon;
    wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName=L"HibernationSettings"; wc.lpfnWndProc=mainProc; RegisterClassW(&wc);
    wc.lpszClassName=L"HibernationSurface"; wc.lpfnWndProc=surfaceProc; RegisterClassW(&wc);
    wc.lpszClassName=L"HibernationLock"; wc.lpfnWndProc=lockProc; RegisterClassW(&wc);
    wc.lpszClassName=L"HibernationTray"; wc.lpfnWndProc=trayProc; RegisterClassW(&wc);
    wc.lpszClassName=L"HibernationDebug"; wc.lpfnWndProc=debugProc; RegisterClassW(&wc);

    int width=px(1120),height=px(752);
    int x=info.rcWork.left+((info.rcWork.right-info.rcWork.left)-width)/2;
    int y=info.rcWork.top+((info.rcWork.bottom-info.rcWork.top)-height)/2;
    mainWindow=CreateWindowExW(WS_EX_APPWINDOW,L"HibernationSettings",uiTest?L"Hibernation UI Test":L"Hibernation",
        WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,x,y,width,height,nullptr,nullptr,instance,nullptr);
    int exitCode=1;
    if(mainWindow) {
        int corner=2; DwmSetWindowAttribute(mainWindow,33,&corner,sizeof(corner));
        // A hidden child window gives the preview renderer a swap chain for scene thumbnails.
        previewWindow=CreateWindowExW(0,L"HibernationSurface",L"preview",WS_CHILD,0,0,ThumbWidth,ThumbHeight,mainWindow,nullptr,instance,nullptr);
        std::wstring error;
        galleryWindow=CreateWindowExW(0,L"HibernationSurface",L"gallery",WS_CHILD,0,0,ThumbWidth,ThumbHeight,mainWindow,nullptr,instance,nullptr);
        if(previewWindow && galleryWindow && ensurePreviews()) {
            if(clockShots) { exitCode=renderClockFrames()?0:1; DestroyWindow(mainWindow); mainWindow=nullptr; }
            else if(smoke) { smokeRenderOK=checkRenderMatrix(); smokeFailed=!smokeRenderOK; }
            else {
                appView.setMediaFolder(mediaLibrary.folder().wstring());
                appView.setMessageHandler(onAppMessage);
                appView.setReadyHandler([]{ pageReady=true; appView.focus(); pushState(); pushLibrary(); pushThumbnails(); cadence(); });
                std::wstring viewError;
                if(!appView.create(mainWindow,loadHtmlResource(IDR_APP_HTML),viewError)) {
                    MessageBoxW(nullptr,viewError.c_str(),L"Hibernation",MB_ICONERROR);
                    std::ofstream(executableDir/L"startup-error.txt")<<Bridge::narrow(viewError);
                    DestroyWindow(mainWindow); mainWindow=nullptr;
                }
                // Built up front and kept hidden, so the first right-click opens instantly.
                if(mainWindow) {
                    trayPanel=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"HibernationTray",L"",WS_POPUP,
                        0,0,px(static_cast<float>(TrayPanelWidth)),px(static_cast<float>(TrayPanelHeight)),
                        nullptr,nullptr,instance,nullptr);
                    if(trayPanel) {
                        int trayCorner=2; DwmSetWindowAttribute(trayPanel,33,&trayCorner,sizeof(trayCorner));
                        trayView.setMessageHandler(onTrayMessage);
                        trayView.setActive(false);
                        std::wstring trayError;
                        trayView.create(trayPanel,loadHtmlResource(IDR_TRAY_HTML),trayError);
                    }
                }
            }
            if(mainWindow) {
                taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated"); addTray();
                WTSRegisterSessionNotification(mainWindow,NOTIFY_FOR_THIS_SESSION);
                applyHotkeys();
                startedAt=lastFrame=GetTickCount64();
                ShowWindow(mainWindow,(wcsstr(command,L"--tray")&&trayAdded)||smoke?SW_HIDE:SW_SHOW);
                cadence();
                MSG msg{}; BOOL result;
                while((result=GetMessageW(&msg,nullptr,0,0))>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                exitCode=result<0?1:static_cast<int>(msg.wParam);
            }
        } else {
            std::ofstream(executableDir/L"startup-error.txt")<<Bridge::narrow(error);
            if(!smoke) MessageBoxW(nullptr,error.c_str(),L"Hibernation",MB_ICONERROR);
            DestroyWindow(mainWindow);
        }
    }
    audioGuard.shutdown(); mediaMonitor.stop();
    if(appIcon && ownIcon) DestroyIcon(appIcon);
    Gdiplus::GdiplusShutdown(token);
    if(SUCCEEDED(com)) CoUninitialize();
    CloseHandle(mutex); return exitCode;
}
