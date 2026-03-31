#include "isobuilder.h"
#include "crc.h"
#include "logo.h"
#include "platform.h"
#include "dvdwriter.h"
#include <queue>

#define GENERATOR "DVD-ROM GENERATOR"

namespace param
{
    extern bool master;
    extern bool quietMode;
    extern fs::path logoRawFile;
};

namespace global
{
    extern time_t buildTime;
};

static uint32_t MinimumOne(const uint32_t val)
{
    return val > 1 ? val : 1;
}

static ISO_USHORT_PAIR SetPair16(uint16_t val)
{
    return {val, SwapBytes16(val)};
}

static ISO_UINT_PAIR SetPair32(uint32_t val)
{
    return {val, SwapBytes32(val)};
}

static std::string ToIsoDchars(std::string_view identifier)
{
    std::string result(identifier);
    for (char &ch : result)
    {
        if (isalnum(static_cast<uint8_t>(ch)))
            ch = std::toupper(static_cast<uint8_t>(ch));
        else if (ch != '.')
            ch = '_';
    }
    return result;
}

iso::DirTree::DirTree(ListView<Entry> view, Entry *entry, DirTree *parent)
    : ListView<Entry>(std::move(view)), m_entry(entry), m_parent(parent)
{
}

void iso::DirTree::OutputHeaderListing(FILE *fp, const char *name) const
{
    fprintf(fp, "/* %s */\n", name);

    for (const auto it : GetView())
    {
        if (it->type != EntryType::EntryFile)
            continue;

        std::string temp_name = "LBA_" + it->identifier;

        for (char &ch : temp_name)
        {
            if (ch == '.' || ch == ' ' || ch == '-')
                ch = '_';

            if (ch == ';')
            {
                ch = '\0';
                break;
            }
        }

        fprintf(fp, "#define %-17s %u\n", temp_name.c_str(), layerBegLBA + it->lba);
    }
    fprintf(fp, "\n");

    for (const auto it : GetView())
    {
        if (it->type == EntryType::EntryDir)
            it->subdir->OutputHeaderListing(fp, it->identifier.c_str());
    }
}

void iso::DirTree::OutputLBAlisting(FILE *fp, const int level) const
{
    // Helper lambda to print common details
    auto printEntryDetails = [&](const char *type, const char *name, const char *sectors, const Entry &entry)
    {
        // Write entry type with 4 spaces at start
        fprintf(fp, "%*s%-6s|", level + 4, "", type);
        // Write entry name
        fprintf(fp, "%-32s|", name);
        // Write entry size in sectors
        fprintf(fp, "%-8s|", sectors);
        // Write LBA offset
        fprintf(fp, "%-7u|", layerBegLBA + entry.lba);
        // Write size in byte units
        fprintf(fp, "%-11s|", entry.type != EntryType::EntryDir ? std::to_string(entry.size).c_str() : "");
        // Write source file path
        fprintf(fp, "%s\n", entry.path.generic_string().c_str());
    };

    uint32_t maxlba = 0;
    if (level == 0)
    {
        for (const auto it : GetView())
        {
            if (it->type != EntryType::EntryDummy)
                maxlba = std::max(it->lba, maxlba);
        }
    }

    // Print first the files in the directory
    for (const auto it : GetView())
    {
        // Skip directories and postgap dummy
        if (it->type == EntryType::EntryDir || (it->type == EntryType::EntryDummy && level == 0 && it->lba > maxlba))
            continue;

        const char *typeStr = it->type == EntryType::EntryFile ? "File " : "Dummy";
        std::string nameStr = it->type == EntryType::EntryFile ? it->identifier : "<DUMMY>";
        uint32_t sectors = GetSizeInSectors(it->size);

        // Print the entry details
        printEntryDetails(typeStr, nameStr.c_str(), std::to_string(sectors).c_str(), *it);
    }

    // Print directories and postgap dummy
    for (const auto it : GetView())
    {
        if (it->type == EntryType::EntryDir)
        {
            printEntryDetails("Dir ", it->identifier.c_str(), "", *it);
            it->subdir->OutputLBAlisting(fp, level + 1);
        }
        else if (it->type == EntryType::EntryDummy && level == 0 && it->lba > maxlba)
        {
            printEntryDetails("Dummy", "<DUMMY>", std::to_string(GetSizeInSectors(it->size)).c_str(), *it);
        }
    }

    if (level > 0)
        fprintf(fp, "%*sEnd   |%-32s|%-8s|%-7s|%-11s|\n", level + 3, "", m_entry->identifier.c_str(), "", "", "");
}

static ISO_DATESTAMP GetISODateStamp(time_t time, char GMToffs, const char *date)
{
    ISO_DATESTAMP result;
    if (ParseDateFromString(result, date, GMToffs))
        return result;

    const tm *timestamp = LocalTime(&time);

    result.year   = timestamp->tm_year;
    result.month  = timestamp->tm_mon + 1;
    result.day    = timestamp->tm_mday;
    result.hour   = timestamp->tm_hour;
    result.minute = timestamp->tm_min;
    result.second = timestamp->tm_sec;

    return result;
}

Entry &iso::DirTree::CreateRootDirectory(std::list<Entry> &entries, const ISO_DATESTAMP &volumeDate, const EntryAttributes &attributes)
{
    Entry &entry = entries.emplace_back();

    entry.type   = EntryType::EntryDir;
    entry.subdir = std::make_unique<DirTree>(ListView(entries), &entry);
    entry.date   = volumeDate;
    entry.size   = 0; // We will calculate the length later when all entries have been processed
    entry.hf     = attributes.HFLAG & 3;
    //entry.attribs = attributes.XAAttrib;
    //entry.perms   = attributes.XAPerm;
    //entry.GID     = attributes.GID;
    //entry.UID     = attributes.UID;

    return entry;
}

bool iso::DirTree::AddFileEntry(std::string id, fs::path srcFile, const EntryAttributes &attributes, const char *date)
{
    auto fileAttrib = Stat(srcFile);
    if (!fileAttrib)
    {
        printf("ERROR: File not found: %s\n", srcFile.string().c_str());
        return false;
    }

    // Check if file entry already exists
    for (const auto it : GetView())
    {
        if (!it->identifier.empty())
        {
            if ((it->type != EntryType::EntryDir) && (CompareICase(it->identifier, id)))
            {
                printf("ERROR: Duplicate file entry: %s\n", id.c_str());
                return false;
            }
        }
    }

    Entry &entry = EmplaceBack();

    entry.identifier = std::move(id);
    entry.type       = EntryType::EntryFile;
    entry.hf         = attributes.HFLAG & 3;
    //entry.attribs = attributes.XAAttrib;
    //entry.perms   = attributes.XAPerm;
    //entry.GID     = attributes.GID;
    //entry.UID     = attributes.UID;
    entry.order      = attributes.ORDER;
    entry.lba        = attributes.LBAFRC;
    entry.path       = std::move(srcFile);
    entry.date       = GetISODateStamp(fileAttrib->st_mtime, attributes.GMTOffs, date);
    entry.size       = fileAttrib->st_size;

    return true;
}

void iso::DirTree::AddDummyEntry(const uint32_t sectors, const uint32_t lbaFRC)
{
    Entry &entry = EmplaceBack();

    entry.type  = EntryType::EntryDummy;
    entry.size  = DVD_SECTOR_SIZE * sectors;
    entry.lba   = lbaFRC;
    entry.order = -1;
}

iso::DirTree *iso::DirTree::AddSubDirEntry(std::string id, const fs::path &srcDir, const EntryAttributes &attributes, const char *date)
{
    // Duplicate directory entries are allowed, but the subsequent occurences will not add a new directory to 'entries'.
    for (const auto it : GetView())
    {
        if ((it->type == EntryType::EntryDir) && (it->identifier == id))
            return it->subdir.get();
    }

    auto fileAttrib = Stat(srcDir);
    if (!fileAttrib.has_value())
        fileAttrib.emplace().st_mtime = global::buildTime;

    Entry &entry = EmplaceBack();

    entry.identifier = std::move(id);
    entry.type       = EntryType::EntryDir;
    entry.subdir     = std::make_unique<DirTree>(NewView(), &entry, this);
    entry.hf         = attributes.HFLAG & 3;
    //entry.attribs = attributes.XAAttrib;
    //entry.perms   = attributes.XAPerm;
    //entry.GID     = attributes.GID;
    //entry.UID     = attributes.UID;
    entry.flc        = attributes.LINKC;
    entry.order      = attributes.ORDER;
    entry.date       = GetISODateStamp(fileAttrib->st_mtime, attributes.GMTOffs, date);
    entry.size       = 0; // We will calculate the length later when all entries have been processed

    return entry.subdir.get();
}

uint32_t iso::DirTree::CalculatePathTableLen(const Entry &dirEntry) const
{
    // Put identifier (empty if first entry)
    uint32_t len = sizeof(ISO_PATHTABLE_ENTRY) + AlignTo<2>(MinimumOne(dirEntry.identifier.length()));
    for (const auto it : GetView())
    {
        if (it->type == EntryType::EntryDir)
            len += it->subdir->CalculatePathTableLen(*it);
    }

    return len;
}

uint32_t iso::DirTree::CalculateFileIdDescLen() const
{
    uint32_t dirEntryLen = AlignTo<4>(sizeof(fileIdentDesc));

    for (const auto it : GetView())
    {
        if (it->type == EntryType::EntryDummy || it->hf > 1)
            continue;

        uint32_t dataLen = sizeof(fileIdentDesc);

        dataLen++; // Compression ID
        dataLen += it->identifier.length() * 2; // UTF-16
        dataLen = AlignTo<4>(dataLen);

        dirEntryLen += dataLen;
    }

    return dirEntryLen;
}

uint32_t iso::DirTree::CalculateDirRecordLen() const
{
    uint32_t dirEntryLen = (AlignTo<2>(sizeof(ISO_DIR_ENTRY)) * 2) + (sizeof(ISO_XA_ATTRIB) * 2);

    for (const auto it : GetView())
    {
        if (it->type == EntryType::EntryDummy || it->hf > 1)
            continue;

        uint32_t dataLen = sizeof(ISO_DIR_ENTRY);

        dataLen += it->identifier.length() + (it->type == EntryType::EntryFile ? sizeof(";1") - 1 : 0);
        dataLen = AlignTo<2>(dataLen);
        dataLen += sizeof(ISO_XA_ATTRIB);

        // Round dirEntryLen to the nearest multiple of 2048 as the rest of that sector is "unusable"
        constexpr int SECTOR_MASK = DVD_SECTOR_SIZE - 1;
        if ((dirEntryLen & SECTOR_MASK) + dataLen > DVD_SECTOR_SIZE)
            dirEntryLen = (dirEntryLen + SECTOR_MASK) & ~SECTOR_MASK;

        dirEntryLen += dataLen;
    }

    return dirEntryLen;
}

uint32_t iso::DirTree::CalculateFileTreeLBA(uint32_t lba)
{
    for (Entry &entry : GetUnderlyingList())
    {
        // Skip directories, they are already calculated
        if (entry.type == EntryType::EntryDir)
            continue;

        if (entry.lba == 0)
            entry.lba = lba;

        entry.lbaISO = entry.lba;

        // Prevent rewind on forced LBAs and update next available LBA
        lba = std::max(lba, entry.lba + GetSizeInSectors(entry.size));
    }

    return lba;
}

uint32_t iso::DirTree::CalculateDirRecordLBA(uint32_t lba)
{
    for (Entry &entry : GetUnderlyingList())
    {
        // Skip non directories
        if (entry.type != EntryType::EntryDir)
            continue;

        // Set current LBA to directory record entry
        entry.lbaISO = lba;
        //entry.size = entry.subdir->CalculateDirRecordLen();
        lba += GetSizeInSectors(entry.subdir->CalculateDirRecordLen());
    }

    return lba;
}

uint32_t iso::DirTree::CalculateFileIdDescLBA(uint32_t lba)
{
    for (Entry &entry : GetUnderlyingList())
    {
        // Skip non directories
        if (entry.type != EntryType::EntryDir)
            continue;

        // Set current LBA to directory record entry
        entry.lba = lba;
        entry.size = entry.subdir->CalculateFileIdDescLen();
        lba += GetSizeInSectors(entry.size);
    }

    return lba;
}

uint32_t iso::DirTree::CalculateInfCtrlBlockLBA(uint32_t lba)
{
    for (Entry &entry : GetUnderlyingList())
    {
        // Skip dummies
        if (entry.type == EntryType::EntryDummy)
            continue;

        // Set current LBA to directory record entry
        entry.lbaICB = lba++;
    }

    return lba;
}

void iso::DirTree::PartitionEntries()
{
    std::list<Entry> dirsByLevel;
    std::queue<iso::DirTree *> dirsQueue;

    // Start Breadth-First Search with the root directory
    dirsQueue.push(this);

    auto &entries = GetUnderlyingList();
    while (!dirsQueue.empty())
    {
        auto *currentTree = dirsQueue.front();
        dirsQueue.pop();

        for (const auto it : currentTree->GetView())
        {
            if (it->type == EntryType::EntryDir)
            {
                // Extract only directories into our temporary list
                dirsByLevel.splice(dirsByLevel.end(), entries, it);

                // Queue subdirectory for the next level
                dirsQueue.push(it->subdir.get());
            }
        }
    }

    // Insert ordered directories right after the root entry (which remains at the front)
    entries.splice(std::next(entries.begin()), dirsByLevel);
}

void iso::DirTree::SaveDirEntriesOrder()
{
    int index = 0;
    for (const auto it : GetView())
    {
        it->order = index++;
        if (it->type == EntryType::EntryDir)
            it->subdir->SaveDirEntriesOrder();
    }
}

void iso::DirTree::SortDirectoryEntries(const bool byLBA)
{
    // Search for directories
    for (const auto it : GetView())
    {
        // Perform recursive call
        if (it->type == EntryType::EntryDir)
            it->subdir->SortDirectoryEntries(byLBA);
    }

    if (byLBA)
    {
        SortView([](const auto &left, const auto &right)
                 { return left->lba < right->lba; });
    }
    else
    {
        SortView([](const auto &left, const auto &right)
                 { return left->order < right->order; });
    }
}

template <bool recursive>
uint32_t iso::DirTree::GetFileCount() const
{
    int numfiles = 0;
    for (const auto it : GetView())
    {
        if (it->type != EntryType::EntryDir)
        {
            if (it->type != EntryType::EntryDummy)
                numfiles++;
        }
        else if constexpr (recursive)
        {
            numfiles += it->subdir->GetFileCount();
        }
    }

    return numfiles;
}

template <bool recursive>
uint32_t iso::DirTree::GetDirCount() const
{
    int numdirs = 0;
    for (const auto it : GetView())
    {
        if (it->type == EntryType::EntryDir)
        {
            numdirs++;
            if constexpr (recursive)
                numdirs += it->subdir->GetDirCount();
        }
    }

    return numdirs;
}

void iso::DirTree::PrintRecordPath() const
{
    if (m_parent == nullptr)
        return;

    m_parent->PrintRecordPath();
    printf("/%s", m_entry->identifier.c_str());
}

uint32_t iso::DirTree::GetPathDepth(size_t *pathLength) const
{
    int depth = 0;
    for (auto current = this; current->m_parent != nullptr; current = current->m_parent)
    {
        depth++;
        if (pathLength != nullptr)
            *pathLength += current->m_entry->identifier.length();
    }

    return depth;
}

void iso::DirTree::WriteFiles() const
{
    for (const Entry &entry : GetUnderlyingList())
    {
        // Write files as regular data sectors
        if (entry.type == EntryType::EntryFile)
        {
            if (!param::quietMode)
            {
                printf("  Packing \"%s\"... ", entry.path.string().c_str());
                fflush(stdout);
            }

            auto fp = OpenScopedFile(entry.path, "rb");
            auto sectorView = dvd::writer->GetSectorView(layerBegLBA + entry.lba, GetSizeInSectors(entry.size));
            sectorView->WriteFile(fp.get());

            if (!param::quietMode)
                printf("Done.\n");
        }
        // Write dummies as gaps without data
        else if (entry.type == EntryType::EntryDummy)
        {
            const uint32_t sizeInSectors = GetSizeInSectors(entry.size);
            auto sectorView = dvd::writer->GetSectorView(layerBegLBA + entry.lba, sizeInSectors);

            sectorView->WriteBlankSectors(sizeInSectors);
        }
    }
}

void iso::WriteBootLogo(const Region::Bit region, const uint8_t key)
{
    auto logo = std::make_unique<ISO_BOOT_LOGO>();
    if (!param::logoRawFile.empty())
    {
        unique_file fp = OpenScopedFile(param::logoRawFile, "rb");
        if (fread(logo->data, 1, sizeof(logo->data), fp.get()) == 0)
            goto default_logo;
    }
    else
    {
    default_logo:
        switch (region)
        {
            case Region::Undef:  goto write_logo;
            case Region::Europe: memcpy(logo->data, bootlogo::pal, sizeof(bootlogo::pal)); break;
            default:             memcpy(logo->data, bootlogo::ntsc, sizeof(bootlogo::ntsc));
        }
    }

    for (size_t i = 0; i < sizeof(logo->data); ++i)
    {
        logo->data[i]  = (logo->data[i] << 5) | (logo->data[i] >> 3);
        logo->data[i] ^= key;
    }

write_logo:
    auto logoSectors = dvd::writer->GetSectorView(0, 14);
    logoSectors->WriteMemory(logo->data, sizeof(logo->data));
    logoSectors->WriteBlankSectors(2);
}

static void genCipherHashes(const char *serial, uint8_t &key, uint32_t &magic1, uint8_t &magic2)
{
    uint32_t letters = 0;
    uint32_t numbers = 0;

    // Pack ASCII code in reverse order
    for (int i = 0; *serial != 0 && i < 4; ++i, ++serial)
        letters |= static_cast<uint32_t>(*serial) << (21 - (i * 7));

    // Skip hyphen character
    if (*serial == '-' || *serial == '_')
        ++serial;

    // Parse string as a Base-10 integer
    for (int i = 0; *serial != 0 && i < 5; ++i, ++serial)
        numbers = (numbers * 10) + (*serial - '0');

    // Hash generation logic
    key    = ((numbers <<  3) & 0xF8) | ((letters >> 25) & 0x07);
    magic1 = ( numbers >> 10        ) | ( letters <<  7);
    magic2 = ((numbers >>  2) & 0xF8) | 0x04;
}

template <size_t N>
static void CopyStringPadWithSpaces(char (&dest)[N], const char *src)
{
    auto begin = std::begin(dest);
    auto end = std::end(dest);

    if (src != nullptr)
    {
        for (; begin != end && *src != 0; ++begin, ++src)
        {
            *begin = std::toupper((uint8_t)*src);
        }
    }

    // Pad the remaining space with spaces
    std::fill(begin, end, ' ');
}

uint8_t iso::WriteMasterDisc(std::string_view serial, const Region::Bit region, const uint32_t layer0LenLBA, const uint32_t layer1LenLBA)
{
    // Generate encryption hashes from serial
    uint32_t magic1;
    uint8_t key, magic2;
    genCipherHashes(serial.data(), key, magic1, magic2);

    auto masterSectors = dvd::writer->GetSectorView(14, 2);
    if (!param::master)
    {
        masterSectors->WriteBlankSectors(2);
        return key;
    }

    ISO_MASTER_DISC md{};
    CopyStringPadWithSpaces(md.serial, serial.data());
    CopyStringPadWithSpaces(md.producer, iso::isoIdentifiers.Producer);
    CopyStringPadWithSpaces(md.copyright, iso::isoIdentifiers.Copyright);
    memcpy(md.creationDate, iso::isoIdentifiers.CreationDate, sizeof(md.creationDate));
    memcpy(md.masterDiscId, "PlayStation Master Disc ", sizeof(md.masterDiscId));
    md.system               = '2';
    md.region               = region;
    md.media                = 0x02;

    md.dvd.layer0EndLBA     = layer0LenLBA - GetSizeInSectors(sizeof(anchorVolDescPtr));
    if (layer1LenLBA == 0)
        md.dvd.type         = 0x01;
    else
    {
        md.dvd.type         = 0x02;
        md.dvd.layer1EndLBA = md.dvd.layer0EndLBA + layer1LenLBA - layout::LBA_ISO_PVD;
    }
    memset(md.pad, ' ', sizeof(md.pad));

    auto *magicPtr          = md.magicBlock;
    *magicPtr++             = {0x01, UINT32_MAX, UINT32_MAX, magic1,    key};
    if (region != Region::Undef)
    {
        md.regionFlags      = 0x30 | (region == Region::China ? 0x08 : 0x00);
        *magicPtr++         = {0x02, UINT32_MAX, UINT32_MAX,      0,   0x80};
    }
    const uint32_t sign2    = (region == Region::China) ? 0x29EA : 0x104A;
    *magicPtr++             = {0x01,       0x4B,      sign2, magic1,    key};
    *magicPtr               = {0x03,       0x4B,      sign2,      0, magic2, 0x00, static_cast<uint8_t>(magicPtr == &md.magicBlock[3] ? 0x80 : 0x00)};
    if (region == Region::China)
        *++magicPtr         = {0x02,       0x4B,      sign2, magic1,    key};

    memset(&md.burner, ' ', sizeof(md.burner));
    CopyStringPadWithSpaces(md.toolVersion, "MKPS2ISO " VERSION);
    memset(md.pad2, ' ', sizeof(md.pad2));

    masterSectors->WriteMemory(&md, sizeof(md));
    masterSectors->WriteMemory(&md, sizeof(md));
    return key;
}

void iso::DirTree::WriteIsoDescriptors(const uint32_t layerLenLBA) const
{
    ISO_DESCRIPTOR isoDescriptor{};

    isoDescriptor.header.type    = 1;
    isoDescriptor.header.version = 1;
    CopyStringPadWithSpaces(isoDescriptor.header.identifier, VSD_STD_ID_CD001);

    // Set System identifier
    CopyStringPadWithSpaces(isoDescriptor.systemID, iso::isoIdentifiers.SystemID);

    // Set Volume identifier
    CopyStringPadWithSpaces(isoDescriptor.volumeID, iso::isoIdentifiers.VolumeID);

    // Set Application identifier
    CopyStringPadWithSpaces(isoDescriptor.applicationIdentifier, iso::isoIdentifiers.Application);

    // Volume Set identifier
    CopyStringPadWithSpaces(isoDescriptor.volumeSetIdentifier, iso::isoIdentifiers.VolumeSet);

    // Publisher identifier
    CopyStringPadWithSpaces(isoDescriptor.publisherIdentifier, iso::isoIdentifiers.Publisher);

    // Data preparer identifier
    CopyStringPadWithSpaces(isoDescriptor.dataPreparerIdentifier, iso::isoIdentifiers.DataPreparer);

    // Copyright (file) identifier
    CopyStringPadWithSpaces(isoDescriptor.copyrightFileIdentifier, iso::isoIdentifiers.Copyright);

    // Unneeded identifiers
    CopyStringPadWithSpaces(isoDescriptor.abstractFileIdentifier, nullptr);
    CopyStringPadWithSpaces(isoDescriptor.bibliographicFilelIdentifier, nullptr);

    ParseLongDateFromString(isoDescriptor.volumeCreateDate, iso::isoIdentifiers.CreationDate);
    ParseLongDateFromString(isoDescriptor.volumeModifyDate, iso::isoIdentifiers.ModificationDate);
    isoDescriptor.volumeEffectiveDate = isoDescriptor.volumeExpiryDate = GetUnspecifiedLongDate();
    isoDescriptor.fileStructVersion = 1;

    uint32_t pathTableLen     = CalculatePathTableLen(*m_entry);
    uint32_t pathTableSectors = GetSizeInSectors(pathTableLen);

    isoDescriptor.volumeSetSize   = SetPair16(1);
    isoDescriptor.volumeSeqNumber = SetPair16(1);
    isoDescriptor.sectorSize      = SetPair16(DVD_SECTOR_SIZE);
    isoDescriptor.pathTableSize   = SetPair32(pathTableLen);

    // Setup the root directory record
    isoDescriptor.rootDirRecord.entryLength   = sizeof(ISO_ROOTDIR_HEADER);
    isoDescriptor.rootDirRecord.entryOffs     = SetPair32(layout::LBA_TABLE_START + (pathTableSectors * 4));
    isoDescriptor.rootDirRecord.entrySize     = SetPair32(CalculateDirRecordLen());
    isoDescriptor.rootDirRecord.flags         = FID_FILE_CHAR_DIRECTORY | m_entry->hf;
    isoDescriptor.rootDirRecord.volSeqNum     = SetPair16(1);
    isoDescriptor.rootDirRecord.identifierLen = 1;
    isoDescriptor.rootDirRecord.entryDate     = m_entry->date;

    isoDescriptor.pathTable1Offs    = layout::LBA_TABLE_START;
    isoDescriptor.pathTable2Offs    = isoDescriptor.pathTable1Offs + pathTableSectors;
    isoDescriptor.pathTable1MSBoffs = isoDescriptor.pathTable2Offs + 1;
    isoDescriptor.pathTable2MSBoffs = isoDescriptor.pathTable1MSBoffs + pathTableSectors;
    isoDescriptor.pathTable1MSBoffs = SwapBytes32(isoDescriptor.pathTable1MSBoffs);
    isoDescriptor.pathTable2MSBoffs = SwapBytes32(isoDescriptor.pathTable2MSBoffs);

    isoDescriptor.volumeSize = SetPair32(layerLenLBA);

    // Write the descriptor
    auto isoDescriptorSectors = dvd::writer->GetSectorView(layerBegLBA + layout::LBA_ISO_PVD, GetSizeInSectors(sizeof(ISO_DESCRIPTOR) * 2));
    isoDescriptorSectors->WriteMemory(&isoDescriptor, sizeof(isoDescriptor));

    // Write descriptor terminator;
    memset(&isoDescriptor, 0, sizeof(ISO_DESCRIPTOR));
    isoDescriptor.header.type    = 255;
    isoDescriptor.header.version = 1;
    CopyStringPadWithSpaces(isoDescriptor.header.identifier, VSD_STD_ID_CD001);
    isoDescriptorSectors->WriteMemory(&isoDescriptor, sizeof(isoDescriptor));

    // Build the path table in memory
    PathTable pathTableObj = GeneratePathTable();

    // Allocate buffer for path table
    const size_t pathTableSize = static_cast<size_t>(DVD_SECTOR_SIZE) * pathTableSectors;
    auto sectorBuff = std::make_unique<uint8_t[]>(pathTableSize);
    auto pathTable  = dvd::writer->GetSectorView(layerBegLBA + layout::LBA_TABLE_START, pathTableSectors * 4);

    // Serialize and write L-path table
    pathTableObj.SerializeTable<false>(sectorBuff.get());
    pathTable->WriteMemory(sectorBuff.get(), pathTableSize);
    pathTable->WriteMemory(sectorBuff.get(), pathTableSize);

    // Serialize and write M-path table
    pathTableObj.SerializeTable<true>(sectorBuff.get());
    pathTable->WriteMemory(sectorBuff.get(), pathTableSize);
    pathTable->WriteMemory(sectorBuff.get(), pathTableSize);
}


iso::PathTable iso::DirTree::GeneratePathTable() const
{
    uint16_t index = 1;
    PathTable pathTable;

    // Write out root explicitly first
    pathTable.entries.push_back({.data = {
        .identifierLen  = 1,
        .dirOffs        = m_entry->lbaISO,
        .parentDirIndex = index // Self for Root
    }});

    // Initialize Breadth-First Search Queue
    std::queue<std::tuple<const DirTree *, uint16_t>> dirsToProcess;
    dirsToProcess.emplace(this, index++);

    // Process directories using BFS
    while (!dirsToProcess.empty())
    {
        const auto [currentDir, parentIndex] = dirsToProcess.front();
        dirsToProcess.pop();

        for (const auto it : currentDir->GetView())
        {
            if (it->type == EntryType::EntryDir)
            {
                pathTable.entries.push_back({.data = {
                    .identifierLen  = static_cast<uint8_t>(MinimumOne(it->identifier.length())),
                    .dirOffs        = it->lbaISO,
                    .parentDirIndex = parentIndex},
                    .identifier     = ToIsoDchars(it->identifier)
                });

                // Queue subdirectories
                dirsToProcess.emplace(it->subdir.get(), index++);
            }
        }
    }

    return pathTable;
}

template <bool msb>
uint8_t *iso::PathTable::SerializeTable(uint8_t *buff) const
{
    for (const PathTableEntry &entry : entries)
    {
        if constexpr (!msb)
        {
            memcpy(buff, &entry.data, sizeof(entry.data));
        }
        else
        {
            ISO_PATHTABLE_ENTRY swappedEntry = entry.data;
            swappedEntry.dirOffs = SwapBytes32(swappedEntry.dirOffs);
            swappedEntry.parentDirIndex = SwapBytes16(swappedEntry.parentDirIndex);
            memcpy(buff, &swappedEntry, sizeof(swappedEntry));
        }

        buff += sizeof(entry.data);

        // Put identifier (nullptr if first entry)
        strncpy(reinterpret_cast<char *>(buff), entry.identifier.c_str(), entry.identifier.length());

        buff += AlignTo<2>(entry.data.identifierLen);
    }

    return buff;
}

void iso::DirTree::WriteDirectoryRecords() const
{
    auto writeOneEntry = [](dvd::IsoWriter::SectorView *sectorView, const Entry &entry, std::optional<bool> currentOrParent = std::nullopt) -> void
    {
        uint8_t buffer[sizeof(ISO_DIR_ENTRY) + 33 + sizeof(ISO_XA_ATTRIB)]{}; // 33 = 31 File Identifier chars + ';1'

        auto dirEntry = reinterpret_cast<ISO_DIR_ENTRY *>(buffer);

        uint32_t length;
        std::string identifier;
        if (entry.type == EntryType::EntryDir)
        {
            dirEntry->flags = 0x02 | entry.hf;
            length          = entry.subdir->CalculateDirRecordLen();
            identifier      = ToIsoDchars(entry.identifier);
        }
        else
        {
            dirEntry->flags = entry.hf;
            length          = entry.size;
            identifier      = ToIsoDchars(entry.identifier) + ";1";
        }

        dirEntry->entryOffs = SetPair32(entry.lbaISO);
        dirEntry->entrySize = SetPair32(length);
        dirEntry->volSeqNum = SetPair16(1);
        dirEntry->entryDate = entry.date;

        // Normal case - write out the identifier
        char *identifierBuffer = reinterpret_cast<char *>(dirEntry + 1);
        if (!currentOrParent.has_value())
        {
            dirEntry->identifierLen = identifier.length();
            strncpy(identifierBuffer, identifier.c_str(), identifier.length());
        }
        else
        {
            // Special cases - current/parent directory entry
            dirEntry->identifierLen = 1;
            identifierBuffer[0] = currentOrParent.value() ? '\1' : '\0';
        }
        uint32_t entryLength = AlignTo<2>(sizeof(*dirEntry) + dirEntry->identifierLen);

        /*auto xa = reinterpret_cast<cdxa::ISO_XA_ATTRIB*>(buffer+entryLength);
        xa->identifier[0] = 'X';
        xa->identifier[1] = 'A';
        xa->attributes = SwapBytes16(entry.type == EntryType::EntryDir ? entry.perms | 0x8800 : entry.perms | 0x800);
        xa->ownergroupid = SwapBytes16(entry.GID);
        xa->owneruserid = SwapBytes16(entry.UID);
        entryLength += sizeof(*xa);*/
        entryLength += sizeof(ISO_XA_ATTRIB);

        dirEntry->entryLength = entryLength;

        if (sectorView->GetSpaceInCurrentSector() < entryLength)
            sectorView->NextSector();

        sectorView->WriteMemory(buffer, entryLength);
    };

    auto WriteDirEntries = [&writeOneEntry](const iso::DirTree *dir, const Entry *parentDir)
    {
        auto sectorView = dvd::writer->GetSectorView(layerBegLBA + dir->m_entry->lbaISO, GetSizeInSectors(dir->CalculateDirRecordLen()));
        writeOneEntry(sectorView.get(), *dir->m_entry, false);
        writeOneEntry(sectorView.get(), *parentDir, true);

        for (const auto it : dir->GetView())
        {
            if (it->type != EntryType::EntryDummy && it->hf < 2)
                writeOneEntry(sectorView.get(), *it);
        }
    };

    // Write Root Record first
    WriteDirEntries(this, m_entry);

    const auto &entries = GetUnderlyingList();
    for (auto it = std::next(entries.begin()); it != entries.end(); ++it)
    {
        if (it->type == EntryType::EntryDir)
            WriteDirEntries(it->subdir.get(), it->subdir->m_parent->m_entry);
    }
}

static void SetTag(void *ptr, const uint16_t tagIdent, const uint32_t lba, const uint32_t length = DVD_SECTOR_SIZE - sizeof(tag))
{
    tag *descTag           = reinterpret_cast<tag *>(ptr);
    descTag->tagIdent      = tagIdent;
    descTag->descVersion   = 2;
    descTag->tagLocation   = lba;
    descTag->descCRCLength = length;
    descTag->descCRC       = CalculateCRC(descTag+1, length);
    descTag->tagChecksum   = CalculateChecksum8(descTag);
}

// Emulates the exact buggy behavior of Sony's CDVDGEN to generate a 1:1 volSetIdent
static void SetBuggyVolSetIdent(dstring *volSetIdent, const ISO_DATESTAMP &date)
{
    // Calculate MS-DOS FAT Date and Time, CDVDGEN used it instead of UNIX time
    uint16_t dosYear = (date.year >= 80) ? (date.year - 80) : 0;
    uint16_t dosDate = (dosYear << 9) | (date.month << 5) | date.day;
    uint16_t dosTime = (date.hour << 11) | (date.minute << 5) | (date.second / 2);

    // Combine into a 32-bit integer (Date High, Time Low)
    uint32_t dosDateTime = (dosDate << 16) | dosTime;

    // Extract nibbles backwards (LSB to MSB) and apply the buggy hex offset
    char buggyHex[8];
    for (int i = 0; i < 8; ++i)
    {
        // Shift starts at 0 and goes up by 4 bits each iteration
        uint8_t nibble = (dosDateTime >> (i * 4)) & 0xF;
        
        // Historic Sony CDVDGEN bug: adding to '0' without checking for A-F
        buggyHex[i] = '0' + nibble;
    }

    volSetIdent[0] = COMPRESSION_ID_ALGORITHM_8BIT;
    memcpy(volSetIdent+1, buggyHex, sizeof(buggyHex));
    CopyStringPadWithSpaces(reinterpret_cast<char(&)[40]>(volSetIdent[9]), "SCEI");
    if (iso::isoIdentifiers.VolumeSet != nullptr)
        memcpy(volSetIdent+17, iso::isoIdentifiers.VolumeSet, strnlen(iso::isoIdentifiers.VolumeSet, sizeof(primaryVolDesc::volIdent)));

    volSetIdent[sizeof(primaryVolDesc::volSetIdent) - 1] = 49; // fixed length
}

template <size_t N>
static void SetUdfIdent(uint8_t (&dest)[N], std::string_view src)
{
    memcpy(dest, src.data(), std::min(src.size(), N));
}

template <size_t N>
static void SetUdfDString(dstring (&dest)[N], const char *src)
{
    dest[0] = COMPRESSION_ID_ALGORITHM_8BIT;
    size_t dstrlen = 0;
    if (src != nullptr)
        memcpy(dest + 1, src, dstrlen = strnlen(src, N - 2));

    dest[N - 1] = 1 + dstrlen;
}

void iso::WriteExtendedDescriptors()
{
    uint8_t buffer[sizeof(layout::EXTENDED_AREA)]{};

    // Beginning Extended Area Descriptor
    auto bea = reinterpret_cast<beginningExtendedAreaDesc *>(buffer);
    SetUdfIdent(bea->stdIdent, VSD_STD_ID_BEA01);
    bea->structVersion = 1;

    // NSR Descriptor
    auto nsr = reinterpret_cast<NSRDesc *>(bea+1);
    SetUdfIdent(nsr->stdIdent, VSD_STD_ID_NSR02);
    nsr->structVersion = 1;

    // Terminating Extended Area Descriptor
    auto tea = reinterpret_cast<terminatingExtendedAreaDesc *>(nsr+1);
    SetUdfIdent(tea->stdIdent, VSD_STD_ID_TEA01);
    tea->structVersion = 1;

    auto isoDescriptorSectors = dvd::writer->GetSectorView(layerBegLBA + layout::LBA_UDF_BRIDGE, GetSizeInSectors(sizeof(buffer)));
    isoDescriptorSectors->WriteMemory(buffer, sizeof(buffer));
}

void iso::WriteUdfDescriptors(const uint32_t partitionStartLBA, const uint32_t partitionSize, const uint32_t lba)
{
    int descSeqNum = 1;
    ISO_DATESTAMP date;
    ParseDateFromString(date, iso::isoIdentifiers.CreationDate);

    uint8_t buffer[sizeof(layout::UDF) - sizeof(layout::UDF::tls)]{};

    // Primary Volume Descriptor
    auto pvd = reinterpret_cast<primaryVolDesc *>(buffer);
    SetUdfDString(pvd->volIdent, iso::isoIdentifiers.VolumeID);
    pvd->volSeqNum          = 1;
    pvd->maxVolSeqNum       = 1;
    pvd->interchangeLvl     = UDF_PVD_INTERCHANGE_LVL_SINGLE_VOL;
    pvd->maxInterchangeLvl  = UDF_PVD_INTERCHANGE_LVL_SINGLE_VOL;
    pvd->charSetList        = 1 << UDF_CHAR_SET_TYPE;
    pvd->maxCharSetList     = 1 << UDF_CHAR_SET_TYPE;
    SetBuggyVolSetIdent(pvd->volSetIdent, date);
    SetUdfIdent(pvd->descCharSet.charSetInfo, UDF_CHAR_SET_INFO);
    SetUdfIdent(pvd->explanatoryCharSet.charSetInfo, UDF_CHAR_SET_INFO);
    CopyStringPadWithSpaces(reinterpret_cast<char(&)[23]>(pvd->appIdent.ident), iso::isoIdentifiers.Application);
    pvd->recordingDateAndTime = DateStampToTimeStamp(date);
    SetUdfIdent(pvd->impIdent.ident, GENERATOR);
    SetTag(pvd, TAG_IDENT_PVD, lba);

    // Implementation Use Volume Descriptor
    auto iuvd = reinterpret_cast<impUseVolDesc *>(buffer+offsetof(layout::UDF, iuvd));
    iuvd->volDescSeqNum = descSeqNum;
    SetUdfIdent(iuvd->impIdent.ident, UDF_ID_LV_INFO);
    reinterpret_cast<domainIdentSuffix *>(&iuvd->impIdent.identSuffix.OSClass)->UDFRevision = UDF_REVISION_MINIMUM; // Sony bug
    SetUdfIdent(iuvd->impUse.LVICharset.charSetInfo, UDF_CHAR_SET_INFO);
    SetUdfDString(iuvd->impUse.logicalVolIdent, iso::isoIdentifiers.VolumeID);
    SetUdfDString(iuvd->impUse.LVInfo1, nullptr);
    SetUdfDString(iuvd->impUse.LVInfo2, nullptr);
    SetUdfDString(iuvd->impUse.LVInfo3, nullptr);
    SetUdfIdent(iuvd->impUse.impIdent.ident, GENERATOR);
    SetTag(iuvd, TAG_IDENT_IUVD, lba + descSeqNum++);

    // Partition Descriptor
    auto pd = reinterpret_cast<partitionDesc *>(buffer+offsetof(layout::UDF, pd));
    pd->volDescSeqNum             = descSeqNum;
    pd->partitionFlags            = PD_PARTITION_FLAGS_ALLOC;
    pd->partitionContents.flags   = ENTITYID_FLAGS_PROTECTED;
    SetUdfIdent(pd->partitionContents.ident, PD_PARTITION_CONTENTS_NSR02);
    pd->accessType                = PD_ACCESS_TYPE_READ_ONLY;
    pd->partitionStartingLocation = partitionStartLBA;
    pd->partitionLength           = partitionSize;
    SetUdfIdent(pd->impIdent.ident, GENERATOR);
    SetTag(pd, TAG_IDENT_PD, lba + descSeqNum++);

    // Logical Volume Descriptor
    auto lvd = reinterpret_cast<logicalVolDesc *>(buffer+offsetof(layout::UDF, lvd));
    lvd->volDescSeqNum                       = descSeqNum;
    SetUdfIdent(lvd->descCharSet.charSetInfo, UDF_CHAR_SET_INFO);
    SetUdfDString(lvd->logicalVolIdent, iso::isoIdentifiers.VolumeID);
    lvd->logicalBlockSize                    = DVD_SECTOR_SIZE;
    SetUdfIdent(lvd->domainIdent.ident, UDF_ID_COMPLIANT);
    lvd->domainIdent.identSuffix.UDFRevision = UDF_REVISION_MINIMUM;
    lvd->domainIdent.identSuffix.domainFlags = DOMAIN_FLAGS_HARD_WRITE_PROTECT | DOMAIN_FLAGS_SOFT_WRITE_PROTECT;
    lvd->logicalVolContentsUse.extLength     = sizeof(layout::FSD);
    lvd->mapTableLength                      = sizeof(genericPartitionMap1);
    lvd->numPartitionMaps                    = 1;
    SetUdfIdent(lvd->impIdent.ident, GENERATOR);
    lvd->integritySeqExt.extLength           = sizeof(layout::LVID);
    lvd->integritySeqExt.extLocation         = layout::LBA_LVID;
    lvd->partitionMaps.partitionMapType      = GP_PARTITION_MAP_TYPE_1;
    lvd->partitionMaps.partitionMapLength    = sizeof(genericPartitionMap1);
    lvd->partitionMaps.volSeqNum             = 1;
    SetTag(lvd, TAG_IDENT_LVD, lba + descSeqNum++);

    // Unallocated Space Descriptor
    auto usd = reinterpret_cast<unallocSpaceDesc *>(buffer+offsetof(layout::UDF, usd));
    usd->volDescSeqNum = descSeqNum;
    SetTag(usd, TAG_IDENT_USD, lba + descSeqNum++);

    // Terminating Descriptor
    auto td = reinterpret_cast<terminatingDesc *>(buffer+offsetof(layout::UDF, td));
    SetTag(td, TAG_IDENT_TD, lba + descSeqNum);

    auto udfDescriptorSectors = dvd::writer->GetSectorView(layerBegLBA + lba, GetSizeInSectors(sizeof(layout::UDF)));
    udfDescriptorSectors->WriteMemory(buffer, sizeof(buffer));
    udfDescriptorSectors->WriteBlankSectors(GetSizeInSectors(sizeof(layout::UDF::tls)));
}

void iso::WriteLviDescriptors(const iso::DirTree *dirTree, const uint32_t partitionSize)
{
    ISO_DATESTAMP date;
    ParseDateFromString(date, iso::isoIdentifiers.CreationDate);

    uint8_t buffer[sizeof(layout::LVID)]{};

    auto lvid = reinterpret_cast<logicalVolIntegrityDesc *>(buffer);
    lvid->recordingDateAndTime  = DateStampToTimeStamp(date);
    lvid->integrityType         = LVID_INTEGRITY_TYPE_CLOSE;
    lvid->logicalVolContentsUse.uniqueID = UINT32_MAX;
    lvid->numOfPartitions       = 1;
    lvid->lengthOfImpUse        = AlignTo<4>(sizeof(logicalVolIntegrityDescImpUse));
    lvid->sizeTable             = partitionSize;
    SetUdfIdent(lvid->impUse.impIdent.ident, GENERATOR);
    lvid->impUse.numFiles       = dirTree->GetFileCount();
    lvid->impUse.numDirs        = dirTree->GetDirCount() + 1; // +root
    lvid->impUse.minUDFReadRev  = UDF_REVISION_MINIMUM;
    lvid->impUse.minUDFWriteRev = UDF_REVISION_MINIMUM;
    lvid->impUse.maxUDFWriteRev = UDF_REVISION_MINIMUM;
    SetTag(lvid, TAG_IDENT_LVID, layout::LBA_LVID);

    // Terminating Descriptor
    auto td = reinterpret_cast<terminatingDesc *>(buffer+offsetof(layout::LVID, td));
    SetTag(td, TAG_IDENT_TD, layout::LBA_LVID_TERM);

    auto lvidDescriptorSectors = dvd::writer->GetSectorView(layerBegLBA + layout::LBA_LVID, GetSizeInSectors(sizeof(buffer)));
    lvidDescriptorSectors->WriteMemory(buffer, sizeof(buffer));
}

void iso::WriteAnchorDescriptor(const uint32_t partitionEnd)
{
    uint8_t buffer[DVD_SECTOR_SIZE]{};

    // Main Anchor point
    anchorVolDescPtr *avdp = reinterpret_cast<anchorVolDescPtr *>(buffer);
    avdp->mainVolDescSeqExt.extLength      = sizeof(layout::UDF);
    avdp->mainVolDescSeqExt.extLocation    = layout::LBA_UDF_MAIN;
    avdp->reserveVolDescSeqExt.extLength   = sizeof(layout::UDF);
    avdp->reserveVolDescSeqExt.extLocation = layout::LBA_UDF_RSRV;
    SetTag(buffer, TAG_IDENT_AVDP, layout::LBA_ANCHOR);

    auto anchorDescriptorSector = dvd::writer->GetSectorView(layerBegLBA + layout::LBA_ANCHOR, 1);
    anchorDescriptorSector->WriteMemory(buffer, sizeof(buffer));

    // Backup Anchor point
    avdp->descTag.tagChecksum = 0; // reset
    SetTag(buffer, TAG_IDENT_AVDP, partitionEnd);

    anchorDescriptorSector = dvd::writer->GetSectorView(layerBegLBA + partitionEnd, 1);
    anchorDescriptorSector->WriteMemory(buffer, sizeof(buffer));
}

void iso::DirTree::WriteInfoCtrlBlocks(const uint32_t partitionStartLBA)
{
    auto &entries = GetUnderlyingList();
    auto sectorView = dvd::writer->GetSectorView(layerBegLBA + m_entry->lbaICB, entries.size()); // We only supports one sector per entry (AKA, up to 868GiB per entry)

    auto writeOneEntry = [&sectorView, &partitionStartLBA](Entry &entry, const int id) -> void
    {
        uint8_t buffer[DVD_SECTOR_SIZE]{};

        auto fe = reinterpret_cast<fileEntry *>(buffer);
        fe->icbTag.strategyType   = ICBTAG_STRATEGY_TYPE_4;
        fe->icbTag.numEntries     = 1;
        if (entry.type == EntryType::EntryDir)
        {
            fe->icbTag.fileType   = ICBTAG_FILE_TYPE_DIRECTORY;
            fe->fileLinkCount     = entry.flc > 0 ? entry.flc : entry.subdir->GetDirCount<false>() + 1; // +self
        }
        else
        {
            fe->icbTag.fileType   = ICBTAG_FILE_TYPE_REGULAR;
            fe->fileLinkCount     = 1;
        }
        //fe->icbTag.parentICBLocation;
        fe->icbTag.flags          = ICBTAG_FLAG_NONRELOCATABLE | ICBTAG_FLAG_ARCHIVE | ICBTAG_FLAG_CONTIGUOUS | ICBTAG_FLAG_SYSTEM;
        fe->uid                   = UINT32_MAX;
        fe->gid                   = UINT32_MAX;
        fe->permissions           = FE_PERM_O_EXEC | FE_PERM_O_READ | FE_PERM_G_EXEC | FE_PERM_G_READ | FE_PERM_U_EXEC | FE_PERM_U_READ;
        fe->informationLength     = entry.size;
        fe->logicalBlocksRecorded = entry.size > 0 ? GetSizeInSectors(entry.size) : 0;
        fe->accessTime            = DateStampToTimeStamp(entry.date);
        fe->modificationTime      = fe->accessTime;
        fe->attrTime              = fe->modificationTime;
        fe->checkpoint            = 1;
        SetUdfIdent(fe->impIdent.ident, GENERATOR);
        fe->uniqueID              = id;
        fe->lengthExtendedAttr    = sizeof(extendedAttrHeaderDesc) + sizeof(impUseExtAttr) + sizeof(freeEaSpace) + sizeof(impUseExtAttr) + sizeof(DVDCopyrightImpUse);
        fe->lengthAllocDescs      = sizeof(short_ad) * GetSizeInSectors<UINT32_MAX>(entry.size); // 2 MSBs should be reserved for fragment type, but Sony didn't respect this.

        auto eahd = reinterpret_cast<extendedAttrHeaderDesc *>(fe+1);
        eahd->impAttrLocation     = sizeof(extendedAttrHeaderDesc);
        eahd->appAttrLocation     = fe->lengthExtendedAttr;
        SetTag(eahd, TAG_IDENT_EAHD, entry.lbaICB - partitionStartLBA, sizeof(extendedAttrHeaderDesc) - sizeof(tag));

        auto iuea1 = reinterpret_cast<impUseExtAttr *>(eahd+1);
        iuea1->attrType           = EXTATTR_IMP_USE;
        iuea1->attrSubtype        = EXTATTR_SUBTYPE;
        iuea1->attrLength         = sizeof(impUseExtAttr) + sizeof(freeEaSpace);
        iuea1->impUseLength       = sizeof(freeEaSpace);
        SetUdfIdent(iuea1->impIdent.ident, UDF_ID_FREE_EA);
        reinterpret_cast<domainIdentSuffix *>(&iuea1->impIdent.identSuffix.OSClass)->UDFRevision = UDF_REVISION_MINIMUM; // Sony bug
        auto feas = reinterpret_cast<freeEaSpace *>(iuea1+1);
        feas->headerChecksum      = CalculateChecksum16(iuea1);

        auto iuea2 = reinterpret_cast<impUseExtAttr *>(feas+1);
        iuea2->attrType           = EXTATTR_IMP_USE;
        iuea2->attrSubtype        = EXTATTR_SUBTYPE;
        iuea2->attrLength         = sizeof(impUseExtAttr) + sizeof(DVDCopyrightImpUse);
        iuea2->impUseLength       = sizeof(DVDCopyrightImpUse);
        SetUdfIdent(iuea2->impIdent.ident, UDF_ID_DVD_CGMS);
        reinterpret_cast<domainIdentSuffix *>(&iuea2->impIdent.identSuffix.OSClass)->UDFRevision = UDF_REVISION_MINIMUM; // Sony bug
        auto dvdciu = reinterpret_cast<DVDCopyrightImpUse *>(iuea2+1);
        dvdciu->headerChecksum    = CalculateChecksum16(iuea2);

        auto sad = reinterpret_cast<short_ad *>(dvdciu+1);
        for (uint64_t i = 0, remaining = entry.size, currentLBA = entry.lba - partitionStartLBA; ; ++i)
        {
            sad[i].extLength      = std::min<uint64_t>(remaining, UINT32_MAX); // 2 MSBs should be reserved for fragment type, but Sony didn't respect this.
            sad[i].extPosition    = currentLBA;
            if ((remaining -= sad[i].extLength) == 0)
                break;
            currentLBA += GetSizeInSectors(sad[i].extLength);
        }

        // At this point, we don't need more the real entry size, so we will reuse it as the size of the ICB needed by FID
        entry.size = sizeof(*fe) + fe->lengthExtendedAttr + fe->lengthAllocDescs;
        SetTag(buffer, TAG_IDENT_FE, entry.lbaICB - partitionStartLBA, entry.size - sizeof(tag));

        sectorView->WriteMemory(buffer, sizeof(buffer));
    };

    // Write Root ICB first
    writeOneEntry(*m_entry, 0);

    int id = 16;
    for (auto it = std::next(entries.begin()); it != entries.end(); ++it)
    {
        if (it->type == EntryType::EntryDummy)
            continue;

        writeOneEntry(*it, id++);
    }
}

void iso::DirTree::WriteFileSetDescriptors(const uint32_t partitionStartLBA) const
{
    ISO_DATESTAMP date;
    ParseDateFromString(date, iso::isoIdentifiers.CreationDate);

    uint8_t buffer[sizeof(layout::FSD)]{};

    auto fsd = reinterpret_cast<fileSetDesc *>(buffer);
    fsd->recordingDateAndTime = DateStampToTimeStamp(date);
    fsd->interchangeLvl       = UDF_FSD_INTERCHANGE_LVL;
    fsd->maxInterchangeLvl    = UDF_FSD_INTERCHANGE_LVL;
    fsd->charSetList          = 1 << UDF_CHAR_SET_TYPE;
    fsd->maxCharSetList       = 1 << UDF_CHAR_SET_TYPE;
    SetUdfIdent(fsd->logicalVolIdentCharSet.charSetInfo, UDF_CHAR_SET_INFO);
    SetUdfDString(fsd->logicalVolIdent, iso::isoIdentifiers.VolumeID);
    SetUdfIdent(fsd->fileSetCharSet.charSetInfo, UDF_CHAR_SET_INFO);
    SetUdfDString(fsd->fileSetIdent, "PLAYSTATION2 DVD-ROM FILE SET");
    fsd->rootDirectoryICB.extLength = m_entry->size;
    fsd->rootDirectoryICB.extLocation.logicalBlockNum = m_entry->lbaICB - partitionStartLBA;
    SetUdfIdent(fsd->domainIdent.ident, UDF_ID_COMPLIANT);
    fsd->domainIdent.identSuffix.UDFRevision = UDF_REVISION_MINIMUM;
    fsd->domainIdent.identSuffix.domainFlags = DOMAIN_FLAGS_HARD_WRITE_PROTECT | DOMAIN_FLAGS_SOFT_WRITE_PROTECT;
    SetTag(fsd, TAG_IDENT_FSD, 0);

    // Terminating Descriptor
    auto td = reinterpret_cast<terminatingDesc *>(buffer+offsetof(layout::FSD, td));
    SetTag(td, TAG_IDENT_TD, partitionStartLBA + 1);

    auto fsdDescriptorSectors = dvd::writer->GetSectorView(layerBegLBA + partitionStartLBA, GetSizeInSectors(sizeof(buffer)));
    fsdDescriptorSectors->WriteMemory(buffer, sizeof(buffer));
}

void iso::DirTree::WriteFileIdDescriptors(const uint32_t partitionStartLBA)
{
    auto writeOneEntry = [&partitionStartLBA](dvd::IsoWriter::SectorView *sectorView, const Entry &entry, uint32_t &fidLBA, const bool parent) -> void
    {
        uint8_t buffer[AlignTo<4>(sizeof(fileIdentDesc) + 1 + 31 * 2)]{}; // 1 Compression ID + 31 File Identifier chars in UTF-16

        auto dirEntry = reinterpret_cast<fileIdentDesc *>(buffer);
        dirEntry->fileVersionNum      = 1;
        dirEntry->fileCharacteristics = entry.type != EntryType::EntryDir ? entry.hf : entry.hf | FID_FILE_CHAR_DIRECTORY;
        dirEntry->icb.extLength       = entry.size;
        dirEntry->icb.extLocation.logicalBlockNum = entry.lbaICB - partitionStartLBA;

        if (!parent)
        {
            uint8_t *identifierBuffer = reinterpret_cast<uint8_t *>(dirEntry+1);
            identifierBuffer[dirEntry->lengthFileIdent++] = COMPRESSION_ID_ALGORITHM_16BIT;

            // Use std::filesystem::path to convert UTF-8 -> UTF-16 automatically
            std::u16string u16Str = fs::path(entry.identifier).u16string();
            for (char16_t codeUnit : u16Str)
            {
                // First store High Byte, then the Low Byte.
                identifierBuffer[dirEntry->lengthFileIdent++] = static_cast<char>(codeUnit >> 8);
                identifierBuffer[dirEntry->lengthFileIdent++] = static_cast<char>(codeUnit);
            }
        }
        else
        {
            dirEntry->fileCharacteristics |= FID_FILE_CHAR_PARENT;
        }

        size_t entryLength = AlignTo<4>(sizeof(*dirEntry) + dirEntry->lengthFileIdent);
        SetTag(buffer, TAG_IDENT_FID, fidLBA - partitionStartLBA, entryLength - sizeof(tag));

        if (sectorView->GetSpaceInCurrentSector() <= entryLength)
            fidLBA++;

        sectorView->WriteMemory(buffer, entryLength);
    };

    for (Entry &entry : GetUnderlyingList())
    {
        if (entry.type != EntryType::EntryDir)
            continue;

        auto sectorView = dvd::writer->GetSectorView(layerBegLBA + entry.lba, GetSizeInSectors(entry.subdir->CalculateFileIdDescLen()));
        writeOneEntry(sectorView.get(), entry, entry.lba, true);

        for (const auto it : entry.subdir->GetView())
        {
            if (it->type != EntryType::EntryDummy && it->hf < 2)
                writeOneEntry(sectorView.get(), *it, entry.lba, false);
        }
    }
}
