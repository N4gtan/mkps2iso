#pragma once

#include "common.h"
#include "mmappedfile.h"

namespace dvd
{
    // Reader class which allows you to read data from an PS2 image
    class IsoReader
    {
        // Memory mapped file to handle bulk reads
        std::unique_ptr<MMappedFile> m_mmap;
        // RAII pointer to opened file
        unique_file m_filePtr = nullptr;
        // Sector buffer size
        uint8_t m_sectorBuff[DVD_SECTOR_SIZE]{};
        // Current data offset in current sector
        size_t m_currentByte = 0;
        // Current sector number
        uint32_t m_currentSector = 0;
        // Total number of sectors in the file
        uint32_t m_totalSectors = 0;

    public:
        // Open file
        bool Open(const fs::path &fileName);

        // Read bytes from sector (supports sequential reading)
        template <bool singleSector = false>
        size_t ReadBytes(void *ptr, size_t bytes);

        // Skip bytes from sector (supports sequential skipping)
        template <bool singleSector = false>
        size_t SkipBytes(size_t bytes);

        // Seek to a sector in sector units (returns true if success)
        bool SeekToSector(const uint32_t sector);

        // Seek to a data offset in byte units (returns true if success)
        bool SeekToByte(const size_t offs);

        // Get current offset in byte units
        size_t GetPos() const;

        const uint8_t *GetSectorBuff() const { return m_sectorBuff; }

        // Close file
        void Close();

        // Return a full view of a specific sector range for bulk copying
        MMappedFile::View GetSectorView(const uint32_t offsetLBA, const uint32_t sizeLBA) const;

    private:
        bool PrepareNextSector();

        template <bool singleSector>
        size_t ReadBytesImpl(void *ptr, size_t bytes);
    };

    inline std::unique_ptr<IsoReader> reader;
}
