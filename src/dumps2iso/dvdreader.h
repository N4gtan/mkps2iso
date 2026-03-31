#pragma once

#include "common.h"

namespace dvd
{
    // Reader class which allows you to read data from an PS2 image
    class IsoReader
    {
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

        // Bulk sequential read from sector
        size_t BulkReadBytes(void *ptr, uint32_t sector, size_t bytes);

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

        uint32_t GetTotalSectors() const { return m_totalSectors; }
        const uint8_t *GetSectorBuff() const { return m_sectorBuff; }

        // Close file
        void Close();

    private:
        bool PrepareNextSector();

        template <bool singleSector>
        size_t ReadBytesImpl(void *ptr, size_t bytes);
    };

    inline std::unique_ptr<IsoReader> reader;
}
