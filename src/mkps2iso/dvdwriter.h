#pragma once

#include "mmappedfile.h"

namespace dvd
{
    class IsoWriter
    {
    public:
        class SectorView
        {
        public:
            SectorView(const MMappedFile *mappedFile, const uint32_t offsetLBA, const uint32_t sizeLBA);
            ~SectorView();

            void WriteFile(FILE *file);
            void WriteMemory(const void *memory, const size_t size);
            void WriteBlankSectors(const uint32_t count);
            void NextSector();
            size_t GetSpaceInView() const;
            size_t GetSpaceInCurrentSector() const;

        private:
            MMappedFile::View m_view;
            uint8_t *m_sectorPtr = nullptr;
            uint32_t m_currentLBA = 0;
            uint32_t m_endLBA = 0;
            size_t m_offsetInSector = 0;
        };

        bool Create(const fs::path &fileName, const uint32_t sizeLBA);
        void Close();

        std::unique_ptr<SectorView> GetSectorView(const uint32_t offsetLBA, const uint32_t sizeLBA) const;

    private:
        std::unique_ptr<MMappedFile> m_mmap;
    };

    inline std::unique_ptr<IsoWriter> writer;
}
