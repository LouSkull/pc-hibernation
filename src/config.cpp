#include "config.h"
#include "lock.h"
#include <windows.h>
#include <sddl.h>
#include <filesystem>
namespace {
struct IntField { const wchar_t* key; int Settings::*value; };
constexpr IntField ints[]={
    {L"PreviewFps",&Settings::previewFps},
    {L"PreviewWidth",&Settings::previewWidth},
    {L"MinFps",&Settings::minFps},
    {L"MinQuality",&Settings::minQuality},
    {L"RestAfter",&Settings::restAfter},
    {L"GpuBudget",&Settings::gpuBudget},
    {L"BackgroundPoll",&Settings::backgroundPoll},
    {L"MaxRenderHeight",&Settings::maxRenderHeight},
    {L"MediaId",&Settings::mediaId},
    {L"MediaFit",&Settings::mediaFit},
    {L"WakeMouseThreshold",&Settings::wakeMouseThreshold},
    {L"Delay",&Settings::delay},{L"Speed",&Settings::speed},{L"Brightness",&Settings::brightness},
    {L"Palette",&Settings::palette},{L"Effect",&Settings::effect},{L"Scale",&Settings::scale},
    {L"Turbulence",&Settings::turbulence},{L"Glow",&Settings::glow},{L"FPS",&Settings::fps},
    {L"Quality",&Settings::quality},{L"CycleSeconds",&Settings::cycleSeconds},{L"FadeSeconds",&Settings::fadeSeconds},
    {L"GameDelay",&Settings::gameDelay},{L"VideoDelay",&Settings::videoDelay},{L"ClockStyle",&Settings::clockStyle},
    {L"ClockSize",&Settings::clockSize},{L"ClockOpacity",&Settings::clockOpacity},{L"ClockPosition",&Settings::clockPosition},
    {L"Accent",&Settings::accent},{L"Tone",&Settings::tone},{L"Roundness",&Settings::roundness},
    {L"LockGraceMinutes",&Settings::lockGraceMinutes},
    {L"ClockWeight",&Settings::clockWeight},{L"ClockColor",&Settings::clockColor},
    {L"HotkeyToggleKey",&Settings::hotkeyToggleKey},{L"HotkeyToggleMods",&Settings::hotkeyToggleMods},
    {L"HotkeyPauseKey",&Settings::hotkeyPauseKey},{L"HotkeyPauseMods",&Settings::hotkeyPauseMods},
    {L"CursorStyle",&Settings::cursorStyle},{L"CursorColor",&Settings::cursorColor},
    {L"CursorSize",&Settings::cursorSize},{L"CursorTip",&Settings::cursorTip}};
struct BoolField { const wchar_t* key; bool Settings::*value; };
constexpr BoolField bools[]={
    {L"MediaRest",&Settings::mediaRest},
    {L"Adaptive",&Settings::adaptive},
    {L"DeepRest",&Settings::deepRest},
    {L"EcoPriority",&Settings::ecoPriority},
    {L"AnimateThumbs",&Settings::animateThumbs},
    {L"DebugEnabled",&Settings::debugEnabled},
    {L"DebugOverlay",&Settings::debugOverlay},
    {L"DebugLog",&Settings::debugLog},
    {L"DebugFreeze",&Settings::debugFreeze},
    {L"SoftwareRenderer",&Settings::softwareRenderer},
    {L"WakeProtection",&Settings::wakeProtection},
    {L"WakeKeyboard",&Settings::wakeKeyboard},
    {L"Enabled",&Settings::enabled},{L"Grain",&Settings::grain},{L"Vignette",&Settings::vignette},
    {L"Clock",&Settings::clock},{L"AllMonitors",&Settings::allMonitors},{L"PauseOnBattery",&Settings::pauseOnBattery},
    {L"FullscreenGuard",&Settings::fullscreenGuard},{L"Playlist",&Settings::playlist},
    {L"GameAllowed",&Settings::gameAllowed},{L"VideoAllowed",&Settings::videoAllowed},
    {L"ClockPrimaryOnly",&Settings::clockPrimaryOnly},{L"MuteAudio",&Settings::muteAudio},
    {L"HideCursor",&Settings::hideCursor},{L"MediaConservative",&Settings::mediaConservative},
    {L"Lock",&Settings::lockEnabled},
    {L"ClockFormat24",&Settings::clockFormat24},{L"ClockSeconds",&Settings::clockSeconds},
    {L"HotkeysEnabled",&Settings::hotkeysEnabled},{L"ShowPreview",&Settings::showPreview},
    {L"CursorTrail",&Settings::cursorTrail},
    {L"AutoStart",&Settings::autoStart},{L"StartShowUI",&Settings::startShowUI}};
}
Settings readSettings(const std::wstring& path,const Settings& defaults,bool includeSecurity) {
    Settings result=defaults;
    for(const auto& f:ints) if(includeSecurity || f.value!=&Settings::lockGraceMinutes)
        result.*(f.value)=static_cast<int>(GetPrivateProfileIntW(L"Settings",f.key,result.*(f.value),path.c_str()));
    for(const auto& f:bools) if(includeSecurity || f.value!=&Settings::lockEnabled)
        result.*(f.value)=GetPrivateProfileIntW(L"Settings",f.key,result.*(f.value)?1:0,path.c_str())!=0;
    // Preserve the old fullscreen preference when upgrading v1/v2 settings.
    wchar_t value[16]{}; GetPrivateProfileStringW(L"Settings",L"GameAllowed",L"",value,16,path.c_str());
    if(!value[0]) result.gameAllowed=!result.fullscreenGuard;
    if(includeSecurity) {
        wchar_t sealed[4096]{},format[16]{};
        GetPrivateProfileStringW(L"Security",L"Format",L"",format,16,path.c_str());
        GetPrivateProfileStringW(L"Security",L"ProtectedState",L"",sealed,4096,path.c_str());
        if(format[0] || sealed[0]) {
            std::wstring state;
            bool valid=!wcscmp(format,L"1") && Lock::unprotect(sealed,state) && state.rfind(L"HSEC1|",0)==0;
            const size_t cut=state.find(L'|',8);
            valid=valid && state.size()>9 && (state[6]==L'0' || state[6]==L'1') && state[7]==L'|' && cut!=std::wstring::npos;
            if(valid) {
                const auto grace=state.substr(8,cut-8);
                valid=!grace.empty() && grace.size()<=2 && std::all_of(grace.begin(),grace.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});
                if(valid) {
                    result.lockGraceMinutes=_wtoi(grace.c_str()); result.lockEnabled=state[6]==L'1';
                    result.credential=state.substr(cut+1);
                    valid=result.lockGraceMinutes<=30 && (result.credential.empty()? !result.lockEnabled : Lock::validCredential(result.credential));
                }
            }
            Lock::wipe(state); result.securityError=!valid;
        } else {
            wchar_t credential[512]{}; GetPrivateProfileStringW(L"Security",L"Credential",L"",credential,512,path.c_str());
            result.credential=credential; SecureZeroMemory(credential,sizeof(credential));
            result.securityError=(!result.credential.empty() && !Lock::validCredential(result.credential)) || (result.lockEnabled && result.credential.empty());
        }
        if(result.securityError) { Lock::wipe(result.credential); result.lockEnabled=true; result.enabled=false; }
    }
    wchar_t cursor[MAX_PATH*2]{}; GetPrivateProfileStringW(L"Settings",L"CursorFile",result.cursorFile.c_str(),cursor,MAX_PATH*2,path.c_str());
    result.cursorFile=cursor;
    result.sanitize(); return result;
}
bool writeSettings(const std::wstring& path,const Settings& settings,bool includeSecurity) {
    if(includeSecurity && (settings.securityError || (!settings.credential.empty() && !Lock::validCredential(settings.credential)))) return false;
    Settings s=settings; s.sanitize();
    if(s.cursorFile.find_first_of(L"\r\n")!=std::wstring::npos || s.cursorFile.find(L'\0')!=std::wstring::npos) return false;
    std::wstring text=L"\xfeff[Settings]\r\n";
    for(const auto& f:ints) if(f.value!=&Settings::lockGraceMinutes) text+=std::wstring(f.key)+L"="+std::to_wstring(s.*(f.value))+L"\r\n";
    for(const auto& f:bools) if(f.value!=&Settings::lockEnabled) text+=std::wstring(f.key)+L"="+(s.*(f.value)?L"1":L"0")+L"\r\n";
    text+=L"CursorFile="+s.cursorFile+L"\r\n";
    if(includeSecurity) {
        std::wstring state=L"HSEC1|"+std::wstring(s.lockEnabled && s.hasPassword()?L"1":L"0")+L"|"+std::to_wstring(s.lockGraceMinutes)+L"|"+s.credential;
        const std::wstring sealed=Lock::protect(state); Lock::wipe(state);
        if(sealed.empty()) return false;
        text+=L"[Security]\r\nFormat=1\r\nProtectedState="+sealed+L"\r\n";
    }
    // Write a complete UTF-16 file beside the target, then atomically replace it.
    // Security data and policy cannot be torn apart by an interrupted slider save.
    std::vector<uint8_t> nonce; if(!Lock::randomBytes(nonce,16)) return false;
    std::wstring temp=path+L".";
    for(auto b:nonce) { temp+=L"0123456789abcdef"[b>>4]; temp+=L"0123456789abcdef"[b&15]; }
    temp+=L".tmp";
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(includeSecurity) {
        HANDLE token=nullptr; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return false;
        DWORD size=0; GetTokenInformation(token,TokenUser,nullptr,0,&size);
        std::vector<BYTE> user(size); const bool got=GetTokenInformation(token,TokenUser,user.data(),size,&size)!=FALSE; CloseHandle(token);
        if(!got) return false;
        LPWSTR sid=nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid)) return false;
        const std::wstring acl=L"D:P(A;;FA;;;SY)(A;;FA;;;"+std::wstring(sid)+L")"; LocalFree(sid);
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)) return false;
    }
    SECURITY_ATTRIBUTES access{sizeof(access),descriptor,FALSE};
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,descriptor?&access:nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(descriptor) LocalFree(descriptor);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0,bytes=static_cast<DWORD>(text.size()*sizeof(wchar_t));
    bool ok=WriteFile(file,text.data(),bytes,&written,nullptr)!=FALSE && written==bytes && FlushFileBuffers(file)!=FALSE;
    CloseHandle(file);
    if(ok) ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok) DeleteFileW(temp.c_str());
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
    return ok;
}
void copyAppearance(Settings& to,const Settings& from) {
    to.effect=from.effect; to.palette=from.palette; to.speed=from.speed; to.brightness=from.brightness;
    to.scale=from.scale; to.turbulence=from.turbulence; to.glow=from.glow;
    to.grain=from.grain; to.vignette=from.vignette; to.clock=from.clock;
    to.clockStyle=from.clockStyle; to.clockSize=from.clockSize; to.clockOpacity=from.clockOpacity;
    to.clockPosition=from.clockPosition; to.clockWeight=from.clockWeight; to.clockColor=from.clockColor;
    to.clockFormat24=from.clockFormat24; to.clockSeconds=from.clockSeconds;
    to.accent=from.accent; to.tone=from.tone; to.roundness=from.roundness;
}
