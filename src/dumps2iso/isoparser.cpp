#include "isoparser.h"
#include "dvdreader.h"

// Partition start LBA for UDF reading
static uint32_t partitionStartLBA = 0;

iso::PathTable::PathTable(const int lba, const uint32_t size)
{
    ReadPathTable(lba, size);
}

void iso::PathTable::FreePathTable()
{
    entries.clear();
}

size_t iso::PathTable::ReadPathTable(const int lba, const size_t size)
{
    if (lba < 0 || !dvd::reader->SeekToSector(lba) || size <= sizeof(ISO_PATHTABLE_ENTRY))  [[unlikely]]
        return 0;

    FreePathTable();

    size_t bytesRead = 0;
    while (bytesRead < size)
    {
        PathTableEntry pathTableEntry;
        bytesRead += dvd::reader->ReadBytes(&pathTableEntry.data, sizeof(pathTableEntry.data));

        // Its the end of the path table when its nothing but zeros
        // ECMA-119 7.6.3 - The length of a Directory Identifier shall not exceed 31
        if (pathTableEntry.data.identifierLen == 0 || pathTableEntry.data.identifierLen > 31) [[unlikely]]
            break;

        // Read entry name
        const size_t length = pathTableEntry.data.identifierLen;
        pathTableEntry.identifier.resize(length);
        bytesRead += dvd::reader->ReadBytes(pathTableEntry.identifier.data(), length);

        // ECMA-119 9.4.6 - 00 field present only if entry length is an odd number
        if ((length % 2) != 0)
            bytesRead += dvd::reader->SkipBytes(1);

        // Strip trailing zeroes, if any
        pathTableEntry.identifier.resize(strlen(pathTableEntry.identifier.c_str()));

        entries.emplace_back(std::move(pathTableEntry));
    }

    return entries.size();
}

fs::path iso::PathTable::GetFullDirPath(int dirEntry) const
{
    fs::path path;

    while (true)
    {
        if (entries[dirEntry].identifier.empty())
            break;

        // Prepend!
        path = entries[dirEntry].identifier / path;

        dirEntry = entries[dirEntry].data.parentDirIndex - 1;
    }

    return path;
}

iso::DirTree::DirTree(ListView<Entry> view)
    : ListView<Entry>(std::move(view))
{
}

template <bool udf>
std::optional<Entry> iso::DirTree::ReadRootDir(const int lba)
{
    dvd::reader->SeekToSector(lba);
    auto entry = [&]() -> std::optional<Entry>
    {
        if constexpr (!udf)
        {
            return ReadEntryISO();
        }
        else
        {
            const anchorVolDescPtr *avdp = reinterpret_cast<const anchorVolDescPtr *>(dvd::reader->GetSectorBuff());
            const int begSeq = avdp->mainVolDescSeqExt.extLocation;
            const int endSeq = begSeq + (avdp->mainVolDescSeqExt.extLength / DVD_SECTOR_SIZE);

            int fsdLogical = 0;
            bool foundPD   = false;
            bool foundLVD  = false;
            for (int i = begSeq; i < endSeq; ++i)
            {
                dvd::reader->SeekToSector(i);
                const tag *descTag = reinterpret_cast<const tag *>(dvd::reader->GetSectorBuff());

                if (descTag->tagIdent == TAG_IDENT_PD && !foundPD)
                {
                    partitionStartLBA = reinterpret_cast<const partitionDesc *>(dvd::reader->GetSectorBuff())->partitionStartingLocation;
                    foundPD    = true;
                }
                else if (descTag->tagIdent == TAG_IDENT_LVD && !foundLVD)
                {
                    fsdLogical = reinterpret_cast<const logicalVolDesc *>(dvd::reader->GetSectorBuff())->logicalVolContentsUse.extLocation.logicalBlockNum;
                    foundLVD   = true;
                }
                else if (descTag->tagIdent == TAG_IDENT_TD)
                {
                    break;
                }

                if (foundPD && foundLVD)
                    break;
            }

            if (!foundPD || !foundLVD) [[unlikely]]
                return std::nullopt;

            dvd::reader->SeekToSector(partitionStartLBA + fsdLogical);

            Entry entry{};
            entry.lbaICB = partitionStartLBA + reinterpret_cast<const fileSetDesc *>(dvd::reader->GetSectorBuff())->rootDirectoryICB.extLocation.logicalBlockNum;
            dvd::reader->SeekToSector(entry.lbaICB);
            ReadICB(entry);
            return entry;
        }
    }();

    if (!entry) [[unlikely]]
        return std::nullopt;

    entry->order = -1;
    return entry;
}

template <bool udf>
void iso::DirTree::ReadDirEntries(int lba, const size_t size)
{
    int entryCount          = 0;
    constexpr int entrySkip = udf ? 1 : 2;
    const size_t endPos     = DVD_SECTOR_SIZE * static_cast<size_t>(lba) + size;

read_sector:
    dvd::reader->SeekToSector(lba++);
    while (dvd::reader->GetPos() < endPos)
    {
        auto entry = [&]() -> std::optional<Entry> {
            if constexpr (udf)
                return ReadEntryUDF();
            else
                return ReadEntryISO();
        }();

        // End of sector
        if (!entry) [[unlikely]]
        {
            if constexpr (udf)
                break;
            else
                goto read_sector;
        }

        if (entryCount++ < entrySkip) [[unlikely]]
            continue;

        entry->order = entryCount - entrySkip - 1;
        EmplaceBack(std::move(entry.value()));
    }

    // Sort the directory by LBA for pretty printing
    SortView([](const auto &left, const auto &right)
             { return left->lba < right->lba; });
}

std::optional<Entry> iso::DirTree::ReadEntryISO()
{
    ISO_DIR_ENTRY iso;

    // Read 33 byte directory entry
    size_t bytesRead = dvd::reader->ReadBytes<true>(&iso, sizeof(iso));

    // The file entry table usually ends with null bytes so break if we reached that area
    // ECMA-119 7.5.1 - The length of a File Identifier shall not exceed 31 + ';1'
    if (bytesRead != sizeof(iso) || iso.entryLength == 0 || iso.identifierLen > 33) [[unlikely]]
        return std::nullopt;

    Entry entry{};

    // Read identifier string
    entry.identifier.resize(iso.identifierLen);
    bytesRead += dvd::reader->ReadBytes<true>(entry.identifier.data(), iso.identifierLen);

    // ECMA-119 9.1.12 - 00 field present only if file identifier length is an even number
    if ((iso.identifierLen % 2) == 0)
        bytesRead += dvd::reader->SkipBytes<true>(1);

    // Skip irrelevant XA attribute data
    bytesRead += dvd::reader->SkipBytes<true>(sizeof(ISO_XA_ATTRIB));

    // Skip unsupported System Use extensions
    if (bytesRead < iso.entryLength) [[unlikely]]
        bytesRead += dvd::reader->SkipBytes<true>(iso.entryLength - bytesRead);

    // If there is still a difference, then it's either an invalid entry or a corrupted sector
    if (bytesRead != iso.entryLength) [[unlikely]]
        return std::nullopt;

    // Set Entry Data
    entry.lba  = entry.lbaICB = iso.entryOffs.lsb;
    entry.size = iso.entrySize.lsb;
    entry.date = iso.entryDate;
    entry.type = iso.flags & FID_FILE_CHAR_DIRECTORY ? EntryType::EntryDir : EntryType::EntryFile;
    entry.hf   = iso.flags & FID_FILE_CHAR_HIDDEN;

    // Strip trailing zeroes, if any
    entry.identifier.resize(strlen(entry.identifier.c_str()));

    // We don't need a dirty ID, clean it!
    if (entry.type != EntryType::EntryDir)
        entry.identifier = entry.identifier.substr(0, entry.identifier.find_last_of(';'));

    return entry;
}

std::optional<Entry> iso::DirTree::ReadEntryUDF()
{
    fileIdentDesc fid;

    // Read 38 byte directory entry
    size_t bytesRead = dvd::reader->ReadBytes(&fid, sizeof(fid));

    // The file entry table usually ends with null bytes so break if we reached that area
    if (bytesRead != sizeof(fid) || (fid.lengthFileIdent < 2 && (fid.fileCharacteristics & FID_FILE_CHAR_PARENT) == 0)) [[unlikely]]
        return std::nullopt;

    Entry entry{};

    // Read identifier string
    if (fid.lengthFileIdent > 0)
    {
        uint8_t rawIdent[0xFF];
        bytesRead += dvd::reader->ReadBytes(rawIdent, fid.lengthFileIdent);

        if (rawIdent[0] == COMPRESSION_ID_ALGORITHM_16BIT)
        {
            std::u16string u16Str;
            u16Str.reserve((fid.lengthFileIdent - 1) / 2);

            for (size_t i = 1; i < fid.lengthFileIdent; i += 2)
            {
                // Combine High Byte (i) and Low Byte (i+1) directly. No Swap needed.
                u16Str.push_back(static_cast<char16_t>((rawIdent[i] << 8) | rawIdent[i + 1]));
            }
            // Use std::filesystem::path to convert UTF-16 -> UTF-8 automatically
            entry.identifier = fs::path(u16Str).string();
        }
        else if (rawIdent[0] == COMPRESSION_ID_ALGORITHM_8BIT) [[unlikely]]
        {
            // Simple cast/copy starting from offset 1
            entry.identifier.assign(reinterpret_cast<const char *>(rawIdent + 1), fid.lengthFileIdent - 1);
        }
    }

    // Skip unsupported Implementation Use
    if (fid.lengthOfImpUse > 0)  [[unlikely]]
        bytesRead += dvd::reader->SkipBytes(fid.lengthOfImpUse);

    // Skip alignment padding field(s)
    dvd::reader->SkipBytes(-static_cast<intptr_t>(bytesRead) & 3);

    // Save current pos, we need to read the ICB to get the entry data
    size_t returnPos = dvd::reader->GetPos();

    // Seek to ICB
    entry.lbaICB = partitionStartLBA + fid.icb.extLocation.logicalBlockNum;
    dvd::reader->SeekToSector(entry.lbaICB);

    // Set Entry Data
    ReadICB(entry);
    entry.hf = fid.fileCharacteristics & FID_FILE_CHAR_HIDDEN;
    
    // Return to FID pos
    dvd::reader->SeekToByte(returnPos);

    return entry;
}

void iso::DirTree::ReadICB(Entry &entry)
{
    fileEntry icb;
    dvd::reader->ReadBytes<true>(&icb, sizeof(icb));

    entry.lba  = partitionStartLBA + reinterpret_cast<const short_ad *>(dvd::reader->GetSectorBuff() + sizeof(icb) + icb.lengthExtendedAttr)->extPosition;
    entry.size = icb.informationLength;
    entry.date = TimeStampToDateStamp(icb.modificationTime);
    entry.type = icb.icbTag.fileType == ICBTAG_FILE_TYPE_DIRECTORY ? EntryType::EntryDir : EntryType::EntryFile;
    entry.flc  = entry.type == EntryType::EntryDir ? icb.fileLinkCount : 0;
}

static std::unique_ptr<iso::DirTree> ParsePathTable(ListView<Entry> view, const std::vector<PathTableEntry> &pathTableList,
    const uint32_t parentIndex, const fs::path &path)
{
    auto dirEntries = std::make_unique<iso::DirTree>(std::move(view));

    // Get Directory Record size
    size_t dirRecordSectors = dirEntries->ReadRootDir<false>(pathTableList[parentIndex].data.dirOffs).value().size;

    // Read Directory Record
    dirEntries->ReadDirEntries<false>(pathTableList[parentIndex].data.dirOffs, dirRecordSectors);

    // Only add missing directories to the list
    for (auto ptIt = std::next(pathTableList.begin()); ptIt != pathTableList.end(); ++ptIt)
    {
        if (ptIt->data.parentDirIndex-1u == parentIndex &&
            !std::any_of(dirEntries->GetView().begin(), dirEntries->GetView().end(), [&ptIt](const auto dirIt)
                         { return dirIt->identifier == ptIt->identifier; }))
        {
            dirEntries->EmplaceBack(std::move(dirEntries->ReadRootDir<false>(ptIt->data.dirOffs).value())).hf |= 0x02; // simulate obfuscation
        }
    }

    for (const auto it : dirEntries->GetView())
    {
        it->path = path;

        if (it->type == EntryType::EntryDir)
        {
            int childIndex = -1;
            for (size_t i = 1; i < pathTableList.size(); ++i)
            {
                const auto &ptEntry = pathTableList[i];
                if (ptEntry.data.dirOffs == it->lba)
                {
                    childIndex = i;
                    it->identifier = ptEntry.identifier;
                    break;
                }
            }

            if (childIndex < 0)
                continue;

            it->subdir = ParsePathTable(dirEntries->NewView(), pathTableList, childIndex, path / it->identifier);
        }
    }

    return dirEntries;
}

template <bool udf>
static std::unique_ptr<iso::DirTree> ParseSubDirectory(ListView<Entry> view, const int lba, const size_t size, const fs::path &path)
{
    auto dirEntries = std::make_unique<iso::DirTree>(std::move(view));
    dirEntries->ReadDirEntries<udf>(lba, size);

    for (const auto it : dirEntries->GetView())
    {
        it->path = path;
        if (it->type == EntryType::EntryDir)
            it->subdir = ParseSubDirectory<udf>(dirEntries->NewView(), it->lba, it->size, path / it->identifier);
    }

    return dirEntries;
}

template <bool udf>
Entry &iso::ParseRoot(std::list<Entry> &entries, const uint32_t lba, const std::vector<PathTableEntry> *pathTableList)
{
    std::optional<Entry> entry = iso::DirTree::ReadRootDir<udf>(lba);

    if (!entry.has_value())
    {
        printf("\nERROR: Root directory is empty or invalid.\n");
        exit(EXIT_FAILURE);
    }

    Entry &root = entries.emplace_back(std::move(entry.value()));
    root.subdir = pathTableList == nullptr
        ? ParseSubDirectory<udf>(ListView(entries), root.lba, root.size, root.identifier)
        : ParsePathTable(ListView(entries), *pathTableList, 0, root.identifier);

    return root;
}

template Entry &iso::ParseRoot<true>(std::list<Entry> &entries, const uint32_t lba, const std::vector<PathTableEntry> *pathTableList);
template Entry &iso::ParseRoot<false>(std::list<Entry> &entries, const uint32_t lba, const std::vector<PathTableEntry> *pathTableList);
