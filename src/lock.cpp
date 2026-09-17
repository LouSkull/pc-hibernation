#include "lock.h"
#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <algorithm>
namespace Lock {
void wipe(std::wstring& text) { if(!text.empty()) SecureZeroMemory(text.data(),text.size()*sizeof(wchar_t)); text.clear(); }
void wipe(std::string& text) { if(!text.empty()) SecureZeroMemory(text.data(),text.size()); text.clear(); }
namespace {
const wchar_t alphabet[]=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int decodeChar(wchar_t c) {
    if(c>=L'A'&&c<=L'Z') return c-L'A';
    if(c>=L'a'&&c<=L'z') return c-L'a'+26;
    if(c>=L'0'&&c<=L'9') return c-L'0'+52;
    if(c==L'+') return 62; if(c==L'/') return 63; return -1;
}
}
std::wstring base64Encode(const std::vector<uint8_t>& data) {
    std::wstring out; out.reserve((data.size()+2)/3*4);
    for(size_t i=0;i<data.size();i+=3) {
        uint32_t block=data[i]<<16; int rest=static_cast<int>(data.size()-i);
        if(rest>1) block|=data[i+1]<<8; if(rest>2) block|=data[i+2];
        out.push_back(alphabet[(block>>18)&63]);
        out.push_back(alphabet[(block>>12)&63]);
        out.push_back(rest>1?alphabet[(block>>6)&63]:L'=');
        out.push_back(rest>2?alphabet[block&63]:L'=');
    }
    return out;
}
bool base64Decode(const std::wstring& text, std::vector<uint8_t>& out) {
    out.clear();
    if(text.size()%4!=0) return false;
    for(size_t i=0;i<text.size();i+=4) {
        int c0=decodeChar(text[i]),c1=decodeChar(text[i+1]);
        if(c0<0||c1<0) return false;
        bool pad2=text[i+2]==L'=',pad3=text[i+3]==L'=';
        int c2=pad2?0:decodeChar(text[i+2]),c3=pad3?0:decodeChar(text[i+3]);
        if((!pad2&&c2<0)||(!pad3&&c3<0)||(pad2&&!pad3)) return false;
        if((pad2||pad3) && i+4!=text.size()) return false;
        if((pad2 && (c1&15)) || (pad3 && !pad2 && (c2&3))) return false;
        uint32_t block=(c0<<18)|(c1<<12)|(c2<<6)|c3;
        out.push_back(static_cast<uint8_t>((block>>16)&0xff));
        if(!pad2) out.push_back(static_cast<uint8_t>((block>>8)&0xff));
        if(!pad3) out.push_back(static_cast<uint8_t>(block&0xff));
    }
    return true;
}
bool randomBytes(std::vector<uint8_t>& out, size_t count) {
    out.assign(count,0);
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr,out.data(),static_cast<ULONG>(count),BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}
bool derive(const std::wstring& password, const std::vector<uint8_t>& salt,
            uint32_t iterations, uint32_t length, std::vector<uint8_t>& out) {
    if(length==0 || length>64 || iterations==0 || iterations>2000000 || password.size()>MaxPasswordLength || salt.size()>64) return false;
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,password.c_str(),static_cast<int>(password.size()),nullptr,0,nullptr,nullptr);
    if(bytes<=0) return false;
    std::string utf8(static_cast<size_t>(bytes),'\0');
    if(bytes) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,password.c_str(),static_cast<int>(password.size()),utf8.data(),bytes,nullptr,nullptr);
    out.assign(length,0);
    // BCryptDeriveKeyPBKDF2 needs an HMAC-capable PRF handle, so open SHA-256 with the HMAC flag.
    BCRYPT_ALG_HANDLE alg=nullptr;
    if(!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG))) { wipe(utf8); return false; }
    NTSTATUS status=BCryptDeriveKeyPBKDF2(alg,
        reinterpret_cast<PUCHAR>(utf8.data()),static_cast<ULONG>(utf8.size()),
        const_cast<PUCHAR>(salt.data()),static_cast<ULONG>(salt.size()),
        iterations,out.data(),length,0);
    BCryptCloseAlgorithmProvider(alg,0);
    wipe(utf8);
    return BCRYPT_SUCCESS(status);
}
bool constantTimeEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if(a.size()!=b.size() || a.empty()) return false;
    volatile uint8_t diff=0; for(size_t i=0;i<a.size();i++) diff=static_cast<uint8_t>(diff | (a[i]^b[i]));
    return diff==0;
}
namespace {
struct Parts {
    uint32_t iterations=0; std::vector<uint8_t> salt,hash; size_t length=0;
    ~Parts() { if(!hash.empty()) SecureZeroMemory(hash.data(),hash.size()); }
};
// "iterations:salt:hash" or "iterations:salt:hash:length". A length that does not
// read as a plausible number is ignored, so it can never make a password unusable.
bool split(const std::wstring& credential, Parts& out) {
    if(credential.size()>256) return false;
    size_t first=credential.find(L':');
    if(first==std::wstring::npos) return false;
    size_t second=credential.find(L':',first+1);
    if(second==std::wstring::npos) return false;
    size_t third=credential.find(L':',second+1);
    if(first==0 || first>7 || !std::all_of(credential.begin(),credential.begin()+first,[](wchar_t c){return c>=L'0' && c<=L'9';})) return false;
    out.iterations=static_cast<uint32_t>(wcstoul(credential.substr(0,first).c_str(),nullptr,10));
    if(out.iterations<250000 || out.iterations>2000000) return false;
    if(!base64Decode(credential.substr(first+1,second-first-1),out.salt) || out.salt.size()!=SaltLength) return false;
    const size_t hashEnd=third==std::wstring::npos?credential.size():third;
    if(!base64Decode(credential.substr(second+1,hashEnd-second-1),out.hash) || out.hash.size()!=HashLength) return false;
    out.length=0;
    if(third!=std::wstring::npos) {
        const std::wstring digits=credential.substr(third+1);
        wchar_t* end=nullptr;
        const unsigned long value=wcstoul(digits.c_str(),&end,10);
        if(!digits.empty() && end && *end==L'\0' && value>=1 && value<=MaxPasswordLength) out.length=value;
    }
    return true;
}
}
std::wstring create(const std::wstring& password) {
    std::vector<uint8_t> salt,hash;
    if(password.empty() || password.size()>MaxPasswordLength) return L"";
    if(!randomBytes(salt,SaltLength) || !derive(password,salt,Iterations,HashLength,hash)) return L"";
    auto result=std::to_wstring(Iterations)+L":"+base64Encode(salt)+L":"+base64Encode(hash)+L":"+std::to_wstring(password.size());
    SecureZeroMemory(hash.data(),hash.size()); return result;
}
bool verify(const std::wstring& password, const std::wstring& credential) {
    Parts parts; std::vector<uint8_t> derived;
    if(!split(credential,parts)) return false;
    if(!derive(password,parts.salt,parts.iterations,static_cast<uint32_t>(parts.hash.size()),derived)) return false;
    const bool ok=constantTimeEqual(derived,parts.hash);
    SecureZeroMemory(derived.data(),derived.size());
    return ok;
}
bool validCredential(const std::wstring& credential) { Parts parts; return split(credential,parts); }
bool needsUpgrade(const std::wstring& credential) { Parts parts; return split(credential,parts) && parts.iterations<Iterations; }
std::wstring protect(const std::wstring& data) {
    if(data.empty() || data.size()>512) return {};
    DATA_BLOB input{static_cast<DWORD>(data.size()*sizeof(wchar_t)),reinterpret_cast<BYTE*>(const_cast<wchar_t*>(data.data()))},output{};
    if(!CryptProtectData(&input,L"Hibernation security v1",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)) return {};
    std::wstring result=L"dpapi1:"+base64Encode(std::vector<uint8_t>(output.pbData,output.pbData+output.cbData));
    SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData);
    return result;
}
bool unprotect(const std::wstring& sealed,std::wstring& data) {
    wipe(data);
    if(sealed.size()>4096 || sealed.rfind(L"dpapi1:",0)!=0) return false;
    std::vector<uint8_t> bytes;
    if(!base64Decode(sealed.substr(7),bytes) || bytes.empty()) return false;
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),bytes.data()},output{};
    if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)) return false;
    const bool ok=output.cbData>0 && output.cbData<=1024 && output.cbData%sizeof(wchar_t)==0;
    if(ok) data.assign(reinterpret_cast<const wchar_t*>(output.pbData),output.cbData/sizeof(wchar_t));
    SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData);
    if(!ok || data.find(L'\0')!=std::wstring::npos) { wipe(data); return false; }
    return true;
}
size_t passwordLength(const std::wstring& credential) {
    Parts parts;
    return split(credential,parts)?parts.length:0;
}
std::wstring withLength(const std::wstring& credential, size_t length) {
    Parts parts;
    if(length<1 || length>MaxPasswordLength || !split(credential,parts) || parts.length) return credential;
    // Drop an unreadable trailing field before writing the real one.
    const size_t third=credential.find(L':',credential.find(L':',credential.find(L':')+1)+1);
    return credential.substr(0,third)+L":"+std::to_wstring(length);
}
}
