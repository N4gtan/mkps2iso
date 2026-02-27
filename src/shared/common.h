#pragma once

#include "layout.h"
#include <filesystem>
#include <optional>
#include <cstring>

namespace fs { using namespace std::filesystem; }

enum class EntryType
{
    EntryFile,
    EntryDir,
    EntryDummy
};

struct EntryAttributes
{
    int8_t GMTOffs = 36;
    uint8_t HFLAG = 0;
    int32_t ORDER = 0;
    uint32_t LINKC = 0;
    uint32_t LBAFRC = 0;
    //uint8_t XAAttrib = 0;
    //uint16_t XAPerm = 0;
    //uint16_t GID = 0;
    //uint16_t UID = 0;
};

struct PathTableEntry
{
    ISO_PATHTABLE_ENTRY data;
    std::string identifier;
};

// Unified entry structure for MKPS2ISO and DUMPS2ISO
namespace iso { class DirTree; }
struct Entry
{
    size_t size;            // In bytes
    uint8_t hf;             // Hidden Flag
    int32_t order;          // Custom FID/DirRecord order
    uint32_t flc;           // File Link Count (CDVDGEN had a bug when reloading projects that grew this value infinitely)
    uint32_t lba;           // Logical Block Address (in sectors)
    uint32_t lbaICB;        // Information Control Block (in sectors)
    uint32_t lbaISO;        // ISO LBA (only for directories)

    fs::path path;          // Empty if dummy
    EntryType type;         // 0: file, 1: directory, 2: dummy
    std::string identifier; // Empty if root or dummy
    ISO_DATESTAMP date;     // In ISO 9660 format

    std::unique_ptr<iso::DirTree> subdir; // Only for directories

    //uint8_t attribs;        // XA attributes, 0xFF is not set
    //uint16_t perms;         // XA permissions
    //uint16_t GID;           // Owner group ID
    //uint16_t UID;           // Owner user ID
};

// Datestamp manipulation helpers
timestamp DateStampToTimeStamp(const ISO_DATESTAMP &date);
ISO_DATESTAMP TimeStampToDateStamp(const timestamp &modificationTime);
std::string DateToString(const ISO_DATESTAMP &src, bool ext);
std::string LongDateToString(const ISO_LONG_DATESTAMP &src);
bool ParseDateFromString(ISO_DATESTAMP &result, const char *str, char defaultGMT = 36);
bool ParseLongDateFromString(ISO_LONG_DATESTAMP &result, const char *str, char defaultGMT = 36);
ISO_LONG_DATESTAMP GetUnspecifiedLongDate();

// Sector conversion helper
template <size_t N = DVD_SECTOR_SIZE>
constexpr uint32_t GetSizeInSectors(const size_t size)
{
    return size > 0 ? static_cast<uint32_t>((size - 1) / N + 1) : 1;
}

// Byte alignment helper
template <uint32_t N>
constexpr uint32_t AlignTo(const uint32_t val)
{
    static_assert((N > 0) && ((N & (N - 1)) == 0), "Alignment N must be a power of 2");
    return (val + (N - 1)) & ~(N - 1);
}

// Endianness swap helpers
uint16_t SwapBytes16(const uint16_t val);
uint32_t SwapBytes32(const uint32_t val);

// Scoped helper for a few resources
struct file_deleter
{
    void operator()(FILE *file) const
    {
        if (file != nullptr)
            fclose(file);
    }
};
using unique_file = std::unique_ptr<FILE, file_deleter>;
unique_file OpenScopedFile(const fs::path &path, const char *mode);

// String manipulation helper
bool CompareICase(std::string_view strLeft, std::string_view strRight);

// Argument parsing helpers
bool ParseArgument(char **argv, std::string_view command, std::string_view longCommand = std::string_view{});
std::optional<fs::path> ParsePathArgument(char **&argv, std::string_view command, std::string_view longCommand = std::string_view{});
std::optional<std::string> ParseStringArgument(char **&argv, std::string_view command, std::string_view longCommand = std::string_view{});
