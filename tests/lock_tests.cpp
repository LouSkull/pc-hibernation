#include "../src/lock.h"
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* name) { if(!ok) { std::cerr<<name<<'\n'; std::exit(1); } }
int main() {
    // Base64 round-trips, including the two padding cases and the empty input.
    for(const std::vector<uint8_t> sample:{std::vector<uint8_t>{},std::vector<uint8_t>{0},std::vector<uint8_t>{0,255},
        std::vector<uint8_t>{1,2,3},std::vector<uint8_t>{9,8,7,6,5,4,3,2,1,0}}) {
        std::vector<uint8_t> back; check(Lock::base64Decode(Lock::base64Encode(sample),back),"base64 decodes");
        check(back==sample,"base64 round-trip");
    }
    std::vector<uint8_t> junk;
    check(!Lock::base64Decode(L"abc",junk),"base64 rejects bad length");
    check(!Lock::base64Decode(L"****",junk),"base64 rejects bad characters");
    check(!Lock::base64Decode(L"AA==AAAA",junk),"base64 rejects interior padding");
    check(!Lock::base64Decode(L"AB==",junk),"base64 rejects noncanonical padding bits");

    // PBKDF2 determinism and salt sensitivity (small iteration count keeps the test fast).
    std::vector<uint8_t> salt{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    std::vector<uint8_t> other{16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1};
    std::vector<uint8_t> a,b,c;
    check(Lock::derive(L"correct horse",salt,2000,32,a),"derive succeeds");
    check(Lock::derive(L"correct horse",salt,2000,32,b),"derive succeeds again");
    check(Lock::derive(L"correct horse",other,2000,32,c),"derive with other salt");
    check(a.size()==32 && a==b,"derive is deterministic");
    check(a!=c,"salt changes the hash");

    check(Lock::constantTimeEqual(a,b),"equal hashes compare equal");
    check(!Lock::constantTimeEqual(a,c),"different hashes compare unequal");
    check(!Lock::constantTimeEqual(std::vector<uint8_t>{},std::vector<uint8_t>{}),"empty compares unequal");

    // Full credential lifecycle at production iteration count.
    std::wstring cred=Lock::create(L"Пароль 42!");
    check(!cred.empty(),"credential created");
    check(cred.rfind(L"600000:",0)==0 && !Lock::needsUpgrade(cred),"current KDF work factor");
    check(Lock::needsUpgrade(L"250000"+cred.substr(cred.find(L':'))),"old work factor upgrades after verification");
    check(!Lock::validCredential(L"2000001"+cred.substr(cred.find(L':'))),"unbounded KDF cost rejected");
    check(!Lock::validCredential(L"+600000"+cred.substr(cred.find(L':'))),"nonnumeric work factor rejected");
    check(!Lock::validCredential(L"600000:AA==:AA=="),"short salt and hash rejected");
    check(cred.find(L':')!=std::wstring::npos && cred.rfind(L':')!=cred.find(L':'),"credential has two separators");
    check(Lock::verify(L"Пароль 42!",cred),"correct password verifies");
    check(!Lock::verify(L"пароль 42!",cred),"password is case sensitive");
    check(!Lock::verify(L"",cred),"empty password rejected");
    check(!Lock::verify(L"anything",L"not-a-credential"),"malformed credential rejected");
    check(Lock::create(L"").empty(),"empty password refused");

    // The stored length lets the lock screen stop input and submit by itself.
    check(Lock::passwordLength(cred)==10,"credential records the password length");
    const std::wstring legacy=cred.substr(0,cred.rfind(L':'));
    check(Lock::passwordLength(legacy)==0 && Lock::verify(L"Пароль 42!",legacy),"older credentials without a length still verify");
    check(Lock::withLength(legacy,10)==cred,"a missing length is added");
    check(Lock::withLength(cred,3)==cred,"an existing length is never replaced");
    check(Lock::withLength(legacy,0)==legacy && Lock::withLength(legacy,129)==legacy,"impossible lengths are not written");
    check(Lock::passwordLength(legacy+L":abc")==0 && Lock::verify(L"Пароль 42!",legacy+L":abc"),"a junk length is ignored");
    check(Lock::passwordLength(legacy+L":999")==0 && Lock::passwordLength(legacy+L":")==0,"an impossible length is ignored");
    check(Lock::withLength(legacy+L":abc",10)==cred,"a junk length is replaced");
    check(!Lock::verify(L"Пароль 42!",L"250000:"+legacy.substr(legacy.find(L':')+1,legacy.rfind(L':')-legacy.find(L':'))+L":10"),"a missing hash is rejected");

    const auto sealed=Lock::protect(cred);
    std::wstring opened;
    check(!sealed.empty() && sealed.find(cred)==std::wstring::npos,"DPAPI envelope hides verifier");
    check(sealed!=Lock::protect(cred),"DPAPI encryption is randomized");
    check(Lock::unprotect(sealed,opened) && opened==cred,"DPAPI user roundtrip");
    std::vector<uint8_t> blob; check(Lock::base64Decode(sealed.substr(7),blob),"decode protected blob");
    blob[blob.size()/2]^=1;
    check(!Lock::unprotect(L"dpapi1:"+Lock::base64Encode(blob),opened) && opened.empty(),"tampered DPAPI data rejected");
    check(!Lock::unprotect(cred,opened),"plaintext is not accepted as a DPAPI envelope");
    // The shouldLock policy itself lives in core_tests, next to the other pure logic.
    std::cout<<"lock checks passed\n";
}
