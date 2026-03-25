#include "xmlwriter.h"
#include <map>

namespace param
{
    extern bool dir;
    extern bool lba;
    extern bool outputSortedByDir;
    extern fs::path outPath;
    extern fs::path xmlFile;
}

namespace
{
    // XML attribute stuff
    struct EntryAttributeCounters
    {
        std::map<int, uint32_t> HFLAG;
        /*std::map<int, uint32_t> GMTOffs;
        std::map<int, uint32_t> XAAttrib;
        std::map<int, uint32_t> XAPerm;
        std::map<int, uint32_t> GID;
        std::map<int, uint32_t> UID;*/
    };

    fs::path srcPath;
    EntryAttributeCounters attributeCounters;
}

template <size_t N>
static std::string_view CleanDescElement(const char (&id)[N])
{
    const std::string_view view(id, N);
    const size_t last_char = view.find_last_not_of(' ');
    if (last_char == std::string_view::npos)
        return {};

    return view.substr(0, last_char + 1);
}

static void WriteOptionalXMLAttribs(tinyxml2::XMLElement *element, const Entry &entry)
{
    if (param::lba)
        element->SetAttribute(xml::attrib::OFFSET, entry.lba);

    if (entry.order >= 0)
        element->SetAttribute(xml::attrib::ORDER, entry.order);

    if (entry.flc > 0)
        element->SetAttribute(xml::attrib::LINK_COUNT, entry.flc);

    element->SetAttribute(xml::attrib::HIDDEN_FLAG, entry.hf);
    ++attributeCounters.HFLAG[entry.hf];

    /*element->SetAttribute(xml::attrib::GMT_OFFSET, entry.date.GMToffs);
    ++attributeCounters.GMTOffs[entry.date.GMToffs];

    const auto XAPerm = entry.extData.attributes & cdxa::XA_PERMISSIONS_MASK;
    element->SetAttribute(xml::attrib::XA_PERMISSIONS, XAPerm);
    ++attributeCounters.XAPerm[XAPerm];

    element->SetAttribute(xml::attrib::XA_GID, entry.extData.ownergroupid);
    element->SetAttribute(xml::attrib::XA_UID, entry.extData.owneruserid);
    ++attributeCounters.GID[entry.extData.ownergroupid];
    ++attributeCounters.UID[entry.extData.owneruserid];*/
}

static EntryAttributes EstablishXMLAttributeDefaults(tinyxml2::XMLElement *defaultAttributesElement)
{
    // First establish "defaults" - that is, the most commonly occurring attributes
    auto findMaxElement = [](const auto &map, const int &defaultAttr)
    {
        if (map.empty())
            return defaultAttr;
            
        return std::max_element(map.begin(), map.end(), [](const auto &left, const auto &right)
                                    { return left.second < right.second; })->first;
    };

    EntryAttributes defaultAttributes;
    defaultAttributes.HFLAG = static_cast<uint8_t>(findMaxElement(attributeCounters.HFLAG, defaultAttributes.HFLAG));
    /*defaultAttributes.GMTOffs = static_cast<int8_t>(findMaxElement(attributeCounters.GMTOffs, defaultAttributes.GMTOffs));
    defaultAttributes.XAAttrib = static_cast<uint8_t>(findMaxElement(attributeCounters.XAAttrib, defaultAttributes.XAAttrib));
    defaultAttributes.XAPerm = static_cast<uint16_t>(findMaxElement(attributeCounters.XAPerm, defaultAttributes.XAPerm));
    defaultAttributes.GID = static_cast<uint16_t>(findMaxElement(attributeCounters.GID, defaultAttributes.GID));
    defaultAttributes.UID = static_cast<uint16_t>(findMaxElement(attributeCounters.UID, defaultAttributes.UID));

    // Write them out to the XML
    defaultAttributesElement->SetAttribute(xml::attrib::GMT_OFFSET, defaultAttributes.GMTOffs);
    defaultAttributesElement->SetAttribute(xml::attrib::XA_ATTRIBUTES, defaultAttributes.XAAttrib);
    defaultAttributesElement->SetAttribute(xml::attrib::XA_PERMISSIONS, defaultAttributes.XAPerm);
    defaultAttributesElement->SetAttribute(xml::attrib::XA_GID, defaultAttributes.GID);
    defaultAttributesElement->SetAttribute(xml::attrib::XA_UID, defaultAttributes.UID);*/
    if (defaultAttributes.HFLAG > 0)
        defaultAttributesElement->SetAttribute(xml::attrib::HIDDEN_FLAG, defaultAttributes.HFLAG);

    return defaultAttributes;
}

static void SimplifyDefaultXMLAttributes(tinyxml2::XMLElement *element, const EntryAttributes &defaults)
{
    // DeleteAttribute can be safely called even if that attribute doesn't exist, so treating failure and default values
    // as equal simplifies logic
    auto deleteAttribute = [element](const char *name, auto defaultValue)
    {
        bool deleteAttribute = false;
        if constexpr (std::is_unsigned_v<decltype(defaultValue)>)
            deleteAttribute = element->UnsignedAttribute(name, defaultValue) == defaultValue;
        else
            deleteAttribute = element->IntAttribute(name, defaultValue) == defaultValue;

        if (deleteAttribute)
            element->DeleteAttribute(name);
    };

    deleteAttribute(xml::attrib::HIDDEN_FLAG, defaults.HFLAG);
    /*deleteAttribute(xml::attrib::GMT_OFFSET, defaults.GMTOffs);
    deleteAttribute(xml::attrib::XA_ATTRIBUTES, defaults.XAAttrib);
    deleteAttribute(xml::attrib::XA_PERMISSIONS, defaults.XAPerm);
    deleteAttribute(xml::attrib::XA_GID, defaults.GID);
    deleteAttribute(xml::attrib::XA_UID, defaults.UID);*/

    int expectedOrder = 0;
    bool isChildElem  = false;
    bool isSequential = true;
    for (tinyxml2::XMLElement *child = element->FirstChildElement(); child != nullptr; child = child->NextSiblingElement())
    {
        SimplifyDefaultXMLAttributes(child, defaults);

        // Check if children entries are perfectly sequenced (0, 1, 2...)
        if (isSequential && std::string_view(child->Name()) != xml::elem::DUMMY)
        {
            isChildElem = true;
            if (child->IntAttribute(xml::attrib::ORDER, -1) != expectedOrder++)
                isSequential = false;
        }
    }

    // Remove the redundant order attributes only if they are perfectly sequential
    if (isChildElem && isSequential)
    {
        for (tinyxml2::XMLElement *child = element->FirstChildElement(); child != nullptr; child = child->NextSiblingElement())
        {
            if (std::string_view(child->Name()) != xml::elem::DUMMY)
                child->DeleteAttribute(xml::attrib::ORDER);
        }
    }
}

static tinyxml2::XMLElement *WriteXMLEntry(const Entry &entry, tinyxml2::XMLElement *dirElement, fs::path *currentVirtualPath = nullptr, tinyxml2::XMLElement *existingElem = nullptr)
{
    tinyxml2::XMLElement *element = existingElem;

    if (entry.type != EntryType::EntryDir)
    {
        element = dirElement->InsertNewChildElement(xml::elem::FILE);
    }
    else
    {
        if (entry.identifier.empty()) [[unlikely]]
        {
            dirElement = element = dirElement->InsertNewChildElement(xml::elem::DIRECTORY_TREE);
            if (!param::lba)
                element->SetAttribute(xml::attrib::ENTRY_SOURCE, srcPath.generic_string().c_str());
            goto write_optional_attr;
        }
        else if (element == nullptr)
        {
            element = dirElement->InsertNewChildElement(xml::elem::DIR);
        }

        dirElement = element;
        if (currentVirtualPath != nullptr)
            *currentVirtualPath /= entry.identifier;
    }

    element->SetAttribute(xml::attrib::ENTRY_NAME, entry.identifier.c_str());
    if (param::lba)
    {
        const fs::path outputPath = srcPath / entry.path / entry.identifier;
        element->SetAttribute(xml::attrib::ENTRY_SOURCE, outputPath.generic_string().c_str());
    }
    if (!param::dir)
        element->SetAttribute(xml::attrib::ENTRY_DATE, DateToString(entry.date, false).c_str());

write_optional_attr:
    WriteOptionalXMLAttribs(element, entry);

    return dirElement;
}

static void WriteXMLGap(const uint32_t numSectors, tinyxml2::XMLElement *dirElement, const uint32_t startSector)
{
    if (numSectors == 0)
        return;

    tinyxml2::XMLElement *newelement = dirElement->InsertNewChildElement(xml::elem::DUMMY);
    newelement->SetAttribute(xml::attrib::NUM_DUMMY_SECTORS, numSectors);
    if (param::lba)
        newelement->SetAttribute(xml::attrib::OFFSET, startSector);
}

static void WriteXMLByLBA(const std::list<Entry> &entries, tinyxml2::XMLElement *dirElement, uint32_t &expectedLBA)
{
    fs::path currentVirtualPath; // Used to find out whether to traverse 'dir' up or down the chain

    // Map of directories and elements
    std::map<fs::path, tinyxml2::XMLElement *> nodeCache{{currentVirtualPath, dirElement}};

    // Pass 1: Trace files and gaps, building structural directory stubs
    for (auto it = std::next(entries.begin()); it != entries.end(); ++it) // Skip root, it's already prepared
    {
        // Skip directories, we will assign their attributes on pass 2
        if (it->type == EntryType::EntryDir)
            continue;

        // Check for gaps
        if (it->lba > expectedLBA)
            WriteXMLGap(it->lba - expectedLBA, dirElement, expectedLBA);

        expectedLBA = std::max(expectedLBA, it->lba + GetSizeInSectors(it->size));

        // Work out the relative position between the current directory and the element to create
        for (const fs::path &part : it->path.lexically_relative(currentVirtualPath))
        {
            if (part == "..")
            {
                // Go up in XML
                dirElement = dirElement->Parent()->ToElement();
                currentVirtualPath = currentVirtualPath.parent_path();
            }
            else if (part != ".")
            {
                // "Enter" the directory
                dirElement = dirElement->InsertNewChildElement(xml::elem::DIR);
                dirElement->SetAttribute(xml::attrib::ENTRY_NAME, part.generic_string().c_str());
                currentVirtualPath /= part;
                nodeCache.try_emplace(currentVirtualPath, dirElement); // Cache the pointer for pass 2
            }
        }

        dirElement = WriteXMLEntry(*it, dirElement, &currentVirtualPath);
    }

    // Pass 2: Assign attributes using cached pointers, insert missing empty directories or out-of-order ones
    for (auto it = std::next(entries.begin()); it != entries.end(); ++it)
    {
        if (it->type != EntryType::EntryDir)
            continue;

        dirElement         = nodeCache[it->path];
        currentVirtualPath = it->path / it->identifier;
        auto targetIt      = nodeCache.find(currentVirtualPath);
        tinyxml2::XMLElement* target = (targetIt != nodeCache.end()) ? targetIt->second : nullptr;

        bool needsNewNode = (target == nullptr);
        if (!needsNewNode)
        {
            // Determine where to start checking backwards.
            // If in the main branch, check siblings. If in a different branch, check the main branch's children.
            auto *node = (target->Parent() == dirElement) ? target->PreviousSiblingElement(xml::elem::DIR) : dirElement->LastChildElement(xml::elem::DIR);
            
            // Any preceding <dir> without the 'order' attribute means it has a higher LBA and hasn't been processed yet.
            for (; node != nullptr; node = node->PreviousSiblingElement(xml::elem::DIR))
            {
                if (node->Attribute(xml::attrib::ORDER) == nullptr)
                {
                    needsNewNode = true;
                    break;
                }
            }
        }

        // Create a new declaration if required to maintain LBA order and correct parent hierarchy
        if (needsNewNode)
        {
            int maxFound = -1;
            tinyxml2::XMLElement *insertAfter = nullptr;
            for (auto *child = dirElement->FirstChildElement(xml::elem::DIR); child != nullptr; child = child->NextSiblingElement(xml::elem::DIR))
            {
                int childOrder  = child->IntAttribute(xml::attrib::ORDER, -1);
                if (childOrder != -1 && childOrder <= it->order && childOrder >= maxFound)
                {
                    maxFound    = childOrder;
                    insertAfter = child;
                }
            }

            // Unlinked creation for empty or out-of-order directories to respect 'order' positioning
            target = dirElement->GetDocument()->NewElement(xml::elem::DIR);
            insertAfter != nullptr ? dirElement->InsertAfterChild(insertAfter, target) : dirElement->InsertFirstChild(target);

            nodeCache.insert_or_assign(targetIt, currentVirtualPath, target);
        }

        WriteXMLEntry(*it, dirElement, nullptr, target);
    }
}

static void WriteXMLByDirectories(const iso::DirTree *directory, tinyxml2::XMLElement *dirElement, uint32_t &expectedLBA)
{
    for (const auto it : directory->GetView())
    {
        // Update the LBA to the max encountered value
        expectedLBA = std::max(expectedLBA, it->lba + GetSizeInSectors(it->size));

        tinyxml2::XMLElement *child = WriteXMLEntry(*it, dirElement);
        // Recursively write children if there are any
        if (const iso::DirTree *subdir = it->subdir.get(); subdir != nullptr)
            WriteXMLByDirectories(subdir, child, expectedLBA);
    }
}

uint32_t xml::Writer::WriteDirTree(const std::list<Entry> &entries, const uint32_t postGap)
{
    // Create <default_attributes> now so it lands before the <directory_tree>
    tinyxml2::XMLElement *defaultAttributesElement = m_projectElement->InsertNewChildElement(xml::elem::DEFAULT_ATTRIBUTES);

    const Entry &root = entries.front();
    tinyxml2::XMLElement *directoryTreeElement = WriteXMLEntry(root, m_projectElement);

    uint32_t currentLBA = root.lbaICB + entries.size(); // This may fail for an image that was not created with CDVDGEN
    if (param::outputSortedByDir)
        WriteXMLByDirectories(root.subdir.get(), directoryTreeElement, currentLBA);
    else
        WriteXMLByLBA(entries, directoryTreeElement, currentLBA);

    // Write post gap, if any
    WriteXMLGap(postGap, directoryTreeElement, currentLBA);
    currentLBA += postGap;

    SimplifyDefaultXMLAttributes(directoryTreeElement, EstablishXMLAttributeDefaults(defaultAttributesElement));

    // Delete the element if it's empty
    if (defaultAttributesElement->FirstAttribute() == nullptr)
        m_projectElement->DeleteChild(defaultAttributesElement);

    return currentLBA;
}

xml::Writer *xml::Writer::WriteHeaders(const std::string &licenseFile)
{
    m_projectElement = static_cast<tinyxml2::XMLElement *>(m_xmlDoc.InsertFirstChild(m_xmlDoc.NewElement(xml::elem::ISO_PROJECT)));
    m_projectElement->SetAttribute(xml::attrib::IMAGE_NAME, "mkps2iso.iso");

    {
        tinyxml2::XMLElement *newElement = m_projectElement->InsertNewChildElement(xml::elem::IDENTIFIERS);
        auto setAttributeIfNotEmpty = [newElement](const char *name, std::string_view value)
        {
            if (!value.empty())
                newElement->SetAttribute(name, std::string(value).c_str());
        };

        setAttributeIfNotEmpty(xml::attrib::SYSTEM_ID, CleanDescElement(iso::descriptor.systemID));
        setAttributeIfNotEmpty(xml::attrib::APPLICATION, CleanDescElement(iso::descriptor.applicationIdentifier));
        setAttributeIfNotEmpty(xml::attrib::VOLUME_ID, CleanDescElement(iso::descriptor.volumeID));
        setAttributeIfNotEmpty(xml::attrib::VOLUME_SET, CleanDescElement(iso::descriptor.volumeSetIdentifier));
        setAttributeIfNotEmpty(xml::attrib::PUBLISHER, CleanDescElement(iso::descriptor.publisherIdentifier));
        setAttributeIfNotEmpty(xml::attrib::DATA_PREPARER, CleanDescElement(iso::descriptor.dataPreparerIdentifier));
        setAttributeIfNotEmpty(xml::attrib::COPYRIGHT, CleanDescElement(iso::descriptor.copyrightFileIdentifier));
        setAttributeIfNotEmpty(xml::attrib::CREATION_DATE, LongDateToString(iso::descriptor.volumeCreateDate).c_str());
        // Set only if not zero
        if (auto ZERO_DATE = GetUnspecifiedLongDate(); memcmp(&iso::descriptor.volumeModifyDate, &ZERO_DATE, sizeof(iso::descriptor.volumeModifyDate)) != 0)
            setAttributeIfNotEmpty(xml::attrib::MODIFICATION_DATE, LongDateToString(iso::descriptor.volumeModifyDate).c_str());
    }

    const fs::path outPath = fs::absolute(param::outPath).lexically_normal();
    const fs::path xmlPath = fs::absolute(param::xmlFile).lexically_normal().parent_path();
    srcPath = param::xmlFile.is_absolute() ? outPath : outPath.lexically_proximate(xmlPath);

    { // Add license element
        tinyxml2::XMLElement *newElement = m_projectElement->InsertNewChildElement(xml::elem::LICENSE);
        newElement->SetAttribute(xml::attrib::LICENSE_FILE, !licenseFile.empty() ? (srcPath / licenseFile).generic_string().c_str() : licenseFile.c_str());
    }

    return this;
}

xml::Writer::Writer()
    : m_filePtr(OpenScopedFile(param::xmlFile, "wb"))
{
    if (m_filePtr == nullptr)
    {
        printf("\nERROR: Cannot create xml file \"%s\". %s\n", param::xmlFile.string().c_str(), strerror(errno));
        exit(EXIT_FAILURE);
    }
}

xml::Writer::~Writer()
{
    m_xmlDoc.SaveFile(m_filePtr.get());
}
