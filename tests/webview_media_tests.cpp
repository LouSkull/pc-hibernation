#include "../src/webview.h"
#include "../src/bridge.h"
#include "../src/resource.h"
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <iostream>
namespace { bool failed=true; WebView player; int stage=0;
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_TIMER) { DestroyWindow(w);return 0; }
    if(m==WM_DESTROY) { player.destroy();PostQuitMessage(0);return 0; }
    if(m==WM_SIZE) { player.resize();return 0; }
    return DefWindowProcW(w,m,wp,lp);
}}
int wmain(int argc,wchar_t** argv) {
    if(argc<2) return 2;
    const std::wstring filename=argc>2?argv[2]:L"test.mp4";
    if(!std::filesystem::exists(std::filesystem::path(argv[1])/filename))return 2;
    const bool image=filename.ends_with(L".gif")||filename.ends_with(L".webp");
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"HibernationMediaTest";wc.lpfnWndProc=proc;RegisterClassW(&wc);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Media playback test",WS_POPUP,0,0,320,180,nullptr,nullptr,wc.hInstance,nullptr);
    player.setProfileFolder((std::filesystem::absolute(argv[1])/L"WebView-test-profile").wstring());
    player.setMediaFolder(std::filesystem::absolute(argv[1]).wstring());
    player.setMessageHandler([window,image](const std::string& message){
        std::cerr<<"Message: "<<message<<std::endl;
        if(message=="mediaError") { std::cerr<<"Decoder error\n";PostMessageW(window,WM_CLOSE,0,0); }
        if((message.rfind("videoStats:",0)==0 || (image && message=="mediaLoaded")) && stage==0) {
            std::cout<<message<<'\n';stage=1;player.post("rest");
        } else if(message=="rested" && stage==1) {
            failed=false;std::cout<<"Mapped media playback and deep rest passed\n";PostMessageW(window,WM_CLOSE,0,0);
        }
    });
    player.setReadyHandler([filename]{std::cerr<<"Native view ready"<<std::endl;player.post("media:{\"url\":\"https://hibernation-media.local/"+Bridge::escape(Bridge::narrow(filename))+"\",\"fit\":0,\"debug\":true,\"clock\":false,\"clockSize\":100,\"clockOpacity\":80,\"clockPosition\":0}");});
    std::wstring error;if(!player.create(window,loadHtmlResource(IDR_MEDIA_HTML),error)) {std::wcerr<<error;DestroyWindow(window);CoUninitialize();return 1;}
    ShowWindow(window,SW_SHOWNOACTIVATE);SetTimer(window,1,15000,nullptr);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    CoUninitialize();return failed?1:0;
}
