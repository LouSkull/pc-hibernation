#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;
namespace {
std::atomic<bool> found=false;
BOOL CALLBACK closePicker(HWND window,LPARAM processValue) {
    DWORD process=0;GetWindowThreadProcessId(window,&process);
    if(process!=static_cast<DWORD>(processValue) || !IsWindowVisible(window)) return TRUE;
    wchar_t title[256]{};GetWindowTextW(window,title,256);
    if(wcsstr(title,L"Choose an animation for Hibernation")) {
        found=true;PostMessageW(window,WM_CLOSE,0,0);return FALSE;
    }
    return TRUE;
}
}
int wmain() {
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
    ComPtr<IFileOpenDialog> dialog;
    HRESULT created=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(FAILED(created)) { CoUninitialize();return 2; }
    const COMDLG_FILTERSPEC filters[]={
        {L"Videos and animations",L"*.mp4;*.webm;*.gif;*.webp"},
        {L"MP4 video",L"*.mp4"},{L"WebM video",L"*.webm"},
        {L"Animated images",L"*.gif;*.webp"}
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)),filters);
    dialog->SetTitle(L"Choose an animation for Hibernation");
    FILEOPENDIALOGOPTIONS options{};dialog->GetOptions(&options);
    dialog->SetOptions(options|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    std::thread closer([] {
        for(int attempt=0;attempt<100 && !found;attempt++) {
            EnumWindows(closePicker,static_cast<LPARAM>(GetCurrentProcessId()));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    HRESULT shown=dialog->Show(nullptr);
    closer.join();CoUninitialize();
    if(!found) { std::cerr<<"The system file picker never became visible.\n";return 3; }
    if(shown!=HRESULT_FROM_WIN32(ERROR_CANCELLED)) { std::cerr<<"Unexpected dialog result.\n";return 4; }
    std::cout<<"PASS: native animation file picker became visible and was cancelled cleanly.\n";
    return 0;
}
