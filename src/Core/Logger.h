#pragma once
#include <string>

namespace nf {

enum class LogLevel { Info, Warn, Error };

// Thread-safe file logger: %LOCALAPPDATA%\NeoFrame\neoframe.log
class Logger {
public:
    static void Init();
    static void Write(LogLevel level, const std::string& message);
    static void Info(const std::string& m)  { Write(LogLevel::Info, m); }
    static void Warn(const std::string& m)  { Write(LogLevel::Warn, m); }
    static void Error(const std::string& m) { Write(LogLevel::Error, m); }
};

} // namespace nf
