#include "dvdwriter.h"
#include <cstring>

#define DVD_SECTOR_SIZE 2048
using namespace dvd;

bool IsoWriter::Create(const fs::path &fileName, const uint32_t sizeLBA)
{
    const uint64_t sizeBytes = static_cast<uint64_t>(sizeLBA) * DVD_SECTOR_SIZE;

    m_mmap = std::make_unique<MMappedFile>();
    return m_mmap->Create(fileName, sizeBytes);
}

void IsoWriter::Close()
{
    m_mmap.reset();
}

// ======================================================

IsoWriter::SectorView::SectorView(const MMappedFile *mappedFile, const uint32_t offsetLBA, const uint32_t sizeLBA)
    : m_view(mappedFile->GetView(static_cast<uint64_t>(offsetLBA) * DVD_SECTOR_SIZE, static_cast<size_t>(sizeLBA) * DVD_SECTOR_SIZE))
    , m_currentLBA(offsetLBA)
    , m_endLBA(offsetLBA + sizeLBA)
{
    m_sectorPtr = static_cast<uint8_t *>(m_view.GetBuffer());
}

IsoWriter::SectorView::~SectorView()
{
    if (m_offsetInSector != 0)
        NextSector();
}

void IsoWriter::SectorView::WriteFile(FILE *file)
{
    /*if (m_offsetInSector != 0) [[unlikely]]
        NextSector();*/

    const size_t bytesRead = fread(m_sectorPtr, 1, GetSpaceInView(), file);

    if (bytesRead == 0) [[unlikely]]
        return NextSector();

    m_offsetInSector = bytesRead % DVD_SECTOR_SIZE;
    m_currentLBA    += bytesRead / DVD_SECTOR_SIZE;
    m_sectorPtr     += bytesRead - m_offsetInSector;

    if (m_offsetInSector > 0)
        NextSector();
}

void IsoWriter::SectorView::WriteMemory(const void *memory, const size_t size)
{
    const size_t memToCopy = std::min(size, GetSpaceInView());

    if (memToCopy == 0) [[unlikely]]
        return;

    memcpy(m_sectorPtr + m_offsetInSector, memory, memToCopy);

    const size_t totalOffset = m_offsetInSector + memToCopy;
    m_offsetInSector = totalOffset % DVD_SECTOR_SIZE;
    m_currentLBA    += totalOffset / DVD_SECTOR_SIZE;
    m_sectorPtr     += totalOffset - m_offsetInSector;
}

void IsoWriter::SectorView::WriteBlankSectors(const uint32_t count)
{
    /*if (m_offsetInSector != 0) [[unlikely]]
        NextSector();*/

    const size_t sectorsToWrite = std::min(m_endLBA - m_currentLBA, count);

    if (sectorsToWrite == 0) [[unlikely]]
        return;

    memset(m_sectorPtr, 0, sectorsToWrite * DVD_SECTOR_SIZE);

    m_currentLBA += sectorsToWrite;
    m_sectorPtr  += sectorsToWrite * DVD_SECTOR_SIZE;
}

void IsoWriter::SectorView::NextSector()
{
    /*if (m_currentLBA >= m_endLBA) [[unlikely]]
        return;*/

    // Fill the remainder of the sector with zeroes if applicable
    memset(m_sectorPtr + m_offsetInSector, 0, GetSpaceInCurrentSector());

    m_offsetInSector = 0;
    m_currentLBA++;
    m_sectorPtr     += DVD_SECTOR_SIZE;
}

size_t IsoWriter::SectorView::GetSpaceInView() const
{
    return (static_cast<size_t>(m_endLBA - m_currentLBA) * DVD_SECTOR_SIZE) - m_offsetInSector;
}

size_t IsoWriter::SectorView::GetSpaceInCurrentSector() const
{
    return DVD_SECTOR_SIZE - m_offsetInSector;
}

auto IsoWriter::GetSectorView(const uint32_t offsetLBA, const uint32_t sizeLBA) const -> std::unique_ptr<SectorView>
{
    return std::make_unique<SectorView>(m_mmap.get(), offsetLBA, sizeLBA);
}

// ======================================================
