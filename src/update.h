#pragma once
#include <string>
// Update check against the GitHub Releases API. The comparison and JSON parsing are pure
// and unit-tested; only fetch() touches the network (WinHTTP, HTTPS to api.github.com).
namespace Update {
struct Result {
    bool ok = false;           // the request completed and a release was found
    bool newer = false;        // the latest release is newer than the running build
    std::wstring latest;       // latest version, e.g. "1.2.0"
    std::wstring current;      // the running build's version
    std::wstring url;          // release page to open in the browser
    std::wstring notes;        // short release description, if any
    std::wstring error;        // set when ok is false
};
// -1 if a<b, 0 if equal, 1 if a>b. Parses up to four dotted numbers; a leading 'v' and any
// pre-release suffix ("-beta") are ignored. A pre-release of the same numbers ranks lower.
int compare(const std::string& a, const std::string& b);
// Pulls tag_name / html_url / body out of a GitHub "releases/latest" JSON body and fills the
// result against currentVersion. Returns false (with result.error) for a 404 or missing tag.
bool parseRelease(const std::string& json, const std::wstring& currentVersion, Result& out);
// Blocking HTTPS GET of the latest release, then parseRelease. Run it off the UI thread.
Result fetch(const std::wstring& owner, const std::wstring& repo, const std::wstring& currentVersion);
// Minimal helpers, exposed for tests.
std::string jsonString(const std::string& json, const std::string& key);
bool safeReleaseUrl(const std::wstring& url,const std::wstring& owner,const std::wstring& repo);
}
