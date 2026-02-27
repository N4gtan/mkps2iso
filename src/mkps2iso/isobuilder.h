#pragma once

#include "common.h"
#include "listview.h"

namespace iso
{
    struct IDENTIFIERS
    {
        const char *SystemID;
        const char *VolumeID;
        const char *VolumeSet;
        const char *Publisher;
        const char *DataPreparer;
        const char *Application;
        const char *Copyright;
        const char *CreationDate;
        const char *ModificationDate;
    };

    class PathTable
    {
    public:
        std::vector<PathTableEntry> entries;

        template <bool msb>
        uint8_t *SerializeTable(uint8_t *buff) const;
    };

    class DirTree : public ListView<Entry>
    {
        Entry   *m_entry; // Non-owning
        DirTree *m_parent; // Non-owning

    public:
        explicit DirTree(ListView<Entry> view, Entry *entry = nullptr, DirTree *parent = nullptr);

        void OutputLBAlisting(FILE *fp, const int level) const;
        void OutputHeaderListing(FILE *fp, const int level, const char *name) const;

        static Entry &CreateRootDirectory(std::list<Entry> &entries, const ISO_DATESTAMP &volumeDate, const EntryAttributes &attributes);

        /** Calculates the length of the directory record to be produced by this class in bytes.
         *
         *  Returns: Length of directory record in bytes.
         */
        uint32_t CalculateDirRecordLen() const;
        uint32_t CalculateFileIdDescLen() const;

        /** Calculates the LBA of all file and directory entries and returns the next LBA address.
         *
         *  lba             - LBA address where the first directory record begins.
         * 
         *  Returns: Highest calculated LBA.
         */
        uint32_t CalculateFileTreeLBA(uint32_t lba);
        uint32_t CalculateDirRecordLBA(uint32_t lba);
        uint32_t CalculateFileIdDescLBA(uint32_t lba);
        uint32_t CalculateInfCtrlBlockLBA(uint32_t lba);

        /** Generates a path table of all directories and subdirectories within this class directory record.
         *
         *  Returns: Object with all entries.
         */
        PathTable GeneratePathTable() const;

        /** Adds a file entry to the directory record.
         *
         *  id              - The name of the file entry.
         *  srcfile         - Path and filename to the source file.
         *  attributes      - GMT offset/permissions for the file, if applicable.
         *  *date           - Timestamp for the file, if applicable.
         */
        bool AddFileEntry(std::string id, fs::path srcFile, const EntryAttributes &attributes, const char *date);

        /** Adds an invisible dummy file entry to the directory record. Its invisible because its file entry
         *  is not actually added to the directory record.
         *
         *  sectors         - The size of the dummy file in sector units (1 = 2048 bytes, 1024 = 2MB).
         *  lbaFRC          - Forced LBA offset.
         */
        void AddDummyEntry(const uint32_t sectors, const uint32_t lbaFRC);

        /** Adds a subdirectory to the directory record.
         *
         *  id              - The name of the subdirectory to add.
         *  srcDir          - Path and filename to the source directory.
         *  attributes      - GMT offset/permissions for the subdirectory, if applicable.
         *  *date           - Timestamp for the subdirectory, if applicable.
         *
         *  Returns: Pointer to another DirTree for accessing the directory record of the subdirectory.
         */
        DirTree *AddSubDirEntry(std::string id, const fs::path &srcDir, const EntryAttributes &attributes, const char *date);

        /** Writes the source files assigned to the directory entries to a DVD image. Its recommended to execute
         *  this first before writing the actual file system.
         */
        void WriteFiles() const;

        /** Writes the file system descriptors to a DVD image. Execute this after the source files
         *  have been written to the DVD image.
         */
        void WriteDirectoryRecords() const;
        void WriteIsoDescriptors(const int totalLenLBA, const IDENTIFIERS &id) const;

        void WriteInfoCtrlBlocks(const uint32_t partitionStartLBA);
        void WriteFileSetDescriptors(const uint32_t partitionStartLBA, const IDENTIFIERS &id) const;
        void WriteFileIdDescriptors(const uint32_t partitionStartLBA);

        void SortDirectoryEntries(const bool byLBA = false);
        void PartitionEntries();

        uint32_t CalculatePathTableLen(const Entry &dirEntry) const;

        template <bool recursive = true>
        uint32_t GetFileCount() const;

        template <bool recursive = true>
        uint32_t GetDirCount() const;

        uint32_t GetPathDepth(size_t *pathLength = nullptr) const;
        void PrintRecordPath() const;
    };

    void WriteLicenseData(const uint8_t *data);

    void WriteExtendedDescriptors();
    void WriteUdfDescriptors(const uint32_t partitionStartLBA, const uint32_t partitionSize, const iso::IDENTIFIERS &id, const uint32_t lba);
    void WriteLviDescriptors(const iso::DirTree *dirTree, const uint32_t partitionSize, const iso::IDENTIFIERS &id);
    void WriteAnchorDescriptor(const uint32_t partitionEnd);
};
