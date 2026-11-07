// macOS replacement for reference/notepad-plus-plus/PowerEditor/src/pugixml/pugiconfig.hpp.
//
// Force-included ahead of pugixml on Apple targets. Defining the upstream include
// guard first makes pugixml skip its own pugiconfig.hpp, so these settings apply.
//
// Upstream enables PUGIXML_CHARCONV_FLOAT, but libc++ only provides floating-point
// std::from_chars from macOS 26.0 and std::to_chars from macOS 13.3, both later than
// our 13.0 deployment target. Without it pugixml falls back to strtod/snprintf.
// Keep every other setting identical to upstream's pugiconfig.hpp.
#ifndef HEADER_PUGICONFIG_HPP
#define HEADER_PUGICONFIG_HPP

#define PUGIXML_NO_XPATH
#define PUGIXML_HEADER_ONLY

#endif
