#include "Settings/Settings.h"
#include "Core/Logger.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace nf {

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::filesystem::path SettingsPath() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) return {};
    std::filesystem::path dir = std::filesystem::path(raw) / L"NeoFrame";
    CoTaskMemFree(raw);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / L"settings.ini";
}
}

Settings SettingsStore::Load() {
    Settings s;
    auto path = SettingsPath();
    if (!path.empty()) {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            int n = 0;
            try { n = std::stoi(v); } catch (...) { continue; }
            if (k == "fgEnabled") s.fgEnabled = n != 0;
            else if (k == "multiplier") s.multiplier = std::clamp(n, 2, 3);
            else if (k == "algorithm") s.algorithm = std::clamp(n, 0, 1);
            else if (k == "quality") s.quality = std::clamp(n, 0, 2);
            else if (k == "targetFps") s.targetFps = std::clamp(n, 0, 360);
            else if (k == "captureCursor") s.captureCursor = n != 0;
            else if (k == "allowTearing") s.allowTearing = n != 0;
        }
    }
    s.runWithWindows = IsRunWithWindows();
    return s;
}

void SettingsStore::Save(const Settings& s) {
    auto path = SettingsPath();
    if (path.empty()) return;
    std::ofstream f(path, std::ios::trunc);
    if (!f) { Logger::Warn("Cannot write settings"); return; }
    f << "fgEnabled=" << s.fgEnabled << "\nmultiplier=" << s.multiplier << "\nalgorithm=" << s.algorithm
      << "\nquality=" << s.quality << "\ntargetFps=" << s.targetFps << "\ncaptureCursor=" << s.captureCursor
      << "\nallowTearing=" << s.allowTearing << "\n";
}

bool SettingsStore::SetRunWithWindows(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    bool ok = true;
    if (enabled) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(path) + L"\"";
        ok = RegSetValueExW(key, L"NeoFrame", 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                            static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LONG r = RegDeleteValueW(key, L"NeoFrame");
        ok = r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

bool SettingsStore::IsRunWithWindows() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    bool present = RegQueryValueExW(key, L"NeoFrame", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return present;
}

} // namespace nf
