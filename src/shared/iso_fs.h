#pragma once

// Set struct alignment to 1 because the ISO file system is not very memory alignment friendly which will
// result to alignment issues when reading data entries with structs.
#pragma pack(push, 1)

// Boot logo "PlayStation(R) 2" (8bpp, PAL 344x71, NTSC 384x64)
struct ISO_BOOT_LOGO
{
    unsigned char data[24576];
};

struct ISO_MASTER_DISC
{
    char serial[32];               // Space padded
    char producer[32];             // Space padded
    char copyright[32];            // Space padded
    char creationDate[8];          // ASCII YYYYMMDD
    char masterDiscId[24];         // "PlayStation Master Disc "
    unsigned char system;          // '1' PS1, '2' PS2
    unsigned char region;          // 0x00 UND, 0x01 JPN, 0x02 USA, 0x04 EUR, 0x08 CHN, 0x0F WLD
    unsigned char regionFlags;     // 0x00 UND or (Bit3 CHN, Bit4 Layer0 non-UND, Bit5 Layer0 DISC ID)
    unsigned char media;           // 0x01 CD, 0x02 DVD

    // Space filled for CDs
    struct
    {
        unsigned char type;        // 0x01 DVD5, 0x02 DVD9
        unsigned char layer;       // 0x00 Layer0, 0x01 Layer1
        unsigned int layer0EndLBA; // Total Layer0 Sectors minus 1
        unsigned int layer1EndLBA; // Total Layer1 Sectors minus 1 minus 0x0F plus layer0EndLBA or null for DVD5
    } dvd;
    char pad[114];                 // Spaces

    // Idk what this blocks could mean but used blocks depends on the region. UND 3 blocks, JPN/USA/EUR/WLD 4 blocks, CHN 5 blocks
    struct
    {
        unsigned char flag;        // Block ID/Flag (0x01, 0x02, 0x03)
        unsigned int sign1;        // 0xFFFFFFFF or 0x4B
        unsigned int sign2;        // 0xFFFFFFFF or 0x104A non-CHN/0x29EA CHN
        unsigned int magic;        // 32-bit Serial hash or null
        unsigned char  key;        //  8-bit Serial hash or 0x80
        unsigned char  pad;        // Null
        unsigned char  end;        // 0x80 if layer1 or for 4th block if its flag is 0x03 and regionFlags Bit5 is on, 0x00 otherwise
    } magicBlock[32];

    // This field is space filled, but it's filled with the burner drive data when burning with CDVDREC
    struct
    {
        char vendor[8];            // SCSI T10 Vendor Identification
        char product[16];          // SCSI Product Identification
        char revision[4];          // SCSI Product Revision Level (Firmware Version)
        char extras[20];           // SCSI Vendor Specific Data (Often contains firmware build dates)
    } burner;
    char toolVersion[32];          // "CDVDGEN x.xx", space padded
    char pad2[1200];               // Spaces
};

/// Structure of a double-endian unsigned short word
struct ISO_USHORT_PAIR
{
    unsigned short lsb; // LSB format 16-bit word
    unsigned short msb; // MSB format 16-bit word
};

/// Structure of a double-endian unsigned int word
struct ISO_UINT_PAIR
{
    unsigned int lsb; // LSB format 32-bit word
    unsigned int msb; // MSB format 32-bit word
};

/// ISO descriptor header structure
struct ISO_DESCRIPTOR_HEADER
{
    unsigned char type;    // Volume descriptor type (1 is descriptor, 255 is descriptor terminator)
    char identifier[5];            // Volume descriptor ID (always CD001)
    unsigned char version; // Volume descriptor version (always 0x01)
    unsigned char pad;     // Unused null byte
};

/// Structure of a date stamp for ISO_DIR_ENTRY structure
struct ISO_DATESTAMP
{
    unsigned char year;   // number of years since 1900
    unsigned char month;  // month, where 1=January, 2=February, etc.
    unsigned char day;    // day of month, in the range from 1 to 31
    unsigned char hour;   // hour, in the range from 0 to 23
    unsigned char minute; // minute, in the range from 0 to 59
    unsigned char second; // Second, in the range from 0 to 59
    signed char GMToffs;  // Greenwich Mean Time offset (usually 0x24)
};

/// Structure of a long date time format, specified in Section 8.4.26.1 of ECMA 119
struct ISO_LONG_DATESTAMP
{
    char year[4];        // year from I to 9999
    char month[2];       // month of the year from 1 to 12
    char day[2];         // day of the month from 1 to 31
    char hour[2];        // hour of the day from 0 to 23
    char minute[2];      // minute of the hour from 0 to 59
    char second[2];      // second of the minute from 0 to 59
    char hsecond[2];     // hundredths of a second
    signed char GMToffs; // Greenwich Mean Time offset (usually 0x24)
};

/// Structure of an ISO path table entry
struct ISO_PATHTABLE_ENTRY
{
    unsigned char identifierLen;   // Length of Directory Identifier(LEN_DI) (or 1 for the root directory)
    unsigned char extLength;       // Number of sectors in Extended Attribute Record
    unsigned int dirOffs;          // Number of the first sector in the directory, as a double word
    unsigned short parentDirIndex; // Index of the directory record's parent directory
    // If identifierLen is odd numbered, a padding byte will be present after the identifier text.
};

struct ISO_DIR_ENTRY
{
    unsigned char entryLength;       // Directory Record Length(LEN_DR) (variable, use for parsing through entries)
    unsigned char extLength;         // Extended Attribute Record Length (always 0)
    ISO_UINT_PAIR entryOffs;         // Points to the LBA of the file/directory entry
    ISO_UINT_PAIR entrySize;         // Size of the file/directory entry
    ISO_DATESTAMP entryDate;         // Date & time stamp of entry
    unsigned char flags;             // File flags (0x02 for directories, 0x00 for files)
    unsigned char fileUnitSize;      // Unit size (usually 0 even with Form 2 files such as STR/XA)
    unsigned char interleaveGapSize; // Interleave gap size (usually 0 even with Form 2 files such as STR/XA)
    ISO_USHORT_PAIR volSeqNum;       // Volume sequence number (always 1)
    unsigned char identifierLen;     // Length of File/Dir Identifier(LEN_FI) in bytes
    // If identifierLen is even numbered, a padding byte will be present after the identifier text.
};

struct ISO_ROOTDIR_HEADER
{
    unsigned char entryLength;       // Always 34 bytes
    unsigned char extLength;         // Always 0
    ISO_UINT_PAIR entryOffs;         // Should point to LBA 22
    ISO_UINT_PAIR entrySize;         // Size of entry extent
    ISO_DATESTAMP entryDate;         // Record date and time
    unsigned char flags;             // File flags
    unsigned char fileUnitSize;      //
    unsigned char interleaveGapSize; //
    ISO_USHORT_PAIR volSeqNum;       //
    unsigned char identifierLen;     // 0x01
    unsigned char identifier;        // 0x00
};

// ISO Primary Volume Descriptor structure
struct ISO_DESCRIPTOR
{
    // ISO descriptor header
    ISO_DESCRIPTOR_HEADER header;
    // System ID (always PLAYSTATION)
    char systemID[32];
    // Volume ID (or label, can be blank or anything)
    char volumeID[32];
    // Unused null bytes
    unsigned char pad[8];
    // Size of volume in sector units
    ISO_UINT_PAIR volumeSize;
    // Unused null bytes
    unsigned char pad2[32];
    // Number of discs in this volume set (always 1 for single volume)
    ISO_USHORT_PAIR volumeSetSize;
    // Number of this disc in volume set (always 1 for single volume)
    ISO_USHORT_PAIR volumeSeqNumber;
    // Size of sector in bytes (always 2048 bytes)
    ISO_USHORT_PAIR sectorSize;
    // Path table size in bytes (applies to all the path tables)
    ISO_UINT_PAIR pathTableSize;
    // LBA to Type-L path table
    unsigned int pathTable1Offs;
    // LBA to optional Type-L path table (usually a copy of the primary path table)
    unsigned int pathTable2Offs;
    // LBA to Type-L path table but with MSB format values
    unsigned int pathTable1MSBoffs;
    // LBA to optional Type-L path table but with MSB format values (usually a copy of the main path table)
    unsigned int pathTable2MSBoffs;
    // Directory entry for the root directory (similar to a directory entry)
    ISO_ROOTDIR_HEADER rootDirRecord;
    // Volume set identifier (can be blank or anything)
    char volumeSetIdentifier[128];
    // Publisher identifier (can be blank or anything)
    char publisherIdentifier[128];
    // Data preparer identifier (can be blank or anything)
    char dataPreparerIdentifier[128];
    // Application identifier (always PLAYSTATION)
    char applicationIdentifier[128];
    // Copyright file in the file system identifier (can be blank or anything)
    char copyrightFileIdentifier[37];
    // Abstract file in the file system identifier (can be blank or anything)
    char abstractFileIdentifier[37];
    // Bibliographical file identifier in the file system (can be blank or anything)
    char bibliographicFilelIdentifier[37];
    // Volume create date
    ISO_LONG_DATESTAMP volumeCreateDate;
    // Volume modify date
    ISO_LONG_DATESTAMP volumeModifyDate;
    // Volume expiry date
    ISO_LONG_DATESTAMP volumeExpiryDate;
    // Volume effective date
    ISO_LONG_DATESTAMP volumeEffectiveDate;
    // File structure version (always 1)
    unsigned char fileStructVersion;
    // Padding
    unsigned char pad3;
    // Application specific data (all null bytes)
    unsigned char appData[512];
    // Padding
    unsigned char pad4[653];
};

// As per "System Description CD-ROM XA" by NV Philips and Sony Corporation.
// XA attribute struct (located right after the identifier string)
// Fields are big endian
struct ISO_XA_ATTRIB
{
    unsigned short ownergroupid; // Usually 0x0000
    unsigned short owneruserid;  // Usually 0x0000
    unsigned short attributes;
    char id[2];
    unsigned char filenum;       // 0x00 standard file. 0x01-0xFF interleaved file (must match the file subheader filenum byte unless it's 0x00)
    unsigned char reserved[5];
};

// Masks for ISO_XA_ATTRIB.attributes
constexpr unsigned short XA_PERMISSIONS_MASK = 0x7FF;
constexpr unsigned short XA_ATTRIBUTES_MASK = ~XA_PERMISSIONS_MASK;

// Leave non-aligned structure packing
#pragma pack(pop)
