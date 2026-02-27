#include "mmappedfile.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif

MMappedFile::MMappedFile()
{
}

MMappedFile::~MMappedFile()
{
    if (m_handle != nullptr)
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(m_handle));
#else
        close(static_cast<int>(reinterpret_cast<intptr_t>(m_handle)));
#endif
}

bool MMappedFile::Open(const fs::path &filePath)
{
    bool result = false;

#ifdef _WIN32
    m_mapFlags = FILE_MAP_READ;
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        HANDLE fileMapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (fileMapping != nullptr)
        {
            m_handle = fileMapping;
            result = true;
        }
        CloseHandle(file);
    }
#else
    m_mapFlags = PROT_READ;
    int file = open(filePath.c_str(), O_RDONLY);
    if (file != -1)
    {
        m_handle = reinterpret_cast<void *>(file);
        result = true;
    }
#endif
    return result;
}

bool MMappedFile::Create(const fs::path &filePath, const uint64_t size)
{
    bool result = false;

#ifdef _WIN32
    m_mapFlags = FILE_MAP_ALL_ACCESS;
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        ULARGE_INTEGER ulSize;
        ulSize.QuadPart = size;

        HANDLE fileMapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, ulSize.HighPart, ulSize.LowPart, nullptr);
        if (fileMapping != nullptr)
        {
            m_handle = fileMapping;
            result = true;
        }
        else if (size == 0)
        {
            result = true;
        }

        CloseHandle(file);
    }
#else
    m_mapFlags = PROT_READ | PROT_WRITE;
    int file = open(filePath.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (file != -1)
    {
        if (ftruncate(file, size) == 0)
        {
            if (size > 0)
                m_handle = reinterpret_cast<void *>(file);
            else
                close(file);

            result = true;
        }
        else
        {
            close(file);
        }
    }
#endif
    return result;
}

MMappedFile::View MMappedFile::GetView(uint64_t offset, size_t size) const
{
    return View(m_handle, offset, size, m_mapFlags);
}

MMappedFile::View::View(void *handle, uint64_t offset, size_t size, uint32_t flags)
{
    if (size == 0)
        return;

#ifdef _WIN32
    SYSTEM_INFO SysInfo;
    GetSystemInfo(&SysInfo);
    const DWORD allocGranularity = SysInfo.dwAllocationGranularity;
#else
    const long allocGranularity = sysconf(_SC_PAGE_SIZE);
#endif

    const uint64_t mapStartOffset = (offset / allocGranularity) * allocGranularity;
    const uint64_t viewDelta = offset - mapStartOffset;
    size += viewDelta;

#ifdef _WIN32
    ULARGE_INTEGER ulOffset;
    ulOffset.QuadPart = mapStartOffset;
    void *mapping = MapViewOfFile(reinterpret_cast<HANDLE>(handle), flags, ulOffset.HighPart, ulOffset.LowPart, size);
    if (mapping != nullptr)
#else
    void *mapping = mmap(nullptr, size, flags, MAP_SHARED, static_cast<int>(reinterpret_cast<intptr_t>(handle)), mapStartOffset);
    if (mapping != MAP_FAILED)
#endif
    {
        m_mapping = mapping;
        m_data = static_cast<char *>(m_mapping) + viewDelta;
        m_size = size;
    }
}

MMappedFile::View::~View()
{
    if (m_mapping != nullptr)
#ifdef _WIN32
        UnmapViewOfFile(m_mapping);
#else
        munmap(m_mapping, m_size);
#endif
}
