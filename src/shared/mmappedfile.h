#pragma once

// Cross-platform memory mapped file wrapper

#include <filesystem>
namespace fs { using namespace std::filesystem; }

class MMappedFile
{
public:
    class View
    {
    public:
        View(void *handle, uint64_t offset, size_t size, uint32_t flags);
        ~View();

        // Delete copy constructor and copy assignment
        View(const View &) = delete;
        View &operator=(const View &) = delete;

        void *GetBuffer() const { return m_data; }

    private:
        void *m_mapping = nullptr; // Aligned down to allocation granularity
        void *m_data = nullptr;
        size_t m_size = 0; // Real size, with adjustments to granularity
    };

    MMappedFile();
    ~MMappedFile();

    // Delete copy constructor and copy assignment
    MMappedFile(const MMappedFile &) = delete;
    MMappedFile &operator=(const MMappedFile &) = delete;

    // Read-Only access
    bool Open(const fs::path &filePath);

    //Read/Write access
    bool Create(const fs::path &filePath, uint64_t size);
    View GetView(uint64_t offset, size_t size) const;

private:
    void *m_handle = nullptr; // Opaque, platform-specific
    uint32_t m_mapFlags = 0;
};
