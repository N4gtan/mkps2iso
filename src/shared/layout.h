#pragma once

#include "iso_fs.h"
#include "ecma_167.h"
#include <cstddef>

// Sector size in bytes (do not change)
#define DVD_SECTOR_SIZE 2048

namespace layout
{
#pragma pack(push, 1)

    template <typename T>
    struct SECTOR
    {
        SECTOR() = delete;
        union
        {
            T data;
            uint8_t raw[DVD_SECTOR_SIZE];
        };
    };

    struct SYSTEM_AREA
    {
        // Sectors 0-11: Logo Data
        ISO_BOOT_LOGO logo;
        // Sectors 12-13: Reserved Area (null)
        uint8_t pad[2 * DVD_SECTOR_SIZE];
        // SECTOR 14: Master Disc Descriptor
        uint8_t mdd1[DVD_SECTOR_SIZE];
        // SECTOR 15: Master Disc Descriptor Backup
        uint8_t mdd2[DVD_SECTOR_SIZE];
    };

    struct ISO
    {
        // SECTOR 16: Primary Volume Descriptor ISO 9660
        SECTOR<ISO_DESCRIPTOR> pvd;
        // SECTOR 17: Volume Descriptor Terminator ISO 9660
        SECTOR<ISO_DESCRIPTOR> vdt;
    };

    struct EXTENDED_AREA
    {
        // SECTOR 18: Beginning Extended Area Descriptor (BEA01)
        SECTOR<beginningExtendedAreaDesc> bea;
        // SECTOR 19: NSR Descriptor (NSR02)
        SECTOR<NSRDesc> nsr;
        // SECTOR 20: Terminating Extended Area Descriptor (TEA01)
        SECTOR<terminatingExtendedAreaDesc> tea;
    };

    struct UDF
    {
        // SECTOR 32: Primary Volume Descriptor UDF
        SECTOR<primaryVolDesc> pvd;
        // SECTOR 33: Implementation Use Volume Descriptor
        SECTOR<impUseVolDesc> iuvd;
        // SECTOR 34: Partition Descriptor
        SECTOR<partitionDesc> pd;
        // SECTOR 35: Logical Volume Descriptor
        SECTOR<logicalVolDesc> lvd;
        // SECTOR 36: Unallocated Space Descriptor
        SECTOR<unallocSpaceDesc> usd;
        // SECTOR 37: Terminating Descriptor
        SECTOR<terminatingDesc> td;
        // Sectors 38-47: Trailing Logical Sectors (null)
        uint8_t tls[10 * DVD_SECTOR_SIZE];
    };

    struct LVID
    {
        // Sectors 64: Logical Volume Integrity Descriptor
        SECTOR<logicalVolIntegrityDesc> lvid;
        // Sectors 65: Terminating Descriptor
        SECTOR<terminatingDesc> td;
    };

    struct VOLUME
    {
        // Sectors 0-15: System Area
        SYSTEM_AREA sys;

        // Sectors 16-17: ISO 9660 Descriptor Area
        ISO iso;

        // Sectors 18-20: Extended Descriptor Area
        EXTENDED_AREA ext;

        // Sectors 21-31: Reserved Area (null)
        uint8_t pad1[11 * DVD_SECTOR_SIZE];

        // Sectors 32-47: UDF Descriptor Area
        UDF main;

        // Sectors 48-63: UDF Descriptor Area Backup
        UDF rsrv;

        // Sectors 64-65: Logical Volume Integrity Descriptor Area
        LVID lvid;

        // Sectors 66-255: Reserved Area (null)
        uint8_t pad2[190 * DVD_SECTOR_SIZE];

        // SECTOR 256: Anchor Volume Descriptor Point
        SECTOR<anchorVolDescPtr> avdp;
    };

    struct FSD
    {
        SECTOR<fileSetDesc> fsd;
        SECTOR<terminatingDesc> td;
    };

#pragma pack(pop)

    // ==========================================
    // LOCATIONS (LBAs)
    // ==========================================

    constexpr size_t LBA_BOOT_LOGO   = offsetof(VOLUME, sys) / DVD_SECTOR_SIZE;      // 0
    constexpr size_t LBA_MASTER_DISC = offsetof(VOLUME, sys.mdd1) / DVD_SECTOR_SIZE; // 14
    constexpr size_t LBA_ISO_PVD     = offsetof(VOLUME, iso) / DVD_SECTOR_SIZE;      // 16
    constexpr size_t LBA_ISO_TERM    = offsetof(VOLUME, iso.vdt) / DVD_SECTOR_SIZE;  // 17
    constexpr size_t LBA_UDF_BRIDGE  = offsetof(VOLUME, ext) / DVD_SECTOR_SIZE;      // 18
    constexpr size_t LBA_UDF_MAIN    = offsetof(VOLUME, main) / DVD_SECTOR_SIZE;     // 32
    constexpr size_t LBA_UDF_RSRV    = offsetof(VOLUME, rsrv) / DVD_SECTOR_SIZE;     // 48
    constexpr size_t LBA_LVID        = offsetof(VOLUME, lvid) / DVD_SECTOR_SIZE;     // 64
    constexpr size_t LBA_LVID_TERM   = offsetof(VOLUME, lvid.td) / DVD_SECTOR_SIZE;  // 65
    constexpr size_t LBA_ANCHOR      = offsetof(VOLUME, avdp) / DVD_SECTOR_SIZE;     // 256

    // Start point for Path Tables
    constexpr size_t LBA_TABLE_START = sizeof(VOLUME) / DVD_SECTOR_SIZE;             // 257
}
