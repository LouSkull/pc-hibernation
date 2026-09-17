// Verifies that the statically linked WebView2 loader resolves and that a
// WebView2 runtime is installed, which the HTML interface depends on.
#include <windows.h>
// WebView2.h needs the COM plumbing that WIN32_LEAN_AND_MEAN leaves out:
// "interface" comes from combaseapi.h and EventRegistrationToken from eventtoken.h.
#include <objbase.h>
#include <unknwn.h>
#include <eventtoken.h>
#include <WebView2.h>
#include <iostream>
int main() {
    LPWSTR version=nullptr;
    HRESULT hr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);
    if(FAILED(hr) || !version) {
        std::cerr<<"WebView2 runtime missing (hr=0x"<<std::hex<<static_cast<unsigned>(hr)<<")\n";
        return 1;
    }
    char narrow[128]{};
    WideCharToMultiByte(CP_UTF8,0,version,-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
    CoTaskMemFree(version);
    std::cout<<"WebView2 runtime "<<narrow<<"\n";
    return 0;
}
