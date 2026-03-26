#include "platform.h"
#include "dvdreader.h"
#include "xmlwriter.h"

#define DEFAULT_LICENSE_NAME "license_data.dat"

namespace param
{
    fs::path isoFile;
    fs::path outPath;
    fs::path xmlFile;
    bool dir = false;
    bool iso = false;
    bool lba = false;
    bool force = false;
    bool noXml = false;
    bool noWarns = false;
    bool quietMode = false;
    bool pathTable = false;
    bool outputSortedByDir = false;
}

template <size_t N>
static void PrintId(const char *label, char (&id)[N])
{
    std::string_view view;

    const std::string_view id_view(id, N);
    const size_t last_char = id_view.find_last_not_of(' ');
    if (last_char != std::string_view::npos)
        view = id_view.substr(0, last_char + 1);

    if (!view.empty())
        printf("%s%.*s\n", label, static_cast<int>(view.length()), view.data());
}

static void PrintDate(const char *label, const ISO_LONG_DATESTAMP &date)
{
    auto ZERO_DATE = GetUnspecifiedLongDate();
    if (memcmp(&date, &ZERO_DATE, sizeof(date)) != 0)
        printf("%s%s\n", label, LongDateToString(date).c_str());
}

static std::unique_ptr<ISO_LICENSE> ReadLicense()
{
    auto license = std::make_unique<ISO_LICENSE>();

    dvd::reader->SeekToSector(0);
    dvd::reader->ReadBytes(license->data, sizeof(license->data));

    return license;
}

static void SaveLicense(const ISO_LICENSE &license)
{
    if (!param::quietMode)
        printf("Creating license data...");

    unique_file outFile = OpenScopedFile(param::outPath / DEFAULT_LICENSE_NAME, "wb");

    if (outFile == NULL)
    {
        printf("\nERROR: Cannot create license file.\n");
        exit(EXIT_FAILURE);
    }
    else if (!param::quietMode)
    {
        printf(" Ok.\n");
    }

    fwrite(license.data, 1, sizeof(license.data), outFile.get());
}

static void ExtractFiles(const std::list<Entry> &entries, const fs::path &rootPath)
{
    for (const auto &entry : entries)
    {
        if (entry.type == EntryType::EntryDir) // Do not extract directories, they're already prepared
            continue;

        const fs::path outputPath = rootPath / entry.path / entry.identifier;

        if (!param::quietMode)
        {
            printf("  Extracting \"%s\"... ", outputPath.string().c_str());
            fflush(stdout);
        }

        MMappedFile outFile;
        if (!outFile.Create(outputPath, entry.size))
        {
            printf("\nERROR: Cannot create file \"%s\"\n", outputPath.filename().string().c_str());
            exit(EXIT_FAILURE);
        }

        memcpy(outFile.GetView(0, entry.size).GetBuffer(), dvd::reader->GetSectorView(iso::layerBegLBA + entry.lba, GetSizeInSectors(entry.size)).GetBuffer(), entry.size);

        if (!param::quietMode)
            printf("Done.\n");
    }

    // Update timestamps AFTER all files have been extracted
    // else directories will have their timestamps discarded when files are being unpacked into them!
    for (const auto &entry : entries)
    {
        fs::path toChange(rootPath / entry.path / entry.identifier);
        UpdateTimestamps(toChange, tm{
            .tm_sec  = entry.date.second,
            .tm_min  = entry.date.minute,
            .tm_hour = entry.date.hour,
            .tm_mday = entry.date.day,
            .tm_mon  = entry.date.month - 1,
            .tm_year = entry.date.year
        });
    }
}

static void CreateDirs(Entry &dirEntry, size_t &numDirs)
{
    std::error_code ec;
    const fs::path dirPath = param::outPath / dirEntry.path / dirEntry.identifier;
    fs::create_directories(dirPath, ec);
    if (ec)
    {
        printf("\nERROR: Cannot create directory \"%s\". %s\n", dirPath.string().c_str(), ec.message().c_str());
        exit(EXIT_FAILURE);
    }

    size_t subDirCount = 0;
    for (auto it : dirEntry.subdir->GetView())
    {
        if (it->type != EntryType::EntryDir)
            continue;

        subDirCount++;
        CreateDirs(*it, ++numDirs);
    }
    if (dirEntry.flc == subDirCount+1)
        dirEntry.flc = 0; // reset FLC if it's not bugged
}

static void ParseDIR()
{
    // Limits of libcdvd-common.h from ps2sdk; bypassing these will crash sceCdSearchFile().
    constexpr int CdlMAXFILE = 64; /* max number of files in a directory */
    constexpr int CdlMAXDIR = 128; /* max number of total directories */
    constexpr int CdlMAXLEVEL = 8; /* max levels of directories */
    // These limits assume ISO Level 1 names (8.3), representing internal buffer capacities:
    // 64  = ((2048 - 96) / 60) * 2; 2 directory record sectors for files (96 bytes for "." & ".." overhead, 60 bytes max entry size).
    // 128 = (2048 / 16) * 1;        1 path table sector for directories (16 bytes max average entry, including the root entry).
    // So, using names longer than 8.3 will decrease this limit even further.

    std::string licenseFile;
    int postGap = 10240; // 20MiB dummy
    int dirCount = CdlMAXDIR - 1; // Reserve one for root
    auto ParseSubDIR = [&](auto &&self, ListView<Entry> view, const fs::path &src, int fileCount, int level) -> std::unique_ptr<iso::DirTree>
    {
        if (level > CdlMAXLEVEL)
        {
            printf("ERROR: Exceeded maximum directory hierarchy depth levels (%d) at \"%s\"\n", CdlMAXLEVEL, src.string().c_str());
            exit(EXIT_FAILURE);
        }

        auto dirEntries = std::make_unique<iso::DirTree>(std::move(view));

        std::error_code ec;
        auto iterator = fs::directory_iterator(src, ec);
        if (ec)
        {
            printf("ERROR: Cannot read directory \"%s\". %s\n", src.string().c_str(), ec.message().c_str());
            exit(EXIT_FAILURE);
        }

        for (const auto &fsEntry : iterator)
        {
            auto &entry = dirEntries->EmplaceBack(Entry
            {
                .order = -1,
                .path = (src == param::outPath) ? fs::path() : src.lexically_proximate(param::outPath),
                .type = EntryType::EntryFile,
                .identifier = fsEntry.path().filename().string()
            });

            if (level == 1 && entry.identifier.length() >= 7)
            {
                if (entry.identifier.length() == 10 && CompareICase(entry.identifier, "SYSTEM.CNF"))
                {
                    dirEntries->RotateBack();
                }
                else if (CompareICase(entry.identifier.substr(0, 7), "license"))
                {
                    licenseFile = entry.identifier;
                    dirEntries->PopBack();
                    continue;
                }
            }

            if (fsEntry.is_directory())
            {
                if (--dirCount == -1)
                    printf("WARNING: Exceeded maximum directories (%d) for libcdvd sceCdSearchFile() at \"%s\"\n", CdlMAXDIR, src.string().c_str());

                entry.type = EntryType::EntryDir;
                entry.subdir = self(self, dirEntries->NewView(), fsEntry.path(), CdlMAXFILE, level+1);
            }
            else if (--fileCount == -1)
            {
                printf("WARNING: Exceeded maximum files per directory (%d) for libcdvd sceCdSearchFile() at \"%s\"\n", CdlMAXFILE, src.string().c_str());
            }
        }
        return dirEntries;
    };

    // Create descriptors
    memcpy(iso::descriptor.volumeID,              "MKPS2ISO",    sizeof("MKPS2ISO"));
    memcpy(iso::descriptor.systemID,              "PLAYSTATION", sizeof("PLAYSTATION"));
    memcpy(iso::descriptor.applicationIdentifier, "PLAYSTATION", sizeof("PLAYSTATION"));

    if (!param::quietMode)
        printf("\nParsing directory \"%s\"... Done.\n", param::outPath.string().c_str());

    // Create root
    std::list<Entry> entries;
    Entry &root = entries.emplace_back(Entry{.order = -1, .type = EntryType::EntryDir});

    // Parse directory recursively
    root.subdir = ParseSubDIR(ParseSubDIR, ListView(entries), param::outPath, CdlMAXFILE, 1);

    if (!param::quietMode)
        printf("Creating XML document... ");

    // Write XML sorted by directories
    param::outputSortedByDir = true;
    xml::Writer().WriteHeaders(licenseFile)->WriteDirTree(entries, postGap);
    if (!param::quietMode)
    {
        printf("Done.\n");

        printf("\n\nIMPORTANT:\n"
               "----------------------------------------------------\n"
               "Files in the root directory starting with \"license\"\n"
               "are assumed to be disc licenses.\n\n"
               "Place the BOOT executable (from SYSTEM.CNF)\n"
               "immediately after the SYSTEM.CNF entry.\n");
        printf("----------------------------------------------------\n");
        printf("Press Enter to continue...");
        getchar();
    }
}

static void ParseISO()
{
    if (!param::quietMode)
    {
        printf("Output directory : \"%s\"\n\n", param::outPath.string().c_str());

        printf("Identifiers:\n");
        PrintId("  System ID         : ", iso::descriptor.systemID);
        PrintId("  Volume ID         : ", iso::descriptor.volumeID);
        PrintId("  Volume Set ID     : ", iso::descriptor.volumeSetIdentifier);
        PrintId("  Publisher ID      : ", iso::descriptor.publisherIdentifier);
        PrintId("  Data Preparer ID  : ", iso::descriptor.dataPreparerIdentifier);
        PrintId("  Application ID    : ", iso::descriptor.applicationIdentifier);
        PrintId("  Copyright ID      : ", iso::descriptor.copyrightFileIdentifier);

        PrintDate("  Creation Date     : ", iso::descriptor.volumeCreateDate);
        PrintDate("  Modification Date : ", iso::descriptor.volumeModifyDate);
        PrintDate("  Expiration Date   : ", iso::descriptor.volumeExpiryDate);
        printf("\n");

        if (!param::noXml)
            printf("License file: \"%s\"\n\n", (param::outPath / DEFAULT_LICENSE_NAME).string().c_str());
    }

    std::list<std::tuple<std::list<Entry>, uint32_t, uint32_t>> layers;
    while (true)
    {
        if (!param::quietMode)
            printf("Parsing Layer%zu directory tree...\n", layers.size());

        auto &[entries, layerLenLBA, postGap] = layers.emplace_back();
        Entry &rootDir = [&]() -> Entry &
        {
            if (!param::iso)
                return iso::ParseRoot<true>(entries, layout::LBA_ANCHOR, nullptr);

            if (!param::pathTable)
                return iso::ParseRoot<false>(entries, iso::descriptor.rootDirRecord.entryOffs.lsb, nullptr);

            iso::PathTable pathTable(iso::descriptor.pathTable1Offs, iso::descriptor.pathTableSize.lsb);
            if (pathTable.entries[0].data.dirOffs != iso::descriptor.rootDirRecord.entryOffs.lsb)
            {
                printf("\nERROR: Root directory offset in path table does not match the one in volume descriptor.\n"
                        "       The ISO image may be corrupt or invalid.\n");
                exit(EXIT_FAILURE);
            }
            return iso::ParseRoot<false>(entries, iso::descriptor.rootDirRecord.entryOffs.lsb, &pathTable.entries);
        }();

        // Sort files by LBA for "strict" output
        entries.sort([](const auto &left, const auto &right)
                     { return left.lba < right.lba; });

        layerLenLBA = iso::descriptor.volumeSize.lsb;

        // PostGap sanity checks
        postGap = layerLenLBA - (entries.back().lba + GetSizeInSectors(entries.back().size));
        if (postGap == 0)
        {
            if (!param::noWarns)
                printf("WARNING: The UDF image does not have a trailing Anchor, it may be corrupt or invalid.\n");
        }
        else
        {
            postGap -= GetSizeInSectors(sizeof(anchorVolDescPtr));
            if (AlignTo<16>(layerLenLBA - GetSizeInSectors(sizeof(anchorVolDescPtr))) != layerLenLBA)
            {
                if (!param::noWarns)
                    printf("WARNING: The UDF image is not aligned.\n");
            }
            else
            {
                postGap &= ~15u; // Aligns down to the nearest multiple of 16
                constexpr uint32_t SECTORS_PER_MIB = DVD_SECTOR_SIZE / 4;
                if (postGap != (20 * SECTORS_PER_MIB) && postGap != 0 && !param::noWarns)
                    printf("WARNING: Size of postgap is of %.2fMiB instead of 20MiB.\n", static_cast<double>(postGap) / SECTORS_PER_MIB);
            }
        }

        // Prepare output directories
        size_t numDirs = 0;
        CreateDirs(rootDir, numDirs);

        if (!param::quietMode)
        {
            printf("  Files Total: %zu\n", entries.size()-1 - numDirs);
            printf("  Directories: %zu\n", numDirs);
            printf("  Total file system size: %ju bytes (%u sectors)\n\n", static_cast<uintmax_t>(layerLenLBA) * DVD_SECTOR_SIZE, layerLenLBA);
        }

        if ((iso::layerBegLBA += layerLenLBA) >= dvd::reader->GetTotalSectors() || !dvd::reader->SeekToSector(iso::layerBegLBA))
            break;

        if (dvd::reader->ReadBytes<true>(&iso::descriptor, DVD_SECTOR_SIZE) != DVD_SECTOR_SIZE || memcmp(&iso::descriptor.header, "\1" VSD_STD_ID_CD001 "\1", 7) != 0)
        {
            printf("ERROR: Layer%zu does not contain a valid ISO9660 file system.\n", layers.size());
            exit(EXIT_FAILURE);
        }
        iso::layerBegLBA -= layout::LBA_ISO_PVD;
    }

    if (!param::quietMode)
        printf("Unpacking...\n");

    iso::layerBegLBA = 0;
    for (const auto &[entries, layerLenLBA, _] : layers)
    {
        ExtractFiles(entries, param::outPath);
        iso::layerBegLBA += layerLenLBA - layout::LBA_ISO_PVD;
    }
    if (!param::quietMode)
        printf("\n");

    if (!param::noXml)
    {
        SaveLicense(*ReadLicense());
        if (!param::quietMode)
            printf("Creating XML document...");

        xml::Writer xml;
        xml.WriteHeaders(DEFAULT_LICENSE_NAME);

        uint32_t currentLBA = 0;
        for (const auto &[entries, _, postGap] : layers)
        {
            currentLBA += AlignTo<16>(xml.WriteDirTree(entries, postGap)) - layout::LBA_ISO_PVD;
        }

        if (!param::quietMode)
            printf(" Ok.\n\n");

        // Check if there is still an EoF gap
        if (!param::noWarns && currentLBA < iso::layerBegLBA)
        {
            printf("WARNING: There is still a gap of %u sectors at the end of file system.\n"
                   "\t This could mean that there are missing files.\n"
                   "\t Try using the -pt command, helps with obfuscated file systems.\n", iso::layerBegLBA - currentLBA);
        }
    }

    if (!param::quietMode)
        printf("Image dumped successfully.\n");
}

int Main(int argc, char *argv[])
{
    constexpr const char *HELP_TEXT =
        "Usage: dumps2iso [options] <input>\n\n"
        "  <input>\t\tAny 2048-sector disc image to extract, or a directory to generate an XML project.\n\n"
        "Options:\n"
        "  -h|--help\t\tShows this help text\n"
        "  -q|--quiet\t\tQuiet mode (suppress all but warnings and errors)\n"
        "  -w|--warns\t\tSuppress all warnings (can be used along with -q)\n"
        "  -o <path>\t\tOptional destination directory for extracted files (defaults to working dir)\n"
        "  -x <file>\t\tOptional XML name/destination for MKPS2ISO script (defaults to working dir)\n"
        "  -i|--iso\t\tDumps all files reading ISO structure instead of UDF\n"
        "  -l|--lba\t\tWrites all source paths and LBA offsets in the XML to force them at build time\n"
        "  -n|--noxml\t\tDo not generate an XML file and license file\n"
        "  -p|--path-table\tGo through every known ISO directory in order; helps on soft obfuscated games\n"
        "  -s|--sort-by-dir\tOutputs a \"pretty\" XML script where entries are grouped in directories\n"
        "\t\t\t(instead of strictly following their original order on the disc)\n";

    constexpr const char *VERSION_TEXT =
        "DUMPS2ISO " VERSION " - PlayStation 2 Image Dumper\n"
        "Get the latest version at https://github.com/N4gtan/mkps2iso\n\n";

    if (argc == 1)
    {
        printf(VERSION_TEXT);
        printf(HELP_TEXT);
        return EXIT_SUCCESS;
    }

    for (char **args = argv + 1; *args != nullptr; args++)
    {
        // Is it a switch?
        if ((*args)[0] == '-')
        {
            if (ParseArgument(args, "h", "help"))
            {
                printf(VERSION_TEXT);
                printf(HELP_TEXT);
                return EXIT_SUCCESS;
            }
            if (ParseArgument(args, "f", "force"))
            {
                param::force = true;
                continue;
            }
            if (ParseArgument(args, "i", "iso"))
            {
                param::iso = true;
                continue;
            }
            if (ParseArgument(args, "l", "lba"))
            {
                param::lba = true;
                continue;
            }
            if (ParseArgument(args, "n", "noxml"))
            {
                param::noXml = true;
                continue;
            }
            if (ParseArgument(args, "p", "path-table"))
            {
                param::iso = true;
                param::pathTable = true;
                continue;
            }
            if (ParseArgument(args, "q", "quiet"))
            {
                param::quietMode = true;
                continue;
            }
            if (ParseArgument(args, "w", "warns"))
            {
                param::noWarns = true;
                continue;
            }
            if (ParseArgument(args, "s", "sort-by-dir"))
            {
                param::outputSortedByDir = true;
                continue;
            }
            if (auto outPath = ParsePathArgument(args, "o"); outPath.has_value())
            {
                param::outPath = outPath->lexically_normal();
                continue;
            }
            if (auto xmlPath = ParsePathArgument(args, "x"); xmlPath.has_value())
            {
                param::xmlFile = xmlPath->lexically_normal();
                continue;
            }

            // If we reach this point, an unknown parameter was passed
            printf("Unknown parameter: %s\n", *args);
            return EXIT_FAILURE;
        }

        if (param::isoFile.empty())
        {
            param::isoFile = fs::path(*args).lexically_normal().lexically_proximate(fs::current_path());
        }
        else
        {
            printf("Only one image file is supported.\n");
            return EXIT_FAILURE;
        }
    }

    if (param::isoFile.empty())
    {
        printf("No iso file specified.\n");
        return EXIT_FAILURE;
    }

    // PowerShell tab-completion adds a trailing slash to directories, breaking fs::path::stem. Idk who thought this was a good idea...
    if (!param::isoFile.has_filename() && param::isoFile.has_parent_path())
        param::isoFile = param::isoFile.parent_path();

    if (!param::quietMode)
        printf(VERSION_TEXT);

    if (param::outPath.empty())
        param::outPath = param::isoFile.stem();

    if (param::xmlFile.empty() || fs::is_directory(param::xmlFile))
       (param::xmlFile /= param::outPath.filename()) += ".xml";

    // Initialize reader
    dvd::reader = std::make_unique<dvd::IsoReader>();

    if (fs::is_directory(param::isoFile))
    {
        param::dir = true;
        param::outPath = param::isoFile;
        ParseDIR();
        return EXIT_SUCCESS;
    }

    if (!dvd::reader->Open(param::isoFile))
    {
        printf("ERROR: Cannot open file \"%s\"\n", param::isoFile.string().c_str());
        return EXIT_FAILURE;
    }

    // Check if file has a valid ISO9660 header
    if (!dvd::reader->SeekToSector(16) ||
        dvd::reader->ReadBytes<true>(&iso::descriptor, DVD_SECTOR_SIZE) != DVD_SECTOR_SIZE || memcmp(&iso::descriptor.header, "\1" VSD_STD_ID_CD001 "\1", 7) != 0)
    {
        printf("ERROR: File does not contain a valid ISO9660 file system.\n");
        return EXIT_FAILURE;
    }

    ParseISO();
    return EXIT_SUCCESS;
}
