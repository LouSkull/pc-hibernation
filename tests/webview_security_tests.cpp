#include "../src/webview.h"
#include <objbase.h>
#include <filesystem>
#include <iostream>
namespace {
WebView view; bool failed=false,fetchBlocked=false,survived=false;
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_TIMER) { failed=true; DestroyWindow(window); return 0; }
    if(message==WM_DESTROY) { view.destroy(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,wp,lp);
}
}
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    wchar_t temp[MAX_PATH]{},name[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp); GetTempFileNameW(temp,L"hws",0,name); DeleteFileW(name);
    view.setProfileFolder(name);
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"HibernationWebSecurityTest"; wc.lpfnWndProc=proc;
    RegisterClassW(&wc);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,0,0,320,180,nullptr,nullptr,wc.hInstance,nullptr);
    view.setMessageHandler([window](const std::string& message) {
        std::cout<<"Message: "<<message<<std::endl;
        if(message=="fetchBlocked") fetchBlocked=true;
        else if(message=="started") {}
        else if(message=="survived") { survived=true; PostMessageW(window,WM_CLOSE,0,0); }
        else { std::cerr<<"Unexpected bridge message: "<<message<<'\n'; failed=true; }
    });
    view.setReadyHandler([] { std::cout<<"Document ready"<<std::endl; });
    std::wstring error;
    const wchar_t* html=LR"html(<html><head></head><body><script>
      const send=m=>chrome.webview.postMessage(m);
      send('started');
      fetch('https://hibernation-network-test.invalid/').then(()=>send('networkAllowed')).catch(()=>send('fetchBlocked'));
      const frame=document.createElement('iframe'); frame.srcdoc='<script>chrome.webview.postMessage("frameEscaped")<\/script>'; document.body.append(frame);
      window.open('https://hibernation-network-test.invalid/');
      setTimeout(()=>location.href='https://hibernation-network-test.invalid/',100);
      setTimeout(()=>location.href='about:blank',400);
      setTimeout(()=>send('survived'),800);
    </script></body></html>)html";
    if(!window || !view.create(window,html,error)) { std::wcerr<<error; view.destroy(); if(window) DestroyWindow(window); CoUninitialize(); return 1; }
    ShowWindow(window,SW_SHOWNOACTIVATE); SetTimer(window,1,10000,nullptr);
    MSG message{}; while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoUninitialize();
    // WebView may still hold its disposable profile while its child process exits.
    std::error_code ignored; std::filesystem::remove_all(name,ignored);
    if(!failed && survived && fetchBlocked) { std::cout<<"Native WebView navigation, frame and network confinement passed\n"; return 0; }
    std::cerr<<"WebView confinement failed or timed out\n"; return 1;
}
