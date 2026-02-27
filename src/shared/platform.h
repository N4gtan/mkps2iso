#pragma once

#include <filesystem>
#include <optional>

namespace fs { using namespace std::filesystem; }

#ifdef _WIN32
#define stat64 _stat64
#define SYSTEM_TIMEZONE _timezone
#else
#include <sys/stat.h>
#define SYSTEM_TIMEZONE timezone
#if defined(__APPLE__) || (defined(__linux__) && defined(__aarch64__))
// Apple uses 64-bit stat by default since macOS 10.6 on all architectures.
// AArch64 Linux was built as a 64-bit system by default and also lacks stat64.
#define stat64 stat
#endif
#endif

FILE *OpenFile(const fs::path &filename, const char *mode);
int SeekFile(FILE *file, int64_t offset, int origin);
std::optional<struct stat64> Stat(const fs::path &path);
int64_t GetSize(const fs::path &path);
void UpdateTimestamps(const fs::path &path, struct tm timestamp);
time_t MakeTime(struct tm *timeBuf);
struct tm *LocalTime(const time_t *timeSec);
