#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <memory>
// Minimal WebView2 host: puts one embedded HTML document inside a native window
// and carries plain-text messages both ways. Creation is asynchronous, so the
// caller gets a callback once the page is live.
class WebView {
public:
    using MessageHandler = std::function<void(const std::string&)>;
    using ReadyHandler = std::function<void()>;
    // Starts creating the view inside `host`. `html` is the full document text.
    // Returns false when WebView2 is unavailable; `error` then explains why.
    bool create(HWND host, const std::wstring& html, std::wstring& error);
    void setMessageHandler(MessageHandler handler) { onMessage = std::move(handler); }
    void setReadyHandler(ReadyHandler handler) { onReady = std::move(handler); }
    // Sends one message to the page; queued until the page is ready.
    void post(const std::string& message);
    // Keeps the view filling the host window.
    void resize();
    void focus();
    void setActive(bool active);
    void setProfileFolder(const std::wstring& folder) { profileFolder=folder; }
    void setMediaFolder(const std::wstring& folder) { mediaFolder=folder; }
    bool ready() const { return controllerReady; }
    void destroy();
    ~WebView() { destroy(); }
private:
    struct Impl;
    Impl* impl = nullptr;
    MessageHandler onMessage;
    ReadyHandler onReady;
    bool controllerReady = false;
    unsigned generation = 0;
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    bool active=true;
    std::wstring mediaFolder,profileFolder;
    friend struct Impl;
};
// True when a WebView2 runtime is installed on this machine.
bool webViewAvailable(std::wstring& version);
// Loads one embedded RCDATA document as UTF-16 text.
std::wstring loadHtmlResource(int id);
