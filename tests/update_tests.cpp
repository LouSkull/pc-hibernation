#include "../src/update.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* name) { if(!ok) { std::cerr<<name<<'\n'; std::exit(1); } }
int main() {
    // Version ordering, including a leading v, extra components and pre-releases.
    check(Update::compare("1.0.0","1.0.0")==0,"equal versions");
    check(Update::compare("v1.2.0","1.1.9")>0,"minor beats patch, v ignored");
    check(Update::compare("1.0.0","1.0.1")<0,"patch behind");
    check(Update::compare("2.0","1.9.9")>0,"major wins with missing parts");
    check(Update::compare("1.0.0.1","1.0.0")>0,"fourth component counts");
    check(Update::compare("1.0.0-beta","1.0.0")<0,"pre-release ranks below release");
    check(Update::compare("1.0.0","1.0.0-rc1")>0,"release beats its own pre-release");
    check(Update::compare("1.10.0","1.9.0")>0,"double-digit minor is numeric, not lexical");

    // String extraction tolerates escaped quotes and slashes.
    const std::string json=R"({"url":"x","html_url":"https:\/\/github.com\/LouSkull\/pc-hibernation\/releases\/tag\/v1.2.0",)"
                           R"("tag_name":"v1.2.0","name":"Shiny \"1.2\"","body":"Line one\nLine two","author":{"html_url":"https://github.com/someone"}})";
    check(Update::jsonString(json,"tag_name")=="v1.2.0","tag_name read");
    check(Update::jsonString(json,"html_url")=="https://github.com/LouSkull/pc-hibernation/releases/tag/v1.2.0","first html_url is the release page, slashes unescaped");
    check(Update::jsonString(json,"name")=="Shiny \"1.2\"","escaped quotes unescaped");

    Update::Result r;
    check(Update::parseRelease(json,L"1.0.0",r),"parse succeeds");
    check(r.ok && r.newer,"1.2.0 is newer than 1.0.0");
    check(r.latest==L"v1.2.0" && r.current==L"1.0.0","versions recorded");
    check(r.url==L"https://github.com/LouSkull/pc-hibernation/releases/tag/v1.2.0","release url recorded");
    check(r.notes.find(L'\n')!=std::wstring::npos,"body newline decoded");

    Update::Result same;
    check(Update::parseRelease(json,L"1.2.0",same) && same.ok && !same.newer,"same version is not newer");
    Update::Result ahead;
    check(Update::parseRelease(json,L"2.0.0",ahead) && ahead.ok && !ahead.newer,"a newer local build is not offered a downgrade");

    // A 404 body (no releases yet) is reported, not treated as an update.
    Update::Result none;
    check(!Update::parseRelease(R"({"message":"Not Found","documentation_url":"x"})",L"1.0.0",none),"404 body rejected");
    check(!none.ok && !none.newer && !none.error.empty(),"404 sets an error, not an update");
    check(Update::jsonString(R"({"author":{"html_url":"wrong"},"html_url":"right"})","html_url")=="right","nested author URL never shadows the release URL");
    check(Update::jsonString(R"({"body":"\u041f\u0430\u0440\u043e\u043b\u044c \ud83d\udd12"})","body")=="Пароль 🔒","Unicode and surrogate escapes decoded");
    for(const auto* malformed:{"{\"tag_name\":\"v2", "{\"tag_name\":\"v2\",}", "{\"tag_name\":\"v2\"}junk", "{\"tag_name\":\"v2\",\"tag_name\":\"v3\"}", "{\"tag_name\":\"<img>\"}"})
        check(!Update::parseRelease(malformed,L"1.0.0",none) && !none.ok && !none.newer,"malformed or ambiguous response rejected and result reset");
    check(Update::safeReleaseUrl(L"https://github.com/LouSkull/pc-hibernation/releases/tag/v1.2",L"LouSkull",L"pc-hibernation"),"own repository release accepted");
    for(const auto* url:{L"https://github.com/other/repo/releases/tag/v2",L"https://github.com.evil.test/LouSkull/pc-hibernation/releases/tag/v2",L"http://github.com/LouSkull/pc-hibernation/releases/tag/v2",L"https://github.com/LouSkull/pc-hibernation/releases/tag/../../other",L"https://github.com/LouSkull/pc-hibernation/releases/tag/v2\n"})
        check(!Update::safeReleaseUrl(url,L"LouSkull",L"pc-hibernation"),"foreign or unsafe URL rejected");
    check(Update::compare("42949672960.0","1.0")>0,"version overflow cannot wrap around");

    std::cout<<"update checks passed\n";
    return 0;
}
