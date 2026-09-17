#include "update.h"
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <map>
#include <set>
#include <limits>
#include <algorithm>

namespace Update {
namespace {
std::wstring widen(const std::string& text) {
    if(text.empty()) return {};
    int size=MultiByteToWideChar(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(size),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.c_str(),static_cast<int>(text.size()),out.data(),size);
    return out;
}
// One dotted-number component; also reports whether a pre-release suffix ("-beta") followed.
struct Parsed { unsigned parts[4]={0,0,0,0}; bool prerelease=false; };
Parsed parseVersion(const std::string& raw) {
    Parsed v; size_t i=0;
    while(i<raw.size() && (raw[i]=='v' || raw[i]=='V' || raw[i]==' ')) i++;
    for(int part=0; part<4 && i<raw.size(); part++) {
        unsigned value=0; bool any=false;
        while(i<raw.size() && raw[i]>='0' && raw[i]<='9') {
            const unsigned digit=static_cast<unsigned>(raw[i]-'0');
            value=value>(std::numeric_limits<unsigned>::max()-digit)/10?std::numeric_limits<unsigned>::max():value*10+digit;
            i++; any=true;
        }
        v.parts[part]=value;
        if(!any) break;
        if(i<raw.size() && raw[i]=='.') { i++; continue; }
        break;
    }
    // Anything left that is not pure separator counts as a pre-release marker.
    while(i<raw.size()) { if(raw[i]!='.' && raw[i]!=' ') { v.prerelease=true; break; } i++; }
    return v;
}
// Validate the complete bounded JSON document, retaining only top-level strings.
// A nested author's html_url must never be mistaken for the release URL.
struct JsonReader {
    const std::string& text; size_t at=0;
    void space() { while(at<text.size() && (text[at]==' ' || text[at]=='\r' || text[at]=='\n' || text[at]=='\t')) ++at; }
    bool take(char c) { space(); if(at<text.size() && text[at]==c) { ++at; return true; } return false; }
    bool hex(unsigned& n) {
        n=0;
        for(int i=0;i<4;++i) {
            if(at==text.size()) return false;
            const char c=text[at++];
            const int v=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;
            if(v<0) return false; n=n*16+static_cast<unsigned>(v);
        }
        return true;
    }
    bool string(std::string& out) {
        if(!take('"')) return false;
        out.clear();
        while(at<text.size()) {
            const unsigned char c=static_cast<unsigned char>(text[at++]);
            if(c=='"') return true;
            if(c<32) return false;
            if(c!='\\') { out+=static_cast<char>(c); continue; }
            if(at==text.size()) return false;
            switch(text[at++]) {
                case '"': out+='"'; break; case '\\': out+='\\'; break; case '/': out+='/'; break;
                case 'n': out+='\n'; break; case 't': out+='\t'; break; case 'r': out+='\r'; break;
                case 'b': out+='\b'; break; case 'f': out+='\f'; break;
                case 'u': {
                    unsigned cp=0; if(!hex(cp)) return false;
                    if(cp>=0xd800 && cp<=0xdbff) {
                        if(at+2>text.size() || text[at++]!='\\' || text[at++]!='u') return false;
                        unsigned low=0; if(!hex(low) || low<0xdc00 || low>0xdfff) return false;
                        cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                    } else if(cp>=0xdc00 && cp<=0xdfff) return false;
                    if(cp<0x80) out+=static_cast<char>(cp);
                    else {
                        if(cp<0x800) out+=static_cast<char>(0xc0|(cp>>6));
                        else {
                            if(cp<0x10000) out+=static_cast<char>(0xe0|(cp>>12));
                            else { out+=static_cast<char>(0xf0|(cp>>18)); out+=static_cast<char>(0x80|((cp>>12)&63)); }
                            out+=static_cast<char>(0x80|((cp>>6)&63));
                        }
                        out+=static_cast<char>(0x80|(cp&63));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(unsigned depth,std::string* captured=nullptr) {
        space(); if(depth>32 || at==text.size()) return false;
        if(text[at]=='"') { std::string out; const bool ok=string(out); if(captured) *captured=std::move(out); return ok; }
        if(text[at]=='{') return object(depth+1,nullptr);
        if(take('[')) { if(take(']')) return true; do { if(!value(depth+1)) return false; } while(take(',')); return take(']'); }
        for(const char* word:{"true","false","null"}) {
            const size_t length=strlen(word);
            if(text.compare(at,length,word)==0) { at+=length; return true; }
        }
        auto digit=[&]{return at<text.size() && text[at]>='0' && text[at]<='9';};
        if(at<text.size() && text[at]=='-') ++at;
        if(!digit()) return false;
        if(text[at]=='0') ++at; else while(digit()) ++at;
        if(at<text.size() && text[at]=='.') { ++at; if(!digit()) return false; while(digit()) ++at; }
        if(at<text.size() && (text[at]=='e' || text[at]=='E')) {
            ++at; if(at<text.size() && (text[at]=='+' || text[at]=='-')) ++at;
            if(!digit()) return false; while(digit()) ++at;
        }
        return true;
    }
    bool object(unsigned depth,std::map<std::string,std::string>* fields) {
        if(depth>32 || !take('{')) return false;
        if(take('}')) return true;
        std::set<std::string> seen;
        do {
            std::string key,result;
            if(!string(key) || !seen.insert(key).second || !take(':') || !value(depth,fields?&result:nullptr)) return false;
            if(fields) (*fields)[key]=std::move(result);
        } while(take(','));
        return take('}');
    }
};
bool readJson(const std::string& json,std::map<std::string,std::string>& fields) {
    if(json.empty() || json.size()>1'000'000 || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,json.data(),static_cast<int>(json.size()),nullptr,0)) return false;
    JsonReader reader{json}; if(!reader.object(0,&fields)) return false;
    reader.space(); return reader.at==json.size();
}
}

int compare(const std::string& a, const std::string& b) {
    Parsed x=parseVersion(a), y=parseVersion(b);
    for(int i=0;i<4;i++) {
        if(x.parts[i]<y.parts[i]) return -1;
        if(x.parts[i]>y.parts[i]) return 1;
    }
    if(x.prerelease!=y.prerelease) return x.prerelease?-1:1;   // 1.0.0-beta < 1.0.0
    return 0;
}

std::string jsonString(const std::string& json, const std::string& key) {
    std::map<std::string,std::string> fields;
    return readJson(json,fields)?fields[key]:std::string{};
}

bool parseRelease(const std::string& json, const std::wstring& currentVersion, Result& out) {
    out=Result{}; out.current=currentVersion;
    std::map<std::string,std::string> fields;
    if(!readJson(json,fields)) { out.error=L"GitHub returned an invalid release document."; return false; }
    const std::string tag=fields["tag_name"];
    if(tag.empty()) {
        const std::string message=fields["message"].substr(0,400);
        out.error=message.empty()?L"No published release was found.":
            (message=="Not Found"?L"No published release yet.":widen(message));
        return false;
    }
    const size_t first=(tag[0]=='v' || tag[0]=='V')?1:0;
    if(tag.size()>128 || first==tag.size() || tag[first]<'0' || tag[first]>'9' ||
       !std::all_of(tag.begin()+first,tag.end(),[](char c){return (c>='0'&&c<='9') || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || c=='.' || c=='-' || c=='+';})) {
        out.error=L"The release tag is not a version number."; return false;
    }
    out.ok=true;
    out.latest=widen(tag);
    out.url=widen(fields["html_url"]);
    std::string body=fields["body"];
    if(body.size()>400) body=body.substr(0,400)+"...";
    out.notes=widen(body);
    std::string current; for(wchar_t c:currentVersion) current+=static_cast<char>(c<=127?c:'?');
    out.newer=compare(tag,current)>0;
    return true;
}

bool safeReleaseUrl(const std::wstring& url,const std::wstring& owner,const std::wstring& repo) {
    const std::wstring prefix=L"https://github.com/"+owner+L"/"+repo+L"/releases/tag/";
    return url.size()>prefix.size() && url.size()<=2048 && url.rfind(prefix,0)==0 &&
        url.find(L"..",prefix.size())==std::wstring::npos &&
        std::none_of(url.begin(),url.end(),[](wchar_t c){return c<=32 || c==127 || c==L'\\' || c==L'?' || c==L'#';});
}

Result fetch(const std::wstring& owner, const std::wstring& repo, const std::wstring& currentVersion) {
    Result result; result.current=currentVersion;
    HINTERNET session=WinHttpOpen(L"Hibernation-Updater/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session) { result.error=L"Could not start the network request."; return result; }
    // Keep a caller from waiting forever on a stalled connection.
    WinHttpSetTimeouts(session,4000,4000,6000,6000);
    HINTERNET connect=WinHttpConnect(session,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET request=connect?WinHttpOpenRequest(connect,L"GET",
        (L"/repos/"+owner+L"/"+repo+L"/releases/latest").c_str(),
        nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    bool ok=false; std::string body;
    if(request) {
        // GitHub requires a User-Agent (set above) and serves JSON for this Accept header.
        DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
        WinHttpAddRequestHeaders(request,L"Accept: application/vnd.github+json",static_cast<DWORD>(-1),WINHTTP_ADDREQ_FLAG_ADD);
        if(WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) &&
           WinHttpReceiveResponse(request,nullptr)) {
            DWORD status=0,size=sizeof(status);
            WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
            char chunk[16384]; bool complete=false;
            const auto deadline=GetTickCount64()+15000;
            while(GetTickCount64()<deadline) {
                DWORD read=0;
                if(!WinHttpReadData(request,chunk,sizeof(chunk),&read)) break;
                if(!read) { complete=true; break; }
                if(body.size()+read>1'000'000) break;
                body.append(chunk,read);
            }
            if(status==200 && complete) ok=true;
            else if(status==200) result.error=L"The release response was incomplete or too large.";
            else if(status==404) result.error=L"No published release yet.";
            else if(status) result.error=L"GitHub returned HTTP "+std::to_wstring(status)+L".";
        }
    }
    if(!ok && result.error.empty()) result.error=L"Could not reach GitHub. Check your connection.";
    if(request) WinHttpCloseHandle(request);
    if(connect) WinHttpCloseHandle(connect);
    if(session) WinHttpCloseHandle(session);
    if(ok && parseRelease(body,currentVersion,result) && !safeReleaseUrl(result.url,owner,repo)) {
        result.ok=false; result.newer=false; result.url.clear(); result.error=L"The release link does not belong to this repository.";
    }
    return result;
}
}
