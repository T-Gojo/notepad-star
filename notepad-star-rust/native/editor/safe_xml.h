#pragma once
#include "pugixml.hpp"
#include <QByteArray>
#include <QXmlStreamReader>
#include <stdexcept>

namespace star {
inline void loadSafeXml(pugi::xml_document& document, const QByteArray& xml, unsigned int flags) {
    if (xml.size() > 2 * 1024 * 1024) throw std::runtime_error("Import XML exceeds 2 MiB.");
    QXmlStreamReader validation(xml);
    int depth = 0;
    int elements = 0;
    while (!validation.atEnd()) {
        const auto token = validation.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            throw std::runtime_error("Import DTDs and external entities are not supported.");
        if (validation.isStartElement()) {
            if (++depth > 8 || ++elements > 100000 || !validation.namespaceUri().isEmpty())
                throw std::runtime_error("Import XML exceeds its structural limits.");
        } else if (validation.isEndElement()) --depth;
    }
    if (validation.hasError()) throw std::runtime_error("Malformed import XML.");
    if (!document.load_buffer(xml.constData(), static_cast<std::size_t>(xml.size()), flags))
        throw std::runtime_error("Cannot parse import XML.");
}
}
