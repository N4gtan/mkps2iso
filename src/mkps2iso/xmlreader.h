#pragma once

#include "xml.h"
#include "isobuilder.h"

namespace xml
{
    class Reader
    {
        tinyxml2::XMLDocument m_xmlDoc;
        tinyxml2::XMLDocument m_xmlIdFile;
        tinyxml2::XMLElement *m_layerElement = nullptr;
        tinyxml2::XMLElement *m_projectElement = nullptr;

        static bool Open(const fs::path &fileName, tinyxml2::XMLDocument &doc);

    public:
        Reader();

        // Parses the elements before <directory_tree> element.
        // Returns self.
        Reader *ReadHeaders(std::string &serial, const char *&region);

        // Parses <directory_tree> element.
        // Returns the generated DirTree.
        iso::DirTree *ReadDirTree(std::list<Entry> &entries);

        tinyxml2::XMLElement *NextLayerElement();
        tinyxml2::XMLElement *NextProjectElement();
    };
}
