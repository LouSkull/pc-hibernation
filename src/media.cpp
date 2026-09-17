#include "media.h"
#include <windows.h>
#include <cwctype>
#include <algorithm>
#include <limits>
namespace fs=std::filesystem;
namespace {
std::wstring extension(const fs::path& path) {
    auto ext=path.extension().wstring();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));}); return ext;
}
std::wstring get(const fs::path& path,const wchar_t* key) {
    wchar_t text[32768]{}; GetPrivateProfileStringW(L"Media",key,L"",text,32768,path.c_str()); return text;
}
}
void MediaLibrary::open(const fs::path& folder) { root=folder; std::error_code error; fs::create_directories(root,error); }
bool MediaLibrary::validName(const std::wstring& name) {
    if(name.empty() || name.size()>80 || iswspace(name.front()) || iswspace(name.back())) return false;
    return std::none_of(name.begin(),name.end(),[](wchar_t c){return c<32;});
}
bool MediaLibrary::supported(const fs::path& path) {
    auto ext=extension(path); return ext==L".mp4" || ext==L".webm" || ext==L".gif" || ext==L".webp";
}
MediaItem MediaLibrary::find(int id) const {
    if(id<=0) return {};
    auto path=root/(std::to_wstring(id)+L".ini");
    MediaItem item{id,get(path,L"Name"),get(path,L"File"),get(path,L"Source")};
    if(get(path,L"NameFormat")==L"1" && !item.name.empty()) item.name.erase(0,1);
    // Metadata never grants permission to read/delete outside our managed library.
    if(!validName(item.name) || !supported(item.file) ||
       item.file!=std::to_wstring(id)+extension(item.file)) return {};
    return item;
}
std::vector<MediaItem> MediaLibrary::items() const {
    std::vector<MediaItem> result; std::error_code error;
    for(fs::directory_iterator it(root,error),end; !error && it!=end; it.increment(error)) {
        const auto& entry=*it;
        if(entry.path().extension()!=L".ini") continue;
        try { auto stem=entry.path().stem().wstring(); size_t used=0; int id=std::stoi(stem,&used);
            if(used!=stem.size() || stem!=std::to_wstring(id)) continue;
            auto item=find(id); if(item.id) result.push_back(item);
        } catch(...) {}
    }
    std::sort(result.begin(),result.end(),[](auto& a,auto& b){return a.id<b.id;}); return result;
}
bool MediaLibrary::write(const MediaItem& item) {
    auto path=root/(std::to_wstring(item.id)+L".ini"); auto temp=path; temp+=L".tmp";
    // UTF-16 INI preserves non-ASCII names and paths with legacy Win32 profile APIs.
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    const wchar_t bom=0xfeff; DWORD count=0; bool ok=WriteFile(file,&bom,sizeof(bom),&count,nullptr)!=FALSE; CloseHandle(file);
    ok=ok && WritePrivateProfileStringW(L"Media",L"Name",(L"@"+item.name).c_str(),temp.c_str());
    ok=ok && WritePrivateProfileStringW(L"Media",L"NameFormat",L"1",temp.c_str());
    ok=ok && WritePrivateProfileStringW(L"Media",L"File",item.file.c_str(),temp.c_str());
    ok=ok && WritePrivateProfileStringW(L"Media",L"Source",item.source.c_str(),temp.c_str());
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
    if(ok) ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok) DeleteFileW(temp.c_str()); return ok;
}
int MediaLibrary::import(const std::wstring& name,const fs::path& source,int replace) {
    std::error_code error;
    if(!validName(name) || !supported(source) || !fs::is_regular_file(source,error) || error) return 0;
    MediaItem old=find(replace); if(replace && !old.id) return 0;
    int id=replace;
    if(!id) {
        id=1;
        for(const auto& item:items()) {
            if(item.id>id) break;
            if(item.id==id) { if(id==std::numeric_limits<int>::max()) return 0; ++id; }
        }
    }
    const auto absolute=fs::absolute(source,error); if(error) return 0;
    MediaItem item{id,name,std::to_wstring(id)+extension(source),absolute.wstring()};
    auto dest=root/item.file; auto temp=dest; temp+=L".import";
    if(!CopyFileW(source.c_str(),temp.c_str(),FALSE)) return 0;
    // Retain an old payload until both new bytes and metadata are safely installed.
    auto backup=dest; backup+=L".backup";
    bool existed=fs::exists(dest,error);
    if(error) { DeleteFileW(temp.c_str()); return 0; }
    if(existed && !MoveFileExW(dest.c_str(),backup.c_str(),MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(temp.c_str()); return 0; }
    if(!MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING) || !write(item)) {
        DeleteFileW(dest.c_str()); if(existed) MoveFileExW(backup.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(temp.c_str()); return 0;
    }
    if(existed) DeleteFileW(backup.c_str());
    if(old.id && old.file!=item.file) DeleteFileW((root/old.file).c_str());
    return id;
}
bool MediaLibrary::rename(int id,const std::wstring& name) {
    auto item=find(id); if(!item.id || !validName(name)) return false;
    item.name=name; return write(item);
}
bool MediaLibrary::remove(int id) {
    auto item=find(id); if(!item.id) return false;
    auto file=root/item.file;
    std::error_code error;
    if(fs::exists(file,error) && !DeleteFileW(file.c_str())) return false;
    if(error) return false;
    return DeleteFileW((root/(std::to_wstring(id)+L".ini")).c_str())!=FALSE;
}
