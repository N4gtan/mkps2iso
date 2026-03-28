#pragma once

#include "xml.h"
#include "isoparser.h"

namespace xml
{
    class Writer
    {
        unique_file m_filePtr = nullptr;
        tinyxml2::XMLDocument m_xmlDoc;
        tinyxml2::XMLElement *m_projectElement = nullptr;

    public:
        Writer();
        ~Writer();

        // Writes the elements before <directory_tree> element.
        // Returns self.
        Writer *WriteHeaders(std::string_view serial, std::string_view region);

        // Writes the entries, automatically filling LBA gaps with dummies.
        // Returns the last inferred LBA.
        uint32_t WriteDirTree(const std::list<Entry> &entries, const uint32_t postGap);
    };
}
