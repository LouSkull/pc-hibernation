#include "bridge.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <charconv>
namespace Bridge {
namespace {
struct IntKey { const char* name; int Settings::*field; };
constexpr IntKey ints[]={
    {"previewFps",&Settings::previewFps},
    {"previewWidth",&Settings::previewWidth},
    {"minFps",&Settings::minFps},
    {"minQuality",&Settings::minQuality},
    {"restAfter",&Settings::restAfter},
    {"gpuBudget",&Settings::gpuBudget},
    {"backgroundPoll",&Settings::backgroundPoll},
    {"maxRenderHeight",&Settings::maxRenderHeight},
    {"mediaId",&Settings::mediaId},
    {"mediaFit",&Settings::mediaFit},
    {"wakeMouseThreshold",&Settings::wakeMouseThreshold},
    {"delay",&Settings::delay},{"speed",&Settings::speed},{"brightness",&Settings::brightness},
    {"palette",&Settings::palette},{"effect",&Settings::effect},{"scale",&Settings::scale},
    {"turbulence",&Settings::turbulence},{"glow",&Settings::glow},{"fps",&Settings::fps},
    {"quality",&Settings::quality},{"cycleSeconds",&Settings::cycleSeconds},{"fadeSeconds",&Settings::fadeSeconds},
    {"gameDelay",&Settings::gameDelay},{"videoDelay",&Settings::videoDelay},{"clockStyle",&Settings::clockStyle},
    {"clockSize",&Settings::clockSize},{"clockOpacity",&Settings::clockOpacity},{"clockPosition",&Settings::clockPosition},
    {"accent",&Settings::accent},{"tone",&Settings::tone},{"roundness",&Settings::roundness},
    {"lockGraceMinutes",&Settings::lockGraceMinutes},
    {"clockWeight",&Settings::clockWeight},{"clockColor",&Settings::clockColor},
    {"hotkeyToggleKey",&Settings::hotkeyToggleKey},{"hotkeyToggleMods",&Settings::hotkeyToggleMods},
    {"hotkeyPauseKey",&Settings::hotkeyPauseKey},{"hotkeyPauseMods",&Settings::hotkeyPauseMods},
    {"cursorStyle",&Settings::cursorStyle},{"cursorColor",&Settings::cursorColor},
    {"cursorSize",&Settings::cursorSize},{"cursorTip",&Settings::cursorTip}};
struct BoolKey { const char* name; bool Settings::*field; };
constexpr BoolKey bools[]={
    {"mediaRest",&Settings::mediaRest},
    {"adaptive",&Settings::adaptive},
    {"deepRest",&Settings::deepRest},
    {"ecoPriority",&Settings::ecoPriority},
    {"animateThumbs",&Settings::animateThumbs},
    {"debugEnabled",&Settings::debugEnabled},
    {"debugOverlay",&Settings::debugOverlay},
    {"debugLog",&Settings::debugLog},
    {"debugFreeze",&Settings::debugFreeze},
    {"softwareRenderer",&Settings::softwareRenderer},
    {"wakeProtection",&Settings::wakeProtection},
    {"wakeKeyboard",&Settings::wakeKeyboard},
    {"enabled",&Settings::enabled},{"grain",&Settings::grain},{"vignette",&Settings::vignette},
    {"clock",&Settings::clock},{"allMonitors",&Settings::allMonitors},{"pauseOnBattery",&Settings::pauseOnBattery},
    {"fullscreenGuard",&Settings::fullscreenGuard},{"playlist",&Settings::playlist},
    {"gameAllowed",&Settings::gameAllowed},{"videoAllowed",&Settings::videoAllowed},
    {"clockPrimaryOnly",&Settings::clockPrimaryOnly},{"muteAudio",&Settings::muteAudio},
    {"hideCursor",&Settings::hideCursor},{"mediaConservative",&Settings::mediaConservative},
    {"lockEnabled",&Settings::lockEnabled},
    {"clockFormat24",&Settings::clockFormat24},{"clockSeconds",&Settings::clockSeconds},
    {"hotkeysEnabled",&Settings::hotkeysEnabled},{"showPreview",&Settings::showPreview},
    {"cursorTrail",&Settings::cursorTrail},
    {"autoStart",&Settings::autoStart},{"startShowUI",&Settings::startShowUI}};
}
std::string escape(const std::string& text) {
    std::string out; out.reserve(text.size()+8);
    for(unsigned char c:text) {
        switch(c) {
        case '"': out+="\\\""; break;
        case '\\': out+="\\\\"; break;
        case '\n': out+="\\n"; break;
        case '\r': out+="\\r"; break;
        case '\t': out+="\\t"; break;
        default:
            if(c<0x20) { char buffer[8]; sprintf_s(buffer,"\\u%04x",c); out+=buffer; }
            else out+=static_cast<char>(c);
        }
    }
    return out;
}
std::string narrow(const std::wstring& text) {
    if(text.empty()) return {};
    int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
std::wstring widen(const std::string& text) {
    if(text.empty()) return {};
    int size=MultiByteToWideChar(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(size),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),out.data(),size);
    return out;
}
std::string toJson(const Settings& settings, const Status& status) {
    Settings s=settings; s.sanitize();
    std::string json="{";
    for(const auto& f:ints) json+="\""+std::string(f.name)+"\":"+std::to_string(s.*(f.field))+",";
    for(const auto& f:bools) json+="\""+std::string(f.name)+"\":"+(s.*(f.field)?"true":"false")+",";
    json+="\"hasPassword\":"+std::string(s.hasPassword()?"true":"false")+",";
    json+="\"securityError\":"+std::string(s.securityError?"true":"false")+",";
    // Only the file name: the page shows which cursor is in use, not where it lives.
    const size_t slash=s.cursorFile.find_last_of(L"\\/");
    json+="\"cursorName\":\""+escape(narrow(slash==std::wstring::npos?s.cursorFile:s.cursorFile.substr(slash+1)))+"\",";
    json+="\"monitors\":"+std::to_string(status.monitors)+",";
    json+="\"hasPreset\":"+std::string(status.hasPreset?"true":"false")+",";
    json+="\"hotkeysOk\":"+std::string(status.hotkeysOk?"true":"false")+",";
    json+="\"mediaAvailable\":"+std::string(status.mediaAvailable?"true":"false")+",";
    json+="\"running\":"+std::string(status.running?"true":"false")+",";
    json+="\"autoStartOn\":"+std::string(status.autoStart?"true":"false")+",";
    json+="\"version\":\""+escape(status.version)+"\",";
    json+="\"context\":\""+escape(status.context)+"\",";
    json+="\"message\":\""+escape(status.message)+"\"";
    json+="}";
    return json;
}
Message parse(const std::string& raw) {
    Message message;
    size_t first=raw.find(':');
    if(first==std::string::npos) { message.kind=raw; return message; }
    message.kind=raw.substr(0,first);
    std::string rest=raw.substr(first+1);
    if(message.kind=="set") {
        size_t second=rest.find(':');
        if(second==std::string::npos) { message.key=rest; return message; }
        message.key=rest.substr(0,second);
        message.value=rest.substr(second+1);   // keeps any further colons intact
    } else {
        message.key=std::move(rest);           // passwords may contain colons; avoid a second copy
    }
    return message;
}
bool applySetting(Settings& settings, const std::string& key, const std::string& value) {
    for(const auto& f:ints) if(key==f.name) {
        int number=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) return false;
        settings.*(f.field)=number;
        settings.sanitize(); return true;
    }
    for(const auto& f:bools) if(key==f.name) {
        if(value!="0" && value!="1" && value!="true" && value!="false") return false;
        settings.*(f.field)=(value=="1"||value=="true");
        settings.sanitize(); return true;
    }
    return false;
}
}
