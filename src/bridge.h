#pragma once
#include "core.h"
#include <string>
// Message and state bridge between the native host and the HTML interface.
// Pure text in, pure text out, so it can be unit tested without a window.
namespace Bridge {
// Live state the page renders but never persists.
struct Status {
    int monitors = 1;
    bool hasPreset = false;
    bool hotkeysOk = true;
    bool mediaAvailable = false;
    bool running = false;
    bool autoStart = false;          // the HKCU Run entry actually exists (the truth on disk)
    std::string context = "Desktop";
    std::string version = "0.0.0";   // build version for the About page
    std::string message;
};
// Serialises settings plus status for the page. The stored credential is never
// sent to the page; only whether one exists.
std::string toJson(const Settings& settings, const Status& status);
// Inbound page message. Wire format:
//   set:<key>:<value>   one setting ("value" is the rest of the text)
//   act:<name>          a command such as launch, shuffle, exit
//   pw:<password>       set a new password
//   try:<password>      unlock attempt
//   nav:<tab>           the page moved to another tab
struct Message { std::string kind, key, value; };
Message parse(const std::string& raw);
// Applies one "set" message. Returns false when the key is unknown.
bool applySetting(Settings& settings, const std::string& key, const std::string& value);
std::string escape(const std::string& text);
std::string narrow(const std::wstring& text);
std::wstring widen(const std::string& text);
}
