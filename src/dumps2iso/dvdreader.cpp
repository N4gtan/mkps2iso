#include "dvdreader.h"
#include "platform.h"

using namespace dvd;

bool IsoReader::Open(const fs::path &fileName)
{
    m_filePtr = OpenScopedFile(fileName, "rb");

    if (m_filePtr == nullptr) [[unlikely]]
        return false;

    m_mmap = std::make_unique<MMappedFile>();
    m_mmap->Open(fileName);

    m_totalSectors = GetSize(fileName) / DVD_SECTOR_SIZE;

    if (fread(m_sectorBuff, DVD_SECTOR_SIZE, 1, m_filePtr.get()) != 1) [[unlikely]]
    {
        Close();
        return false;
    }

    m_currentByte   = 0;
    m_currentSector = 0;

    return true;
}

void IsoReader::Close()
{
    m_filePtr.reset();
    m_mmap.reset();
}

template <bool singleSector>
inline size_t IsoReader::ReadBytesImpl(void *ptr, size_t bytes)
{
    size_t bytesRead    = 0;
    char *const dataPtr = static_cast<char *>(ptr);

    if (m_currentSector >= m_totalSectors) [[unlikely]]
        goto eof_fill;

    while (bytes > 0)
    {
        if (m_currentByte >= DVD_SECTOR_SIZE) [[unlikely]]
            goto check_next_sector;

        {
            const size_t toRead = std::min(DVD_SECTOR_SIZE - m_currentByte, bytes);

            if (dataPtr != nullptr)  [[likely]]
                memcpy(dataPtr + bytesRead, &m_sectorBuff[m_currentByte], toRead);

            bytes         -= toRead;
            bytesRead     += toRead;
            m_currentByte += toRead;
        }

        if (m_currentByte >= DVD_SECTOR_SIZE)
        {
        check_next_sector:
            if constexpr (singleSector)
                return bytesRead;
            else if (!PrepareNextSector()) [[unlikely]]
                goto eof_fill;
        }
    }

    return bytesRead;

eof_fill:
    if (dataPtr != nullptr && bytes > 0) [[unlikely]]
        memset(dataPtr + bytesRead, 0, bytes);

    return bytesRead;
}

template <bool singleSector>
size_t IsoReader::ReadBytes(void *ptr, size_t bytes)
{
    return ReadBytesImpl<singleSector>(ptr, bytes);
}

template <bool singleSector>
size_t IsoReader::SkipBytes(size_t bytes)
{
    return ReadBytesImpl<singleSector>(nullptr, bytes);
}

bool IsoReader::PrepareNextSector()
{
    m_currentByte = 0;
    m_currentSector++;

    if (fread(m_sectorBuff, DVD_SECTOR_SIZE, 1, m_filePtr.get()) != 1) [[unlikely]]
        return false;

    return true;
}

bool IsoReader::SeekToSector(const uint32_t sector)
{
    if (sector >= m_totalSectors || m_filePtr == nullptr) [[unlikely]]
        return false;

    if (SeekFile(m_filePtr.get(), DVD_SECTOR_SIZE * static_cast<int64_t>(sector), SEEK_SET) != 0 ||
        fread(m_sectorBuff, DVD_SECTOR_SIZE, 1, m_filePtr.get()) != 1) [[unlikely]]
        return false;

    m_currentByte   = 0;
    m_currentSector = sector;

    return true;
}

bool IsoReader::SeekToByte(const size_t offs)
{
    if (!SeekToSector(offs / DVD_SECTOR_SIZE)) [[unlikely]]
        return false;

    m_currentByte = offs % DVD_SECTOR_SIZE;

    return true;
}

size_t IsoReader::GetPos() const
{
    return (DVD_SECTOR_SIZE * static_cast<size_t>(m_currentSector)) + m_currentByte;
}

MMappedFile::View IsoReader::GetSectorView(const uint32_t offsetLBA, const uint32_t sizeLBA) const
{
    return m_mmap->GetView(static_cast<uint64_t>(offsetLBA) * DVD_SECTOR_SIZE, static_cast<size_t>(sizeLBA) * DVD_SECTOR_SIZE);
}

template size_t IsoReader::ReadBytes<true>(void *ptr, size_t bytes);
template size_t IsoReader::ReadBytes<false>(void *ptr, size_t bytes);
template size_t IsoReader::SkipBytes<true>(size_t bytes);
template size_t IsoReader::SkipBytes<false>(size_t bytes);
