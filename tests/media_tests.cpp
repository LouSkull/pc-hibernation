#include "../src/media.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool v,const char* text) { if(!v) throw std::runtime_error(text); }
int main() {
    namespace fs=std::filesystem;
    wchar_t temp[MAX_PATH]{},name[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"hml",0,name);DeleteFileW(name);
    fs::path root=name;fs::create_directories(root);int result=0;
    try {
        MediaLibrary library;library.open(root/L"Media");
        auto source=root/L"original.mp4";std::ofstream(source)<<"test payload";
        check(!library.import(L"",source),"name required");
        check(!library.import(L" spaces ",source),"trimmed name required");
        check(!MediaLibrary::supported(L"bad.html"),"no executable web documents");
        int id=library.import(L"Моя анимация",source);check(id>0,"import Unicode name");
        check(library.find(id).name==L"Моя анимация","Unicode survives persistence");
        fs::remove(source);check(fs::exists(library.folder()/library.find(id).file),"library copy independent of source");
        check(library.rename(id,L"\"Quoted name\""),"quoted name allowed");
        check(library.find(id).name==L"\"Quoted name\"","INI quote preservation");
        check(library.rename(id,L"New name"),"rename");
        auto replacement=root/L"replacement.webm";std::ofstream(replacement)<<"replacement";
        check(library.import(L"New name",replacement,id)==id,"replace keeps ID");
        check(!fs::exists(library.folder()/(std::to_wstring(id)+L".mp4")),"replacement removes old managed copy");
        check(!library.import(L"New name",root/L"missing.mp4",id),"missing replacement rejected");
        check(library.find(id).file==std::to_wstring(id)+L".webm","failed replacement preserves item");
        auto ini=library.folder()/(std::to_wstring(id)+L".ini");
        WritePrivateProfileStringW(L"Media",L"File",L"../replacement.webm",ini.c_str());
        check(!library.remove(id) && fs::exists(replacement),"metadata traversal cannot delete source");
        WritePrivateProfileStringW(L"Media",L"File",(std::to_wstring(id)+L".webm").c_str(),ini.c_str());
        check(library.remove(id) && fs::exists(replacement),"delete only library copy");
        check(library.items().empty(),"deleted metadata gone");
        auto large=library.folder()/L"2147483647.ini";
        WritePrivateProfileStringW(L"Media",L"Name",L"largest ID",large.c_str());
        WritePrivateProfileStringW(L"Media",L"File",L"2147483647.mp4",large.c_str());
        check(library.import(L"After large ID",replacement)==1,"large metadata ID cannot overflow the next ID");
        MediaLibrary unavailable; unavailable.open(replacement/L"not-a-folder");
        check(unavailable.items().empty() && !unavailable.import(L"Unavailable",replacement),"filesystem errors do not crash the library");
        std::cout<<"Media library checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what();result=1; }
    fs::remove_all(root);return result;
}
