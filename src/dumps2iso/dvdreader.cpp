#include "dvdreader.h"
#include "platform.h"

using namespace dvd;

bool IsoReader::Open(const fs::path &fileName)
{
    m_filePtr = OpenScopedFile(fileName, "rb");

    if (m_filePtr == nullptr) [[unlikely]]
        return false;
    setvbuf(m_filePtr.get(), nullptr, _IOFBF, 1024*1024);

    m_currentByte   = 0;
    m_currentSector = 0;
    m_totalSectors  = GetSize(fileName) / DVD_SECTOR_SIZE;

    return true;
}

void IsoReader::Close()
{
    m_filePtr.reset();
}

size_t IsoReader::BulkReadBytes(void *ptr, const uint32_t sector, const size_t bytes)
{
    SeekFile(m_filePtr.get(), DVD_SECTOR_SIZE * static_cast<int64_t>(sector), SEEK_SET);
    return fread(ptr, 1, bytes, m_filePtr.get());
}

template <bool singleSector, bool Skip>
inline size_t IsoReader::ReadBytesImpl(void *ptr, size_t bytes)
{
    if (bytes == 0) [[unlikely]]
        return 0;

    size_t bytesRead    = 0;
    char *const dataPtr = static_cast<char *>(ptr);

    if (m_currentSector >= m_totalSectors) [[unlikely]]
        goto eof_fill;

    do
    {
        if (m_currentByte >= DVD_SECTOR_SIZE)
        {
            if constexpr (singleSector)
                return bytesRead;
            else if (!PrepareNextSector()) [[unlikely]]
                goto eof_fill;
        }

        const size_t toRead = std::min(DVD_SECTOR_SIZE - m_currentByte, bytes);

        if constexpr (!Skip)
            memcpy(dataPtr + bytesRead, m_sectorBuff + m_currentByte, toRead);

        bytes         -= toRead;
        bytesRead     += toRead;
        m_currentByte += toRead;

    } while (bytes > 0);

    return bytesRead;

eof_fill:
    if constexpr (!Skip)
        memset(dataPtr + bytesRead, 0, bytes);

    return bytesRead;
}

template <bool singleSector>
size_t IsoReader::ReadBytes(void *ptr, size_t bytes)
{
    return ReadBytesImpl<singleSector, false>(ptr, bytes);
}

template <bool singleSector>
size_t IsoReader::SkipBytes(size_t bytes)
{
    return ReadBytesImpl<singleSector, true>(nullptr, bytes);
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
    {
        m_currentByte = DVD_SECTOR_SIZE;
        return false;
    }

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

template size_t IsoReader::ReadBytes<true>(void *ptr, size_t bytes);
template size_t IsoReader::ReadBytes<false>(void *ptr, size_t bytes);
template size_t IsoReader::SkipBytes<true>(size_t bytes);
template size_t IsoReader::SkipBytes<false>(size_t bytes);
