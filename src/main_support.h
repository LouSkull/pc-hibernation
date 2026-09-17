// Included inside main.cpp's application namespace, after native lifecycle helpers.
void pushLibrary() {
    if(smoke) return;
    std::string data="library:{\"folder\":\""+Bridge::escape(Bridge::narrow(mediaLibrary.folder().wstring()))+"\",\"items\":[";
    bool first=true;
    for(const auto& item:mediaLibrary.items()) {
        std::error_code error;
        if(!first) data+=","; first=false;
        data+="{\"id\":"+std::to_string(item.id)+",\"name\":\""+Bridge::escape(Bridge::narrow(item.name))+"\",\"file\":\""+
            Bridge::escape(Bridge::narrow(item.file))+"\",\"source\":\""+Bridge::escape(Bridge::narrow(item.source))+"\",\"missing\":"+
            (std::filesystem::exists(mediaLibrary.folder()/item.file,error)?"false":"true")+"}";
    }
    appView.post(data+"]}");
}
bool selectingMedia=false;
struct PendingMediaImport { int replace=0; std::wstring name; bool active=false; } pendingMediaImport;
std::wstring chooseMedia() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return {};
    const COMDLG_FILTERSPEC filters[]={
        {L"Videos and animations",L"*.mp4;*.webm;*.gif;*.webp"},
        {L"MP4 video",L"*.mp4"},{L"WebM video",L"*.webm"},
        {L"Animated images",L"*.gif;*.webp"}
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)),filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(L"Choose an animation for Hibernation");
    FILEOPENDIALOGOPTIONS options{};
    if(SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    selectingMedia=true;
    SetForegroundWindow(mainWindow);
    HRESULT shown=dialog->Show(mainWindow);
    selectingMedia=false;
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if(FAILED(shown)) { toast("Windows could not open the file picker."); return {}; }
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR path=nullptr;
    if(FAILED(dialog->GetResult(&item)) || !item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)) || !path) {
        if(path) CoTaskMemFree(path);
        toast("The selected file path could not be read."); return {};
    }
    std::wstring result=path; CoTaskMemFree(path); return result;
}
void completePendingMediaImport() {
    if(!pendingMediaImport.active) return;
    auto pending=pendingMediaImport;
    pendingMediaImport={};
    auto source=chooseMedia();
    appView.focus();
    if(source.empty()) return;
    int added=mediaLibrary.import(pending.name,source,pending.replace);
    if(!added) { toast("Could not copy the file. Check available disk space and file access."); return; }
    settings.mediaId=added;
    save(); pushLibrary(); pushState(); toast("Animation added to the library.");
}
bool mediaCommand(const Bridge::Message& message) {
    if(message.kind.rfind("media",0)!=0) return false;
    if(locked || !screens.empty()) { toast("Stop the screensaver before editing the library."); return true; }
    try {
        int id=0; std::string name;
        if(message.kind=="mediaAdd") name=message.key;
        else if(message.kind=="mediaList") { pushLibrary(); return true; }
        else {
            auto cut=message.key.find(':'); auto raw=message.key.substr(0,cut);size_t used=0;
            id=std::stoi(raw,&used); if(used!=raw.size() || id<=0) throw std::runtime_error("Invalid media ID");
            if(cut!=std::string::npos) name=message.key.substr(cut+1);
        }
        if(message.kind=="mediaAdd" || message.kind=="mediaReplace") {
            auto label=message.kind=="mediaAdd"?Bridge::widen(name):mediaLibrary.find(id).name;
            if(!MediaLibrary::validName(label)) { toast("Enter a name (1–80 characters, no surrounding spaces)."); return true; }
            // Opening a modal Windows dialog from inside WebView2's message callback is
            // unreliable: the browser callback still owns focus. Defer until it returns.
            pendingMediaImport={id,label,true};
            if(!PostMessageW(mainWindow,MediaDialogMessage,0,0)) {
                pendingMediaImport={}; toast("The file picker could not be opened.");
            }
            return true;
        } else if(message.kind=="mediaRename") {
            if(!mediaLibrary.rename(id,Bridge::widen(name))) throw std::runtime_error("Could not save the name.");
        } else if(message.kind=="mediaDelete") {
            if(!mediaLibrary.remove(id)) throw std::runtime_error("Could not delete the library copy. Close its playback first.");
            if(settings.mediaId==id) settings.mediaId=0;
        } else return true;
        save();pushLibrary();pushState(); toast("Library updated.");
    } catch(const std::exception& error) { toast(error.what()); }
    return true;
}
LRESULT CALLBACK debugProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCHITTEST) return HTTRANSPARENT;
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);
        FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(145,235,200));
        auto font=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));r.left+=12;r.top+=8;
        DrawTextW(dc,overlayText.c_str(),-1,&r,DT_LEFT|DT_TOP|DT_NOPREFIX);
        SelectObject(dc,font);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void copyDiagnostics() {
    if(!OpenClipboard(mainWindow)) return;
    auto text=Bridge::widen(lastDiagnostics); const size_t bytes=(text.size()+1)*sizeof(wchar_t);
    HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(data) { auto* memory=GlobalLock(data);if(memory) { memcpy(memory,text.c_str(),bytes);GlobalUnlock(data);
        EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data)) GlobalFree(data);
    } else GlobalFree(data); }
    CloseClipboard();toast("Diagnostics copied.");
}
void updateDiagnostics(ULONGLONG now) {
    if(!diagnostics.sample(settings.debugEnabled)) return;
    if(!screens.empty() && !mediaActive()) {
        bool software=screens.front().renderer->isSoftware();
        if(performance.sample(settings,now-activatedAt,diagnostics.gpuMs,diagnostics.frameMs,software)) { sizeScreens();cadence(); }
    }
    if(mediaActive() && settings.deepRest && settings.mediaRest && !mediaResting && now-activatedAt>=static_cast<ULONGLONG>(settings.restAfter)*1000) {
        mediaResting=true;videoFps=0;videoStats.clear();for(auto& screen:screens) screen.media->post("rest");
    }
    if(screens.empty() && !previewLive()) diagnostics.gpuMs=-1;
    if(!settings.debugEnabled) { if(debugWindow) { DestroyWindow(debugWindow);debugWindow=nullptr; }if(debugCsv.is_open()) debugCsv.close();return; }
    int width=0,height=0; std::string renderer="Idle (no rendering)";
    if(mediaActive()) renderer=mediaResting?"Media deep rest (still frame)":"Media decoder (source FPS; render limits apply to built-in scenes)";
    else if(!screens.empty()) { auto& r=*screens.front().renderer;width=r.renderWidth();height=r.renderHeight();renderer=r.isSoftware()?"D3D11 software (WARP)":"D3D11 hardware"; }
    else if(previewLive()) { renderer="JPEG live preview";width=settings.previewWidth;height=width*9/16; }
    lastDiagnostics=diagnostics.json(screens.empty()?settings.previewFps:performance.fps,performance.quality,renderer,width,height);
    if(!videoStats.empty() && mediaActive()) lastDiagnostics.insert(lastDiagnostics.size()-1,",\"video\":"+videoStats);
    if(!screens.empty()) { runHistory.push_back(lastDiagnostics);if(runHistory.size()>60)runHistory.pop_front(); }
    if(screens.empty() && IsWindowVisible(mainWindow) && !IsIconic(mainWindow)) appView.post("stats:"+lastDiagnostics);
    if(settings.debugOverlay && !screens.empty()) {
        if(!debugWindow) {
            RECT r{};GetWindowRect(screens.front().window,&r);
            debugWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_LAYERED,
                L"HibernationDebug",L"",WS_POPUP,r.left+16,r.top+16,530,115,mainWindow,nullptr,instance,nullptr);
            SetLayeredWindowAttributes(debugWindow,0,215,LWA_ALPHA);ShowWindow(debugWindow,SW_SHOWNOACTIVATE);
        }
        std::wostringstream text;text<<std::fixed<<std::setprecision(1);
        text<<(mediaActive()?(mediaResting?L"Media deep rest (still frame)":std::to_wstring(videoFps)+L" decoded FPS"):(std::to_wstring(diagnostics.fps)+L" FPS / cap "+std::to_wstring(performance.fps)))
            <<L"\nCPU app tree "<<diagnostics.treeCpu<<L"% | PC "<<diagnostics.systemCpu<<L"% | RAM "<<diagnostics.treeMb<<L" MB (sum working sets)"
            <<L"\nGPU frame "<<diagnostics.gpuMs<<L" ms | CPU render "<<diagnostics.frameMs<<L" ms | "<<width<<L" x "<<height
            <<L"\nLate frames "<<diagnostics.lateFrames<<L" | Resolution native | CPU delta "<<(diagnostics.baselineCpu<0?0:diagnostics.treeCpu-diagnostics.baselineCpu)<<L" pp";
        overlayText=text.str();InvalidateRect(debugWindow,nullptr,FALSE);
    } else if(debugWindow) { DestroyWindow(debugWindow);debugWindow=nullptr; }
    if(settings.debugLog && !debugLogFull) {
        if(!debugCsv.is_open()) {
            auto dir=std::filesystem::path(configPath).parent_path()/L"Diagnostics";std::error_code error;std::filesystem::create_directories(dir,error);
            debugCsv.open(dir/L"latest.csv",std::ios::trunc);
            debugCsv<<"seconds,app_cpu,system_cpu,app_working_sets_mb,fps,cpu_frame_ms,gpu_frame_ms,cap,quality,late_frames,video_fps,media_rest\n";
        }
        if(debugCsv) { debugCsv<<now/1000.0<<','<<diagnostics.treeCpu<<','<<diagnostics.systemCpu<<','<<diagnostics.treeMb<<','<<diagnostics.fps<<','<<diagnostics.frameMs<<','<<diagnostics.gpuMs<<','<<performance.fps<<','<<performance.quality<<','<<diagnostics.lateFrames<<','<<(mediaActive()?videoFps:0)<<','<<(mediaResting?1:0)<<'\n';debugCsv.flush(); }
        if(debugCsv.tellp()>5*1024*1024) { debugCsv.close();debugLogFull=true;toast("Diagnostic recording reached 5 MB. Toggle recording to start a new log."); }
    } else if(debugCsv.is_open()) debugCsv.close();
}
