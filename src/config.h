#pragma once
#include "core.h"
#include <string>
Settings readSettings(const std::wstring& path,const Settings& defaults=Settings{},bool includeSecurity=true);
bool writeSettings(const std::wstring& path,const Settings& settings,bool includeSecurity=true);
void copyAppearance(Settings& to,const Settings& from);
