// Exercise the actual native message handlers with isolated settings and no windows.
#include "../src/main.cpp"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* name) { if(!value) throw std::runtime_error(name); }
int main() {
    wchar_t temp[MAX_PATH]{},file[MAX_PATH]{};
    if(!GetTempPathW(MAX_PATH,temp) || !GetTempFileNameW(temp,L"hsf",0,file)) return 1;
    int result=0;
    try {
        configPath=file; settings=Settings{};
        settings.credential=Lock::create(L"original:password"); settings.lockEnabled=true;
        const auto original=settings.credential;
        check(writeSettings(configPath,settings),"initialize isolated protection");
        onAppMessage("act:clearPassword");
        onAppMessage("set:lockEnabled:0");
        onAppMessage("set:lockGraceMinutes:30");
        onAppMessage("pw:unauthorized password");
        check(settings.credential==original && settings.lockEnabled && settings.lockGraceMinutes==0,"native handlers reject unauthorized security changes");
        for(int i=0;i<5;++i) onAppMessage("auth:incorrect");
        check(authCooldownUntil>GetTickCount64(),"failed settings authentication activates cooldown");
        onAppMessage("auth:original:password");
        check(!securityGrantUntil,"correct password cannot skip cooldown");
        authCooldownUntil=0;
        onAppMessage("auth:original:password");
        onAppMessage("set:lockGraceMinutes:4");
        check(settings.lockGraceMinutes==4 && readSettings(configPath).lockGraceMinutes==4 && !securityGrantUntil,"verified policy change persists and consumes permission");
        onAppMessage("act:clearPassword");
        check(settings.credential==original,"permission cannot be reused");
        onAppMessage("auth:original:password");
        onAppMessage("pw:newpw");
        check(Lock::verify(L"newpw",settings.credential) && readSettings(configPath).credential==settings.credential,"verified password replacement");
        const auto replaced=settings.credential;
        onAppMessage("auth:newpw");
        securityGrantUntil=GetTickCount64()-1;
        onAppMessage("act:clearPassword");
        check(settings.credential==replaced,"expired permission rejected");
        locked=true; screens.push_back(Screen{}); activatedAt=GetTickCount64();
        check(passwordRequired(),"an engaged lock still requires authentication within grace period");
        onAppMessage("pw:another password"); onTrayMessage("act:quit");
        mainProc(nullptr,UnlockMessage,0,0);
        mainProc(nullptr,WM_HOTKEY,1,0);
        surfaceProc(nullptr,WM_KEYDOWN,VK_ESCAPE,0);
        surfaceProc(nullptr,WM_CLOSE,0,0);
        showSettings();
        check(locked && screens.size()==1 && settings.credential==replaced,"forged unlock, Escape, close, tray, hotkey and settings cannot dismiss lock");
        onLockMessage("try:incorrect"); check(!unlockVerified && locked,"incorrect lock password denied");
        onLockMessage("try:newpw"); check(unlockVerified && locked,"valid lock password authorizes deferred unlock");
        screens.clear(); locked=false; unlockVerified=false;
        onAppMessage("auth:newpw"); onAppMessage("act:clearPassword");
        check(!settings.hasPassword() && !settings.lockEnabled && !readSettings(configPath).hasPassword(),"verified removal persists");
        settings.securityError=true;
        onAppMessage("pw:recovery bypass");
        check(!settings.hasPassword(),"damaged security data cannot be replaced through the UI");
        std::cout<<"Native authentication and lock bypass regressions passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; result=1; }
    screens.clear(); locked=false; DeleteFileW(file); return result;
}
