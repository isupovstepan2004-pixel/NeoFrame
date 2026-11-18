#include "Core/Logger.h"

#include <Windows.h>
#include <ShlObj.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace nf {
namespace {
std::mutex g_mutex;
std::ofstream g_file;
}

void Logger::Init() {
    std::lock_guard lock(g_mutex);
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) return;
    std::filesystem::path dir = std::filesystem::path(raw) / L"NeoFrame";
    CoTaskMemFree(raw);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    g_file.open(dir / L"neoframe.log", std::ios::app);
}

void Logger::Write(LogLevel level, const std::string& message) {
    std::lock_guard lock(g_mutex);
    if (!g_file.is_open()) return;
    SYSTEMTIME t; GetLocalTime(&t);
    const char* tag = level == LogLevel::Info ? "INFO" : level == LogLevel::Warn ? "WARN" : "ERROR";
    char ts[40];
    sprintf_s(ts, "%04d-%02d-%02d %02d:%02d:%02d.%03d", t.wYear, t.wMonth, t.wDay,
              t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    g_file << ts << " [" << tag << "] " << message << '\n';
    g_file.flush();
}

} // namespace nf
