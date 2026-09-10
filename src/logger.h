#pragma once

namespace logger {

bool Initialize(bool enabled);
bool IsEnabled();
void Log(const char* severity, const char* category, const char* format, ...);

}  // namespace logger
