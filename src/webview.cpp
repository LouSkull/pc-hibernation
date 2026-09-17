#include "webview.h"
#include "bridge.h"
// WebView2.h needs the COM plumbing that WIN32_LEAN_AND_MEAN leaves out:
// "interface" comes from combaseapi.h and EventRegistrationToken from eventtoken.h.
#include <objbase.h>
#include <unknwn.h>
#include <eventtoken.h>
#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/event.h>   // Microsoft::WRL::Callback
#include <shlobj.h>
#include <vector>
#ifdef HIBERNATION_WEBVIEW_TEST
#include <iostream>
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct WebView::Impl {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    HWND host{};
    std::wstring html;
    std::vector<std::string> queue;   // messages posted before the page was ready
    bool initialNavigation=true;
    bool failed=false;
    std::wstring documentUri;
};

bool webViewAvailable(std::wstring& version) {
    LPWSTR text=nullptr;
    HRESULT hr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&text);
    if(FAILED(hr) || !text) return false;
    version=text; CoTaskMemFree(text); return true;
}

std::wstring loadHtmlResource(int id) {
    HRSRC found=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!found) return {};
    HGLOBAL handle=LoadResource(nullptr,found);
    DWORD size=SizeofResource(nullptr,found);
    const char* data=static_cast<const char*>(LockResource(handle));
    if(!data || !size) return {};
    // The .html files are authored as UTF-8; skip a byte order mark if present.
    size_t offset=(size>=3 && static_cast<unsigned char>(data[0])==0xEF &&
                   static_cast<unsigned char>(data[1])==0xBB &&
                   static_cast<unsigned char>(data[2])==0xBF) ? 3 : 0;
    return Bridge::widen(std::string(data+offset,size-offset));
}

bool WebView::create(HWND host, const std::wstring& html, std::wstring& error) {
    std::wstring version;
    if(!webViewAvailable(version)) {
        error=L"Microsoft Edge WebView2 Runtime is required but was not found.";
        return false;
    }
    destroy();
    const unsigned requestGeneration=generation;
    impl=new Impl();
    impl->host=host;
    impl->html=html;
    const std::wstring policy=L"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob: https://hibernation-media.local; media-src blob: https://hibernation-media.local; connect-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'\">";
    const auto head=impl->html.find(L"<head>");
    impl->html.insert(head==std::wstring::npos?0:head+6,policy);
    // Keep browser data beside the other app data instead of next to the exe.
    std::wstring dataFolder;
    PWSTR local=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) {
        dataFolder=std::wstring(local)+L"\\Hibernation\\WebView2";
        CoTaskMemFree(local);
    }
    if(!profileFolder.empty()) dataFolder=profileFolder;
    HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,
        dataFolder.empty()?nullptr:dataFolder.c_str(),nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this,requestGeneration,life=std::weak_ptr<int>(lifetime)](HRESULT result,ICoreWebView2Environment* environment)->HRESULT {
                if(life.expired() || generation!=requestGeneration || !impl) return S_OK;
                if(FAILED(result) || !environment) { impl->failed=true; impl->queue.clear(); if(onMessage) onMessage("webviewError:"+std::to_string(static_cast<unsigned>(result)));return result; }
                return environment->CreateCoreWebView2Controller(impl->host,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this,requestGeneration,life=std::weak_ptr<int>(lifetime)](HRESULT created,ICoreWebView2Controller* controller)->HRESULT {
                            if(life.expired() || generation!=requestGeneration || !impl) { if(controller) controller->Close(); return S_OK; }
                            if(FAILED(created) || !controller) { impl->failed=true; impl->queue.clear(); if(onMessage) onMessage("webviewError:"+std::to_string(static_cast<unsigned>(created)));return created; }
                            impl->controller=controller;
                            controller->put_IsVisible(FALSE);
                            // The WebView2 default background is white, which flashes on startup
                            // and behind the page. Paint it in the app's own dark tone instead.
                            ComPtr<ICoreWebView2Controller2> controller2;
                            if(SUCCEEDED(impl->controller.As(&controller2)) && controller2) {
                                COREWEBVIEW2_COLOR dark{255,11,14,19};
                                controller2->put_DefaultBackgroundColor(dark);
                            }
                            impl->controller->get_CoreWebView2(&impl->view);
                            if(!impl->view) return E_FAIL;

                            ComPtr<ICoreWebView2Settings> settings;
                            if(SUCCEEDED(impl->view->get_Settings(&settings)) && settings) {
                                // A local interface: no dev tools, no context menu, no browser chrome.
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                                settings->put_IsBuiltInErrorPageEnabled(FALSE);
                                // Browser shortcuts are an escape hatch on the lock screen: Ctrl+P opens
                                // printing, and its Save dialog is a file browser. None of the pages need
                                // reload, print or find, and a shortcut being recorded must reach the page.
                                ComPtr<ICoreWebView2Settings3> keys;
                                if(SUCCEEDED(settings.As(&keys)) && keys) keys->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                                ComPtr<ICoreWebView2Settings4> forms;
                                if(SUCCEEDED(settings.As(&forms)) && forms) {
                                    forms->put_IsPasswordAutosaveEnabled(FALSE);
                                    forms->put_IsGeneralAutofillEnabled(FALSE);
                                }
                                ComPtr<ICoreWebView2Settings5> pinch;
                                if(SUCCEEDED(settings.As(&pinch)) && pinch) pinch->put_IsPinchZoomEnabled(FALSE);
                                ComPtr<ICoreWebView2Settings6> swipe;
                                if(SUCCEEDED(settings.As(&swipe)) && swipe) swipe->put_IsSwipeNavigationEnabled(FALSE);
                            }
                            // A file dropped on a window would navigate away from the interface.
                            ComPtr<ICoreWebView2Controller4> drops;
                            if(SUCCEEDED(impl->controller.As(&drops)) && drops) drops->put_AllowExternalDrop(FALSE);
                            if(!mediaFolder.empty()) {
                                ComPtr<ICoreWebView2_3> local;
                                if(SUCCEEDED(impl->view.As(&local))) local->SetVirtualHostNameToFolderMapping(
                                    L"hibernation-media.local",mediaFolder.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            }
                            EventRegistrationToken token{};
                            impl->view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [this,requestGeneration,life=std::weak_ptr<int>(lifetime)](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
                                    LPWSTR uri=nullptr; args->get_Uri(&uri);
                                    // NavigateToString is reported as about:blank on some runtimes
                                    // and as a data:text/html URL on others. Only the very first,
                                    // host-initiated load is allowed; all later navigations are denied.
                                    const bool embedded=uri && (!wcscmp(uri,L"about:blank") || wcsncmp(uri,L"data:text/html;",15)==0);
                                    const bool allowed=!life.expired() && generation==requestGeneration && impl && impl->initialNavigation && embedded;
#ifdef HIBERNATION_WEBVIEW_TEST
                                    std::wcerr<<L"Navigation: "<<(uri?std::wstring(uri).substr(0,70):L"(null)")<<L" allowed="<<allowed<<std::endl;
#endif
                                    if(allowed) impl->documentUri=uri;
                                    if(uri) CoTaskMemFree(uri);
                                    if(allowed) impl->initialNavigation=false;
                                    else args->put_Cancel(TRUE);
                                    return S_OK;
                                }).Get(),&token);
                            impl->view->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT { args->put_Cancel(TRUE); return S_OK; }).Get(),&token);
                            impl->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT { args->put_Handled(TRUE); return S_OK; }).Get(),&token);
                            impl->view->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                [](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT { args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; }).Get(),&token);
                            ComPtr<ICoreWebView2_4> downloads;
                            if(SUCCEEDED(impl->view.As(&downloads))) downloads->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
                                [](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT { args->put_Cancel(TRUE); return S_OK; }).Get(),&token);
                            impl->view->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                                [this,requestGeneration,life=std::weak_ptr<int>(lifetime)](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args)->HRESULT {
                                    if(life.expired() || generation!=requestGeneration || !impl) return S_OK;
                                    COREWEBVIEW2_PROCESS_FAILED_KIND kind{}; args->get_ProcessFailedKind(&kind);
                                    // WebView2 restarts GPU and utility processes itself. Only a failure
                                    // of the browser or this document invalidates the password UI.
                                    if(kind!=COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED &&
                                       kind!=COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED &&
                                       kind!=COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) return S_OK;
                                    controllerReady=false;
                                    impl->failed=true; impl->queue.clear();
                                    if(onMessage) onMessage("webviewError:processFailed:"+std::to_string(static_cast<int>(kind)));
                                    return S_OK;
                                }).Get(),&token);
                            impl->view->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this,requestGeneration,life=std::weak_ptr<int>(lifetime)](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT {
                                        if(life.expired() || generation!=requestGeneration || !impl) return S_OK;
                                        LPWSTR source=nullptr; args->get_Source(&source);
                                        const bool trusted=source && (!wcscmp(source,L"about:blank") || (!impl->documentUri.empty() && impl->documentUri==source));
#ifdef HIBERNATION_WEBVIEW_TEST
                                        std::wcerr<<L"Message origin: "<<(source?std::wstring(source).substr(0,70):L"(null)")<<std::endl;
#endif
                                        if(source) CoTaskMemFree(source);
                                        if(!trusted) return S_OK;
                                        LPWSTR raw=nullptr;
                                        if(SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            const size_t length=wcslen(raw);
                                            std::string text=length<=4096?Bridge::narrow(raw):std::string{};
                                            SecureZeroMemory(raw,length*sizeof(wchar_t));
                                            CoTaskMemFree(raw);
                                            if(!text.empty() && onMessage) onMessage(text);
                                            if(!text.empty()) SecureZeroMemory(text.data(),text.size());
                                        }
                                        return S_OK;
                                    }).Get(),&token);
                            impl->view->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this,requestGeneration,life=std::weak_ptr<int>(lifetime)](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
                                        if(life.expired() || generation!=requestGeneration || !impl) return S_OK;
                                        BOOL success=FALSE; args->get_IsSuccess(&success);
#ifdef HIBERNATION_WEBVIEW_TEST
                                        std::cerr<<"Navigation completed: "<<success<<std::endl;
#endif
                                        if(!success) return S_OK; // A blocked navigation must not hide the existing document.
                                        controllerReady=true;
                                        resize(); impl->controller->put_IsVisible(TRUE);
                                        if(impl) for(const auto& queued:impl->queue) post(queued);
                                        if(impl) impl->queue.clear();
                                        if(onReady) onReady();
                                        setActive(active);
                                        return S_OK;
                                    }).Get(),&token);
                            resize();
                            impl->view->NavigateToString(impl->html.c_str());
                            return S_OK;
                        }).Get());
            }).Get());
    if(FAILED(hr)) {
        error=L"WebView2 could not start (0x"+std::to_wstring(static_cast<unsigned>(hr))+L").";
        destroy();
        return false;
    }
    return true;
}

void WebView::post(const std::string& message) {
    if(!impl || impl->failed) return;
    if(!controllerReady || !impl->view) {
        if(impl->queue.size()>=32) impl->queue.erase(impl->queue.begin());
        impl->queue.push_back(message); return;
    }
    // Strings cross as JSON so the page receives them verbatim through event.data.
    std::wstring wide=Bridge::widen("\""+Bridge::escape(message)+"\"");
    impl->view->PostWebMessageAsJson(wide.c_str());
}

void WebView::resize() {
    if(!impl || !impl->controller) return;
    RECT bounds{};
    GetClientRect(impl->host,&bounds);
    impl->controller->put_Bounds(bounds);
}

void WebView::focus() {
    if(impl && impl->controller) impl->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void WebView::destroy() {
    ++generation;
    controllerReady=false;
    if(impl) {
        if(impl->controller) impl->controller->Close();
        delete impl;
        impl=nullptr;
    }
}

void WebView::setActive(bool value) {
    const bool changed=active!=value; active=value;
    if(!impl || !impl->controller || !controllerReady) return;
    if(changed) post(value?"activity:1":"activity:0");
    impl->controller->put_IsVisible(value?TRUE:FALSE);
    ComPtr<ICoreWebView2_3> view;
    if(FAILED(impl->view.As(&view))) return;
    if(value) { view->Resume(); return; }
    // Visibility can change while suspension is completing. Resume on that race.
    const unsigned requested=generation;
    if(changed || !value) view->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>(
        [this,requested,life=std::weak_ptr<int>(lifetime)](HRESULT,BOOL)->HRESULT {
            if(!life.expired() && generation==requested && impl && active) setActive(true);
            return S_OK;
        }).Get());
}
