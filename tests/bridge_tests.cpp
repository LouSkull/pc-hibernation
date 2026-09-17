#include "../src/bridge.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* name) { if(!ok) { std::cerr<<name<<'\n'; std::exit(1); } }
bool has(const std::string& text,const std::string& piece) { return text.find(piece)!=std::string::npos; }
int main() {
    // Message parsing.
    auto set=Bridge::parse("set:speed:72");
    check(set.kind=="set" && set.key=="speed" && set.value=="72","set message parsed");
    auto act=Bridge::parse("act:launch");
    check(act.kind=="act" && act.key=="launch","act message parsed");
    auto colons=Bridge::parse("pw:a:b:c");
    check(colons.kind=="pw" && colons.key=="a:b:c","password keeps inner colons");
    auto bare=Bridge::parse("ping");
    check(bare.kind=="ping" && bare.key.empty(),"bare message parsed");

    // Applying settings, including clamping through sanitize().
    Settings s;
    check(Bridge::applySetting(s,"speed","72") && s.speed==72,"int setting applied");
    check(Bridge::applySetting(s,"speed","9999") && s.speed==100,"int setting clamped");
    check(Bridge::applySetting(s,"enabled","0") && !s.enabled,"bool setting off");
    check(Bridge::applySetting(s,"enabled","true") && s.enabled,"bool setting on");
    check(Bridge::applySetting(s,"lockGraceMinutes","99") && s.lockGraceMinutes==30,"grace clamped");
    check(Bridge::applySetting(s,"wakeMouseThreshold","999") && s.wakeMouseThreshold==200,"wake distance clamped");
    check(Bridge::applySetting(s,"wakeKeyboard","0") && !s.wakeKeyboard,"keyboard wake applied");
    check(!Bridge::applySetting(s,"nonsense","1"),"unknown key rejected");
    check(!Bridge::applySetting(s,"speed","3junk") && !Bridge::applySetting(s,"speed","99999999999999"),"malformed integers rejected");
    check(!Bridge::applySetting(s,"enabled","garbage") && s.enabled,"malformed booleans cannot disable a setting");

    // Serialisation covers every bridged key and never leaks the credential.
    Bridge::Status status; status.monitors=3; status.context="Video"; status.message="hi";
    s.credential=L"250000:c2FsdA==:aGFzaA==";
    std::string json=Bridge::toJson(s,status);
    check(json.front()=='{' && json.back()=='}',"json is an object");
    for(const char* key:{"delay","speed","effect","palette","clockStyle","accent","roundness",
                         "lockGraceMinutes","enabled","clock","muteAudio","lockEnabled",
                         "clockWeight","clockColor","clockFormat24","clockSeconds",
                         "hotkeysEnabled","hotkeyToggleKey","hotkeyToggleMods","showPreview",
                         "cursorStyle","cursorColor","cursorSize","cursorTip","cursorTrail","cursorName",
                         "wakeProtection","wakeKeyboard","wakeMouseThreshold"})
        check(has(json,std::string("\"")+key+"\":"),"json contains every bridged key");
    check(has(json,"\"hasPassword\":true"),"json reports that a password exists");
    check(!has(json,"250000:"),"json never carries the credential");
    s.cursorFile=L"C:\\Users\\private\\AppData\\Local\\Hibernation\\Cursors\\ring \"one\".png";
    json=Bridge::toJson(s,status);
    check(has(json,"\"cursorName\":\"ring \\\"one\\\".png\"") && !has(json,"private"),"only the cursor file name reaches the page");
    check(Bridge::applySetting(s,"cursorStyle","7") && s.cursorStyle==2,"cursor style clamped");
    check(Bridge::applySetting(s,"cursorTrail","0") && !s.cursorTrail,"cursor trail applied");
    check(has(json,"\"monitors\":3") && has(json,"\"context\":\"Video\""),"json carries status");

    // Text helpers.
    check(Bridge::escape("a\"b\\c\nd")=="a\\\"b\\\\c\\nd","json escaping");
    check(Bridge::narrow(Bridge::widen("ascii"))=="ascii","utf8 round-trip");
    check(Bridge::narrow(L"Aé")=="A\xc3\xa9","wide to utf8");
    std::cout<<"bridge checks passed\n";
}
