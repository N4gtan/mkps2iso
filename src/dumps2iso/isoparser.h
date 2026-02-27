#pragma once

#include "common.h"
#include "listview.h"

namespace iso
{
    class PathTable
    {
    public:
        std::vector<PathTableEntry> entries;

        // Default constructor for standard initialization
        PathTable() = default;

        // Parameterized constructor to initialize and read the table immediately
        PathTable(const int lba, const uint32_t size);

        void FreePathTable();
        size_t ReadPathTable(const int lba, const size_t size);
        fs::path GetFullDirPath(int dirEntry) const;
    };

    class DirTree : public ListView<Entry>
    {
    public:
        explicit DirTree(ListView<Entry> view);

        template <bool udf>
        static std::optional<Entry> ReadRootDir(const int lba);

        template <bool udf>
        void ReadDirEntries(const int lba, const size_t size);

    private:
        static void ReadICB(Entry &entry);
        static std::optional<Entry> ReadEntryISO();
        static std::optional<Entry> ReadEntryUDF();
    };

    template <bool udf>
    ::Entry &ParseRoot(std::list<Entry> &entries, const uint32_t lba, const std::vector<PathTableEntry> *pathTableList);
}
