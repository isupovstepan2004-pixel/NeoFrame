#pragma once
#include "Core/Types.h"

namespace nf {

// Persists Settings to %LOCALAPPDATA%\NeoFrame\settings.ini and manages Windows autostart.
class SettingsStore {
public:
    static Settings Load();
    static void Save(const Settings& s);
    static bool SetRunWithWindows(bool enabled);
    static bool IsRunWithWindows();
};

} // namespace nf
