#pragma once

#include <string>

namespace mausely {

// Minimal thread-safe logger: stderr + optional file (%APPDATA%\Mausely\mausely.log).
void logInit(const std::wstring& filePath);
void logInfo(const std::string& msg);
void logError(const std::string& msg);

// UTF-8 <-> UTF-16 helpers used across the Windows code.
std::string toUtf8(const std::wstring& w);
std::wstring toWide(const std::string& s);

// Directory that contains the running executable (with trailing backslash).
std::wstring exeDirectory();
// %APPDATA%\Mausely\ (created on demand, trailing backslash).
std::wstring appDataDirectory();

}  // namespace mausely
