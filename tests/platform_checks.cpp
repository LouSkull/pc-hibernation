#include "../src/platform.h"
#include <iostream>
#include <cwchar>
int wmain(int argc,wchar_t** argv) {
    if(argc>2 && !wcscmp(argv[1],L"--audio-guard")) return audioGuardianMain(static_cast<DWORD>(wcstoul(argv[2],nullptr,10)));
    if(argc>1 && !wcscmp(argv[1],L"--audio-owner-test")) return audioOwnerTest();
    if(argc>1 && !wcscmp(argv[1],L"--audio-self-test")) { int result=audioSelfTest(); std::cout<<"Audio mute, restore, owner crash recovery: "<<(result?"FAIL":"PASS")<<'\n'; return result; }
    int audio=audioProbe(),media=mediaProbe();
    std::cout<<"Audio endpoint access: "<<(audio?"FAIL":"PASS")<<"\nMedia detection backend: "<<(media?"FAIL":"PASS")<<'\n';
    return audio||media;
}
