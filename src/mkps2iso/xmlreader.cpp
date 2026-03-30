#include "xmlreader.h"

namespace param
{
    extern fs::path xmlFile;
    extern fs::path isoFile;
    extern fs::path logoRawFile;
};

namespace global
{
    time_t buildTime;
};

namespace
{
    fs::path xmlPath;
    EntryAttributes defaultAttributes;
}
static bool ParseDirectory(iso::DirTree *dirTree, const tinyxml2::XMLElement *parentElement, const fs::path &currentPath);

static EntryAttributes ReadEntryAttributes(EntryAttributes current, const tinyxml2::XMLElement *dirElement)
{
    if (dirElement != nullptr)
    {
        auto getAttributeIfExists = [dirElement](auto &value, const char *name)
        {
            using type = std::decay_t<decltype(value)>;
            if constexpr (std::is_unsigned_v<type>)
                value = static_cast<type>(dirElement->UnsignedAttribute(name, value));
            else
                value = static_cast<type>(dirElement->IntAttribute(name, value));
        };

        getAttributeIfExists(current.LBAFRC, xml::attrib::OFFSET);
        getAttributeIfExists(current.ORDER, xml::attrib::ORDER);
        getAttributeIfExists(current.LINKC, xml::attrib::LINK_COUNT);
        getAttributeIfExists(current.HFLAG, xml::attrib::HIDDEN_FLAG);
        /*getAttributeIfExists(current.GMTOffs, xml::attrib::GMT_OFFSET);
        getAttributeIfExists(current.XAAttrib, xml::attrib::XA_ATTRIBUTES);
        getAttributeIfExists(current.XAPerm, xml::attrib::XA_PERMISSIONS);
        getAttributeIfExists(current.GID, xml::attrib::XA_GID);
        getAttributeIfExists(current.UID, xml::attrib::XA_UID);*/
    }

    return current;
};

static bool ParseFileEntry(iso::DirTree *dirTree, const tinyxml2::XMLElement *dirElement, const fs::path &currentPath)
{
    const char *nameElement = dirElement->Attribute(xml::attrib::ENTRY_NAME);
    const char *sourceElement = dirElement->Attribute(xml::attrib::ENTRY_SOURCE);

    if ((nameElement == nullptr || *nameElement == 0) && (sourceElement == nullptr || *sourceElement == 0))
    {
        printf("ERROR: Missing name and source attributes on line %d.\n", dirElement->GetLineNum());
        return false;
    }

    fs::path srcFile;
    std::string name;
    if (sourceElement != nullptr && *sourceElement != 0)
    {
        srcFile = (xmlPath / sourceElement).lexically_normal();

        if (nameElement != nullptr && *nameElement != 0)
            name = nameElement;
        else
            name = srcFile.filename().string();
    }
    else
    {
        name    = nameElement;
        srcFile = currentPath / name;
    }

    // ECMA-119 7.5.1 and 10.1 - File Identifier shall be 1-30 characters long plus one dot.
    if (name.size() > 12)
    {
        if (name.size() > 31)
        {
            printf("ERROR: File name '%s' on line %d is more than 31 characters long.\n", name.c_str(), dirElement->GetLineNum());
            return false;
        }

        // ECMA-119 6.8.2.1 - The path length of any file shall not exceed 255.
        size_t pathLength = (name + ";1").length();
        int depth = dirTree->GetPathDepth(&pathLength);
        if (pathLength + depth > 255)
        {
            printf("ERROR: File path length exceeds 255 characters on line %d.\n", dirElement->GetLineNum());
            return false;
        }
    }

    return dirTree->AddFileEntry(std::move(name), std::move(srcFile), ReadEntryAttributes(defaultAttributes, dirElement), dirElement->Attribute(xml::attrib::ENTRY_DATE));
}

static bool ParseDirEntry(iso::DirTree *dirTree, const tinyxml2::XMLElement *dirElement, const fs::path &currentPath)
{
    fs::path srcDir;
    std::string name;
    if (const char *sourceElement = dirElement->Attribute(xml::attrib::ENTRY_SOURCE); sourceElement != nullptr && *sourceElement != 0)
        srcDir = (xmlPath / sourceElement).lexically_normal();

    if (const char *nameElement = dirElement->Attribute(xml::attrib::ENTRY_NAME); nameElement != nullptr && *nameElement != 0)
    {
        name = nameElement;
        if (srcDir.empty())
            srcDir = currentPath / name;
    }
    else if (!srcDir.empty())
    {
        name = srcDir.filename().string();
    }
    else
    {
        printf("ERROR: Directory name missing on line %d.\n", dirElement->GetLineNum());
        return false;
    }

    // ECMA-119 7.6.3 and 10.1 - Directory Identifier shall be 1-31 characters long.
    if (name.length() > 31)
    {
        printf("ERROR: Directory name '%s' on line %d is more than 31 characters long.\n", name.c_str(), dirElement->GetLineNum());
        return false;
    }

    { // ECMA-119 6.8.2.1 - The number of levels in the hierarchy shall not exceed eight.
        int level = dirTree->GetPathDepth() + 2; // +1 for root, +1 for this dir
        if (level > 8)
        {
            printf("ERROR: Directory hierarchy depth exceeds 8 levels on line %d.\n", dirElement->GetLineNum());
            return false;
        }
    }

    iso::DirTree *subdir = dirTree->AddSubDirEntry(std::move(name), srcDir, ReadEntryAttributes(defaultAttributes, dirElement), dirElement->Attribute(xml::attrib::ENTRY_DATE));

    if (subdir == nullptr)
        return false;

    return ParseDirectory(subdir, dirElement, srcDir);
}

static bool ParseDirectory(iso::DirTree *dirTree, const tinyxml2::XMLElement *parentElement, const fs::path &currentPath)
{
    for (const tinyxml2::XMLElement *dirElement = parentElement->FirstChildElement(); dirElement != nullptr; dirElement = dirElement->NextSiblingElement())
    {

        if (CompareICase(xml::elem::FILE, dirElement->Name()))
        {
            if (!ParseFileEntry(dirTree, dirElement, currentPath))
                return false;
        }
        else if (CompareICase(xml::elem::DUMMY, dirElement->Name()))
        {
            dirTree->AddDummyEntry(dirElement->UnsignedAttribute(xml::attrib::NUM_DUMMY_SECTORS),
                                   dirElement->UnsignedAttribute(xml::attrib::OFFSET));
        }
        else if (CompareICase(xml::elem::DIR, dirElement->Name()))
        {
            if (!ParseDirEntry(dirTree, dirElement, currentPath))
                return false;
        }
    }

    return true;
}

iso::DirTree *xml::Reader::ReadDirTree(std::list<Entry> &entries)
{
    time(&global::buildTime); // Get current time to be used as timestamps for all directories without source

    ISO_DATESTAMP volumeDate;
    // Try to use time from XML. If it's malformed, fall back to local time.
    if (!ParseDateFromString(volumeDate, iso::isoIdentifiers.CreationDate))
    {
        // Use local time
        const tm *imageTime = localtime(&global::buildTime);
        volumeDate.year     = imageTime->tm_year;
        volumeDate.month    = imageTime->tm_mon + 1;
        volumeDate.day      = imageTime->tm_mday;
        volumeDate.hour     = imageTime->tm_hour;
        volumeDate.minute   = imageTime->tm_min;
        volumeDate.second   = imageTime->tm_sec;
        volumeDate.GMToffs  = 36; // Use Japan GMT

        // Convert ISO_DATESTAMP to ISO_LONG_DATESTAMP
        static const std::string creationDate = DateToString(volumeDate, true);
        iso::isoIdentifiers.CreationDate = creationDate.c_str();
    }

    // Establish default entry attributes from XML (if any)
    defaultAttributes = ReadEntryAttributes(defaultAttributes, m_layerElement->FirstChildElement(elem::DEFAULT_ATTRIBUTES));

    const tinyxml2::XMLElement *directoryTree = m_layerElement->FirstChildElement(elem::DIRECTORY_TREE);
    if (directoryTree == nullptr)
    {
        printf("ERROR: No %s element specified for the project on line %d.\n", elem::DIRECTORY_TREE, m_layerElement->GetLineNum());
        return nullptr;
    }

    const char *dirTreePath = directoryTree->Attribute(attrib::ENTRY_SOURCE);
    fs::path currentPath = dirTreePath != nullptr && *dirTreePath != 0
                               ? (xmlPath / dirTreePath).lexically_normal()
                               : xmlPath;

    Entry &root = iso::DirTree::CreateRootDirectory(entries, volumeDate, ReadEntryAttributes(defaultAttributes, directoryTree));
    iso::DirTree *dirTree = root.subdir.get();

    if (!ParseDirectory(dirTree, directoryTree, currentPath))
        return nullptr;

    return dirTree;
}

xml::Reader *xml::Reader::ReadHeaders(std::string &serial, Region::Bit &region)
{
    if (const char *str = m_projectElement->Attribute(attrib::SERIAL); str == nullptr || *str == 0)
        printf("Error: <iso_project> attribute \"serial\" is missing or blank on line %d.\n", m_projectElement->GetLineNum());
    else
        for (; *str != 0; ++str)
            serial += std::toupper(static_cast<uint8_t>(*str));

    if (const char *str = m_projectElement->Attribute(attrib::REGION); str != nullptr)
    {
        switch (std::tolower(static_cast<uint8_t>(str[0])))
        {
            case 'j': region = Region::Japan;   break;
            case 'a': region = Region::America; break;
            case 'e': region = Region::Europe;  break;
            case 'c': region = Region::China;   break;
            case 'w': region = Region::World;
        }
    }

    // Set file system identifiers
    if (const tinyxml2::XMLElement *identifierElement = m_projectElement->FirstChildElement(elem::IDENTIFIERS))
    {
        // Use individual elements defined by each attribute
        iso::isoIdentifiers.SystemID         = identifierElement->Attribute(attrib::SYSTEM_ID);
        iso::isoIdentifiers.VolumeID         = identifierElement->Attribute(attrib::VOLUME_ID);
        iso::isoIdentifiers.VolumeSet        = identifierElement->Attribute(attrib::VOLUME_SET);
        iso::isoIdentifiers.Publisher        = identifierElement->Attribute(attrib::PUBLISHER);
        iso::isoIdentifiers.DataPreparer     = identifierElement->Attribute(attrib::DATA_PREPARER);
        iso::isoIdentifiers.Application      = identifierElement->Attribute(attrib::APPLICATION);
        iso::isoIdentifiers.Copyright        = identifierElement->Attribute(attrib::COPYRIGHT);
        iso::isoIdentifiers.CreationDate     = identifierElement->Attribute(attrib::CREATION_DATE);
        iso::isoIdentifiers.ModificationDate = identifierElement->Attribute(attrib::MODIFICATION_DATE);

        // Is an ID file specified?
        if (const char *identifierFile = identifierElement->Attribute(attrib::ID_FILE))
        {
            // Load the file as an XML document
            if (!Open(identifierFile, m_xmlIdFile))
                return nullptr;

            // Get the identifier element, if there is one
            if ((identifierElement = m_xmlIdFile.FirstChildElement(elem::IDENTIFIERS)))
            {
                const char *str;
                // Use strings defined in file, otherwise leave ones already defined alone
                if ((str = identifierElement->Attribute(attrib::SYSTEM_ID)))
                    iso::isoIdentifiers.SystemID         = str;
                if ((str = identifierElement->Attribute(attrib::VOLUME_ID)))
                    iso::isoIdentifiers.VolumeID         = str;
                if ((str = identifierElement->Attribute(attrib::VOLUME_SET)))
                    iso::isoIdentifiers.VolumeSet        = str;
                if ((str = identifierElement->Attribute(attrib::PUBLISHER)))
                    iso::isoIdentifiers.Publisher        = str;
                if ((str = identifierElement->Attribute(attrib::DATA_PREPARER)))
                    iso::isoIdentifiers.DataPreparer     = str;
                if ((str = identifierElement->Attribute(attrib::APPLICATION)))
                    iso::isoIdentifiers.Application      = str;
                if ((str = identifierElement->Attribute(attrib::COPYRIGHT)))
                    iso::isoIdentifiers.Copyright        = str;
                if ((str = identifierElement->Attribute(attrib::CREATION_DATE)))
                    iso::isoIdentifiers.CreationDate     = str;
                if ((str = identifierElement->Attribute(attrib::MODIFICATION_DATE)))
                    iso::isoIdentifiers.ModificationDate = str;
            }
        }
    }

    // Check for logo file
    bool gotLogoFromXML = false;
    const tinyxml2::XMLElement *logoElement = m_projectElement->FirstChildElement(elem::LOGO);
    if (param::logoRawFile.empty() && logoElement != nullptr)
    {
        const char *logo_file_attrib = logoElement->Attribute(attrib::LOGO_FILE);

        if (logo_file_attrib == nullptr || *logo_file_attrib == 0)
        {
            printf("ERROR: <logo> attribute \"file\" is missing or blank on line %d.\n", logoElement->GetLineNum());
            return nullptr;
        }

        param::logoRawFile = (param::xmlFile.parent_path() / logo_file_attrib).lexically_normal();
        gotLogoFromXML = true;
    }

    if (!param::logoRawFile.empty() && OpenScopedFile(param::logoRawFile, "rb") == nullptr)
    {
        printf("ERROR: Specified logo file ");

        if (gotLogoFromXML)
            printf("(on line %d) ", logoElement->GetLineNum());

        printf("not found.\n");
        return nullptr;
    }

    return this;
}

tinyxml2::XMLElement *xml::Reader::NextLayerElement()
{
    if (m_layerElement == nullptr)
    {
        // First call: Check if there is a <layer> element
        m_layerElement = m_projectElement->FirstChildElement(elem::LAYER);
        if (m_layerElement == nullptr)
            printf("ERROR: Cannot find <layer> element in XML document.\n");
    }
    else
    {
        // Subsequent calls: get the next sibling
        m_layerElement = m_layerElement->NextSiblingElement(elem::LAYER);
    }

    return m_layerElement;
}

tinyxml2::XMLElement *xml::Reader::NextProjectElement()
{
    if (m_projectElement == nullptr)
    {
        // First call: Check if there is an <iso_project> element
        m_projectElement = m_xmlDoc.FirstChildElement(elem::ISO_PROJECT);
        if (m_projectElement == nullptr)
        {
            printf("ERROR: Cannot find <iso_project> element in XML document.\n");
        }
        else if (param::isoFile.empty())
        {
            // Check if image_name attribute is specified
            if (const char *image_name = m_projectElement->Attribute(attrib::IMAGE_NAME); image_name != nullptr && *image_name != 0)
                param::isoFile = image_name;
            else
               (param::isoFile = param::xmlFile.stem()) += ".iso"; // Use file name of XML project as the image file name
        }
    }
    else
    {
        // Subsequent calls: get the next sibling
        m_projectElement = m_projectElement->NextSiblingElement(elem::ISO_PROJECT);
        if (m_projectElement != nullptr)
        {
            // Check if image_name attribute is specified
            if (const char *image_name = m_projectElement->Attribute(attrib::IMAGE_NAME); image_name != nullptr && *image_name != 0)
            {
                param::isoFile = image_name;
            }
            else
            {
                printf("ERROR: <iso_project> attribute \"image_name\" is missing or blank on line %d.\n", m_projectElement->GetLineNum());
                m_projectElement = nullptr;
            }
        }
    }

    return m_projectElement;
}

bool xml::Reader::Open(const fs::path &fileName, tinyxml2::XMLDocument &doc)
{
    tinyxml2::XMLError error;
    if (unique_file file = OpenScopedFile(fileName, "rb"); file != nullptr)
        error = doc.LoadFile(file.get());
    else
        error = tinyxml2::XML_ERROR_FILE_NOT_FOUND;

    if (error != tinyxml2::XML_SUCCESS)
    {
        printf("ERROR: ");
        if (error == tinyxml2::XML_ERROR_FILE_NOT_FOUND)
            printf("File not found.\n");
        else if (error == tinyxml2::XML_ERROR_FILE_COULD_NOT_BE_OPENED)
            printf("File cannot be opened.\n");
        else if (error == tinyxml2::XML_ERROR_FILE_READ_ERROR)
            printf("Error reading file.\n");
        else
            printf("%s on line %d\n", doc.ErrorName(), doc.ErrorLineNum());
        return false;
    }

    return true;
}

xml::Reader::Reader()
{
    if (!Open(param::xmlFile, m_xmlDoc))
        exit(EXIT_FAILURE);
    xmlPath = param::xmlFile.parent_path();
}
