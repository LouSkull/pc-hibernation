#pragma once
#include "core.h"
#include <cwctype>
#include <string_view>

enum class MediaSource { Other, Browser, Player };
enum MediaSignal : unsigned { NoMedia=0, VideoMedia=1, BrowserMedia=2, OtherMedia=4 };

inline MediaSource mediaSource(std::wstring_view identity) {
    const auto separator=identity.find_last_of(L"\\/");
    if(separator!=std::wstring_view::npos) identity.remove_prefix(separator+1);
    std::wstring name(identity);
    for(auto& ch:name) ch=static_cast<wchar_t>(std::towlower(ch));
    // Executable names and media-session app IDs (including profile/channel suffixes).
    const auto matches=[&](std::wstring_view id) {
        return name==id || (name.starts_with(id) && name.size()>id.size() &&
            (name[id.size()]==L'.' || name[id.size()]==L'_' || name[id.size()]==L'!'));
    };
    for(auto id:{L"chrome",L"msedge",L"firefox",L"brave",L"opera",L"operagx",L"browser",L"vivaldi",
                 L"google.chrome",L"microsoft.msedge",L"microsoft.microsoftedge",L"mozilla.firefox",
                 L"bravesoftware.brave-browser",L"yandex.browser"})
        if(matches(id)) return MediaSource::Browser;
    for(auto id:{L"vlc",L"videolan.vlc",L"mpv",L"mpc-hc64",L"mpc-hc",L"mpc-be64",L"mpc-be",
                 L"potplayermini64",L"potplayermini",L"wmplayer",L"microsoft.media.player",L"microsoft.zunevideo"})
        if(matches(id)) return MediaSource::Player;
    return MediaSource::Other;
}

inline unsigned mediaSignal(MediaSource source,bool explicitlyVideo=false) {
    if(explicitlyVideo || source==MediaSource::Player) return VideoMedia;
    return source==MediaSource::Browser?BrowserMedia:OtherMedia;
}

inline Activity classifyActivity(unsigned playing,MediaSource foreground,bool fullscreen,bool conservative) {
    // Browser playback often has no Video type. Keep its source separate from game
    // audio, and give detected playback precedence even when the desktop has focus.
    if((playing&(VideoMedia|BrowserMedia)) || foreground==MediaSource::Player) return Activity::Video;
    // Fullscreen browsers may show muted video without publishing any media session.
    if(fullscreen) return foreground==MediaSource::Browser?Activity::Video:Activity::Game;
    if(conservative && (playing&OtherMedia)) return Activity::Video;
    return Activity::Desktop;
}
