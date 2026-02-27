#include "common.h"
#include "platform.h"

timestamp DateStampToTimeStamp(const ISO_DATESTAMP &date)
{
    uint16_t offsetMinutes = static_cast<uint16_t>(date.GMToffs * 15) & 0x0FFF;

    return timestamp{
        .typeAndTimezone = static_cast<uint16_t>(TIMESTAMP_TYPE_LOCAL | offsetMinutes),
        .year    = static_cast<uint16_t>(date.year + 1900),
        .month   = date.month,
        .day     = date.day,
        .hour    = date.hour,
        .minute  = date.minute,
        .second  = date.second
    };
}

ISO_DATESTAMP TimeStampToDateStamp(const timestamp &modificationTime)
{
    int16_t offsetMinutes = modificationTime.typeAndTimezone & TIMESTAMP_TIMEZONE_MASK;

    // Check if the sign bit (bit 11) is set
    if (offsetMinutes & 0x0800)
    {
        if (offsetMinutes == TIMESTAMP_TIMEZONE_UNDEFINED)
            offsetMinutes = 0; // Default to GMT 0
        else
            offsetMinutes |= 0xF000; // Extend to full 16 bits
    }

    return ISO_DATESTAMP {
        .year    = static_cast<uint8_t>(modificationTime.year - 1900),
        .month   = modificationTime.month,
        .day     = modificationTime.day,
        .hour    = modificationTime.hour,
        .minute  = modificationTime.minute,
        .second  = modificationTime.second,
        .GMToffs = static_cast<int8_t>(offsetMinutes / 15)
    };
}

std::string DateToString(const ISO_DATESTAMP &src, bool ext)
{
    char buf[20];
    snprintf(buf, sizeof(buf), "%04u%02hhu%02hhu%02hhu%02hhu%02hhu",
             src.year + 1900u, src.month, src.day, src.hour, src.minute, src.second);

    if (ext)
        snprintf(buf + 14, 6, "00%+hhd", src.GMToffs);

    return std::string(buf);
}

std::string LongDateToString(const ISO_LONG_DATESTAMP &src)
{
    // Interpret ISO_LONG_DATESTAMP as 16 characters, manually write out GMT offset
    const char *srcStr = reinterpret_cast<const char *>(&src);

    std::string result(srcStr, srcStr + 16);

    char GMTbuf[4];
    snprintf(GMTbuf, sizeof(GMTbuf), "%+hhd", src.GMToffs);
    result.append(GMTbuf);

    return result;
}

bool ParseDateFromString(ISO_DATESTAMP &result, const char *str, char defaultGMT)
{
    if (str == nullptr || strlen(str) < 14)
    {
        result = {.month = 1, .day = 1, .GMToffs = defaultGMT};
        return false;
    }

    unsigned int year;
    const int argsRead = sscanf(str, "%4u%2hhu%2hhu%2hhu%2hhu%2hhu%*2[0-9]%hhd",
                                &year, &result.month, &result.day,
                                &result.hour, &result.minute, &result.second, &result.GMToffs);
    if (argsRead >= 6)
    {
        result.year = year >= 1900 ? year - 1900 : 0;
        if (argsRead < 7)
            result.GMToffs = defaultGMT; // Consider GMToffs optional

        return true;
    }

    result = {.month = 1, .day = 1, .GMToffs = defaultGMT};
    return false;
}

bool ParseLongDateFromString(ISO_LONG_DATESTAMP &result, const char *str, char defaultGMT)
{
    if (!str || strlen(str) < 14)
    {
        result = GetUnspecifiedLongDate();
        return false;
    }

    for (int i = 0; i < 14; ++i)
    {
        if (!isdigit(static_cast<uint8_t>(str[i])))
        {
            result = GetUnspecifiedLongDate();
            return false;
        }
    }

    if (isdigit(static_cast<uint8_t>(str[14])) && isdigit(static_cast<uint8_t>(str[15])))
    {
        memcpy(&result, str, 16);
        result.GMToffs = str[16] != 0 ? static_cast<char>(atoi(str + 16)) : defaultGMT;
    }
    else
    {
        memcpy(&result, str, 14);
        memcpy(result.hsecond, "00", 2);
        result.GMToffs = str[14] != 0 && (str[14] == '+' || str[14] == '-') ? static_cast<char>(atoi(str + 14)) : defaultGMT;
    }

    return true;
}

ISO_LONG_DATESTAMP GetUnspecifiedLongDate()
{
    ISO_LONG_DATESTAMP result;

    memset(&result, '0', sizeof(result) - 1);
    result.GMToffs = 0;

    return result;
}

uint16_t SwapBytes16(const uint16_t val)
{
    return ((val & 0xFF) << 8) |
           ((val & 0xFF00) >> 8);
}

uint32_t SwapBytes32(const uint32_t val)
{
    return ((val & 0xFF) << 24) |
           ((val & 0xFF00) << 8) |
           ((val & 0xFF0000) >> 8) |
           ((val & 0xFF000000) >> 24);
}

unique_file OpenScopedFile(const fs::path &path, const char *mode)
{
    return unique_file{ OpenFile(path, mode) };
}

bool CompareICase(std::string_view strLeft, std::string_view strRight)
{
    return std::equal(strLeft.begin(), strLeft.end(), strRight.begin(), strRight.end(), [](char left, char right)
                      { return left == right || std::tolower((unsigned char)left) == std::tolower((unsigned char)right); });
}

bool ParseArgument(char **argv, std::string_view command, std::string_view longCommand)
{
    const std::string_view arg(*argv);
    // Try the long command first, case insensitively
    if (!longCommand.empty() && arg.length() > 2 && arg[0] == '-' && arg[1] == '-' && CompareICase(arg.substr(2), longCommand))
        return true;

    // Short commands are case sensitive
    if (!command.empty() && arg.length() > 1 && arg[0] == '-' && arg.substr(1) == command)
        return true;

    return false;
}

std::optional<fs::path> ParsePathArgument(char **&argv, std::string_view command, std::string_view longCommand)
{
    if (ParseArgument(argv, command, longCommand))
    {
        if (*(argv + 1) != nullptr && **(argv + 1) != '-')
            argv++;

        return *argv;
    }
    return std::nullopt;
}

std::optional<std::string> ParseStringArgument(char **&argv, std::string_view command, std::string_view longCommand)
{
    if (ParseArgument(argv, command, longCommand) && *(argv + 1) != nullptr)
    {
        argv++;
        return std::string(*argv);
    }
    return std::nullopt;
}
