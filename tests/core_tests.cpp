#include "../src/core.h"
#include <iostream>
#include <cstdlib>
void check(bool ok, const char* name) { if(!ok) { std::cerr<<name<<'\n'; std::exit(1); } }
int main() {
    Settings s;
    check(!shouldActivate(5999,1000,s,false,false),"must wait full five seconds");
    check(shouldActivate(6000,1000,s,false,false),"activate at threshold");
    check(!shouldActivate(6000,1000,s,true,false),"settings suppress activation");
    check(!shouldActivate(6000,1000,s,false,true),"no duplicate activation");
    s.enabled=false;
    check(!shouldActivate(6000,1000,s,false,false),"pause suppresses activation");
    s.enabled=true;
    check(shouldActivate(3000,UINT32_MAX-2000,s,false,false),"tick rollover");
    s.delay=-1; s.speed=200; s.brightness=0; s.palette=8; s.sanitize();
    check(s.delay==5 && s.speed==100 && s.brightness==15 && s.palette==7,"invalid settings clamped");
    s.effect=99; s.scale=0; s.glow=200; s.turbulence=-1; s.fps=47; s.quality=74; s.cycleSeconds=2; s.fadeSeconds=20; s.sanitize();
    check(s.effect==EffectCount-1 && s.scale==50 && s.glow==100 && s.turbulence==0,"appearance bounds");
    check(s.fps==47 && s.quality==100 && s.cycleSeconds==15 && s.fadeSeconds==5,"performance and timing bounds");
    s.delay=77; s.enabled=false; s.allMonitors=false; s.playlist=true;
    applyPreset(s,2);
    check(s.effect==2 && s.palette==1 && s.speed==55,"warm preset applied");
    check(s.delay==77 && !s.enabled && !s.allMonitors && s.playlist,"preset preserves behavior");
    s=Settings{}; s.clock=true;
    check(forMonitor(s,true).clock && !forMonitor(s,false).clock,"clock only on primary monitor");
    s.clockPrimaryOnly=false; check(forMonitor(s,true).clock && forMonitor(s,false).clock,"clock on all monitors");
    s.clock=false; check(!forMonitor(s,true).clock,"disabled clock stays disabled");
    check(shouldActivateInContext(6000,1000,s,Activity::Desktop,false,false),"desktop delay");
    check(!shouldActivateInContext(900000,0,s,Activity::Game,false,false),"games disabled independently");
    s.gameAllowed=true; s.gameDelay=60;
    check(!shouldActivateInContext(59999,0,s,Activity::Game,false,false),"game-specific threshold");
    check(shouldActivateInContext(60000,0,s,Activity::Game,false,false),"game enabled after one minute");
    check(!shouldActivateInContext(900000,0,s,Activity::Video,false,false),"video remains disabled when game enabled");
    s.videoAllowed=true; s.videoDelay=120;
    check(!shouldActivateInContext(119999,0,s,Activity::Video,false,false) && shouldActivateInContext(120000,0,s,Activity::Video,false,false),"independent video delay");
    s.clockSize=999; s.clockOpacity=-9; s.clockStyle=8; s.accent=9; s.tone=-1; s.gameDelay=0; s.videoDelay=99999; s.lockGraceMinutes=99; s.sanitize();
    check(s.clockSize==170 && s.clockOpacity==20 && s.clockStyle==2 && s.accent==5 && s.tone==0 && s.gameDelay==5 && s.videoDelay==1800,"new setting bounds");
    s.clockWeight=9; s.clockColor=-3; s.hotkeyToggleMods=99; s.hotkeyPauseKey=9999; s.sanitize();
    check(s.clockWeight==2 && s.clockColor==0 && s.hotkeyToggleMods==15 && s.hotkeyPauseKey==255,"clock and shortcut bounds");
    check(s.lockGraceMinutes==30,"lock grace clamped to 30 minutes");
    s.wakeMouseThreshold=999; s.sanitize(); check(s.wakeMouseThreshold==200,"mouse wake threshold clamped");
    s=Settings{}; WakeFilter wake;
    wake.mouse(6,2,false,s,1000); wake.mouse(-5,-2,false,s,1010);
    check(!wake.consume(1010),"small mouse vibration is ignored");
    wake.mouse(18,0,false,s,1020); check(!wake.consume(1020),"mouse stays asleep below configured distance");
    wake.mouse(7,0,false,s,1030); check(wake.consume(1030),"net mouse movement wakes at configured distance");
    s.wakeKeyboard=false; wake.keyboard(s); check(!wake.consume(1040),"keyboard wake can be disabled");
    s.wakeKeyboard=true; wake.keyboard(s); check(wake.consume(1050),"keyboard wake can be enabled");
    s.wakeProtection=false; s.wakeKeyboard=false; wake.keyboard(s); check(wake.consume(1060),"protection off restores keyboard wake");
    wake.mouse(1,0,false,s,1070); check(wake.consume(1070),"unfiltered mouse wakes immediately");
    s.wakeProtection=true; wake.mouse(0,0,true,s,1080); check(wake.consume(1080),"mouse button wakes immediately");
    Settings lock; const uint64_t away=24ull*3600000ull;
    check(!shouldLock(0,away,lock) && !lock.hasPassword(),"no lock without a password");
    lock.credential=L"250000:x:y"; check(lock.hasPassword() && !shouldLock(0,away,lock),"password without enable stays open");
    lock.lockEnabled=true; check(shouldLock(0,0,lock) && shouldLock(3600000,away,lock),"grace 0 always locks");
    lock.lockGraceMinutes=3;
    check(!shouldLock(179999,away,lock) && shouldLock(180000,away,lock),"grace threshold measured in minutes");
    check(!shouldLock(600000,60000,lock),"no re-lock right after an unlock");
    check(shouldLock(600000,180000,lock),"locks again once the grace passed since the unlock");
    std::cout<<"Core checks passed\n";
}
