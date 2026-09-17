#include "../src/config.h"
#include "../src/lock.h"
#include <windows.h>
#include <aclapi.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>
void check(bool ok,const char* name) { if(!ok) throw std::runtime_error(name); }
int main() {
    wchar_t temp[MAX_PATH]{},file[MAX_PATH]{};
    if(!GetTempPathW(MAX_PATH,temp)||!GetTempFileNameW(temp,L"hib",0,file)) return 1;
    int result=0;
    try {
        Settings s;s.fps=47;s.quality=63;s.previewFps=7;s.previewWidth=320;s.minFps=5;s.minQuality=20;s.restAfter=300;s.gpuBudget=12;s.backgroundPoll=4000;s.maxRenderHeight=720;s.mediaId=12;s.mediaFit=2;s.adaptive=false;s.deepRest=false;s.ecoPriority=false;s.animateThumbs=false;s.debugEnabled=true;s.debugOverlay=true;s.debugLog=true;s.softwareRenderer=true;s.mediaRest=false; s.delay=123; s.effect=5; s.palette=7; s.speed=77; s.brightness=66;
        s.scale=135; s.glow=21; s.turbulence=87; s.fps=15; s.quality=50; s.cycleSeconds=125; s.fadeSeconds=4;
        s.enabled=false; s.grain=false; s.vignette=false; s.clock=true; s.allMonitors=false;
        s.pauseOnBattery=false; s.fullscreenGuard=false; s.playlist=true;
        s.effect=11; s.clockPrimaryOnly=true; s.muteAudio=true; s.hideCursor=false;
        s.gameAllowed=true; s.videoAllowed=false; s.gameDelay=90; s.videoDelay=200;
        s.clockStyle=2; s.clockSize=145; s.clockOpacity=55; s.clockPosition=2; s.accent=4; s.tone=2; s.roundness=19;
        s.clockWeight=2; s.clockColor=3; s.clockFormat24=false; s.clockSeconds=true;
        s.hotkeysEnabled=false; s.showPreview=false;
        s.hotkeyToggleKey='K'; s.hotkeyToggleMods=6; s.hotkeyPauseKey='J'; s.hotkeyPauseMods=9;
        s.lockEnabled=true; s.lockGraceMinutes=7; s.credential=Lock::create(L"release-test-password");
        s.cursorStyle=2; s.cursorColor=4; s.cursorSize=0; s.cursorTip=1; s.cursorTrail=false;
        s.wakeProtection=true; s.wakeKeyboard=false; s.wakeMouseThreshold=73;
        s.cursorFile=L"C:\\Users\\Тест\\AppData\\Local\\Hibernation\\Cursors\\ring.png";
        check(writeSettings(file,s),"write configuration"); auto r=readSettings(file);
        check(r.previewFps==10 && r.previewWidth==800 && r.minFps==5 && r.minQuality==100 && r.restAfter==300 && r.gpuBudget==12 && r.backgroundPoll==4000 && r.maxRenderHeight==720 && r.mediaId==12 && r.mediaFit==2 && !r.adaptive && !r.deepRest && !r.ecoPriority && r.animateThumbs && r.debugEnabled && r.debugOverlay && r.debugLog && r.softwareRenderer && !r.mediaRest,"performance and media persistence");
        check(r.delay==123 && r.effect==11 && r.palette==7 && r.speed==77 && r.brightness==66,"appearance roundtrip");
        check(r.scale==135 && r.glow==21 && r.turbulence==87 && r.fps==15 && r.quality==100 && r.cycleSeconds==125 && r.fadeSeconds==4,"advanced roundtrip");
        check(!r.enabled && !r.grain && !r.vignette && r.clock && !r.allMonitors && !r.pauseOnBattery && !r.fullscreenGuard && r.playlist,"toggle roundtrip");
        check(r.clockPrimaryOnly && r.muteAudio && !r.hideCursor && r.gameAllowed && !r.videoAllowed && r.gameDelay==90 && r.videoDelay==200,"per-context and immersion roundtrip");
        check(r.clockStyle==2 && r.clockSize==145 && r.clockOpacity==55 && r.clockPosition==2 && r.accent==4 && r.tone==2 && r.roundness==19,"clock and theme roundtrip");
        check(r.clockWeight==2 && r.clockColor==3 && !r.clockFormat24 && r.clockSeconds && !r.showPreview,"clock customisation roundtrip");
        check(!r.hotkeysEnabled && r.hotkeyToggleKey=='K' && r.hotkeyToggleMods==6 && r.hotkeyPauseKey=='J' && r.hotkeyPauseMods==9,"custom shortcut roundtrip");
        check(!r.securityError && r.lockEnabled && r.lockGraceMinutes==7 && r.credential==s.credential && r.hasPassword(),"protected lock and credential roundtrip");
        {
            std::ifstream input(file,std::ios::binary); const std::string bytes((std::istreambuf_iterator<char>(input)),{});
            const std::string raw(reinterpret_cast<const char*>(s.credential.data()),s.credential.size()*sizeof(wchar_t));
            check(bytes.find(raw)==std::string::npos,"raw verifier is absent from disk");
        }
        PSECURITY_DESCRIPTOR descriptor=nullptr; PACL acl=nullptr;
        check(GetNamedSecurityInfoW(file,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&descriptor)==ERROR_SUCCESS,"read file permissions");
        SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision=0;
        check(GetSecurityDescriptorControl(descriptor,&control,&revision) && (control&SE_DACL_PROTECTED) && acl && acl->AceCount==2,"file has an explicit protected two-entry DACL");
        LocalFree(descriptor);
        WritePrivateProfileStringW(L"Settings",L"Lock",L"0",file);
        WritePrivateProfileStringW(L"Settings",L"LockGraceMinutes",L"30",file);
        r=readSettings(file);
        check(r.lockEnabled && r.lockGraceMinutes==7,"plaintext edits cannot override protected policy");
        check(r.cursorStyle==2 && r.cursorColor==4 && r.cursorSize==0 && r.cursorTip==1 && !r.cursorTrail && r.cursorFile==s.cursorFile,"cursor roundtrip");
        check(r.wakeProtection && !r.wakeKeyboard && r.wakeMouseThreshold==73,"accidental wake settings roundtrip");
        Settings plain; plain.cursorFile.clear();
        check(writeSettings(file,plain) && readSettings(file).cursorFile.empty(),"clearing the cursor file removes it");
        check(writeSettings(file,s),"write configuration again"); r=readSettings(file);
        copyAppearance(r,Settings{});
        check(r.lockEnabled && r.credential==s.credential,"appearance restore keeps the password");
        check(r.effect==0 && r.palette==0 && r.speed==45 && r.scale==100 && !r.clock,"appearance restore");
        check(r.delay==123 && !r.enabled && !r.allMonitors && r.playlist && r.fps==15,"restore preserves behavior");
        WritePrivateProfileStringW(L"Settings",L"Effect",L"-20",file);
        WritePrivateProfileStringW(L"Settings",L"Delay",L"9999999",file);
        WritePrivateProfileStringW(L"Settings",L"CursorStyle",L"9",file);
        WritePrivateProfileStringW(L"Settings",L"CursorColor",L"-3",file);
        r=readSettings(file); check(r.effect==0 && r.delay==300,"malformed imported bounds");
        check(r.cursorStyle==2 && r.cursorColor==0,"malformed cursor bounds");
        check(r.muteAudio && !r.hideCursor && r.gameAllowed && !r.videoAllowed,"presets preserve immersion and rules");
        check(writeSettings(file,s,false),"portable export");
        wchar_t field[4096]{};
        GetPrivateProfileStringW(L"Security",nullptr,L"",field,4096,file);
        check(!field[0],"export contains no Security section");
        r=readSettings(file,s,false);
        check(r.credential==s.credential && r.lockEnabled && r.lockGraceMinutes==7,"import preserves local protection");
        // Migrate a valid old plaintext verifier without asking users to reset passwords.
        WritePrivateProfileStringW(L"Security",L"Credential",s.credential.c_str(),file);
        WritePrivateProfileStringW(L"Settings",L"Lock",L"1",file);
        r=readSettings(file);
        check(!r.securityError && r.credential==s.credential && writeSettings(file,r),"legacy migration");
        GetPrivateProfileStringW(L"Security",L"Credential",L"",field,4096,file);
        check(!field[0],"migration removes plaintext verifier");
        HANDLE busy=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        check(busy!=INVALID_HANDLE_VALUE && !writeSettings(file,Settings{}),"failed replacement is reported");
        CloseHandle(busy);
        check(readSettings(file).credential==s.credential,"failed replacement preserves old password");
        Settings injected=s; injected.cursorFile=L"x\r\n[Security]\r\nCredential=";
        check(!writeSettings(file,injected),"INI injection rejected");
        WritePrivateProfileStringW(L"Security",L"ProtectedState",L"dpapi1:AAAA",file);
        r=readSettings(file);
        check(r.securityError && !r.enabled && r.lockEnabled && !writeSettings(file,r),"damaged protection cannot silently disable the lock or overwrite the file");
        r=readSettings(file,s,false);
        check(!r.securityError && r.credential==s.credential,"imports ignore even malformed foreign security data");
        std::cout<<"configuration and protected storage checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
    DeleteFileW(file); return result;
}
