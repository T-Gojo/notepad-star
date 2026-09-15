#pragma once

#include <cstdint>
#include "rust/cxx.h"

namespace star {
struct FilePath;
struct SearchJob;
struct SearchOutput;
struct OutlineSymbol;
struct LaunchSettings;
rust::Vec<rust::String> outline_keys();
rust::Vec<OutlineSymbol> outline_snapshot(rust::Str text, rust::Str parser);
SearchOutput search_snapshot(const SearchJob& job);
bool lexer_exists(rust::Str name);
bool valid_udl(rust::Str xml);
std::int32_t run_desktop(const LaunchSettings& settings, rust::Vec<FilePath> files);
rust::String instance_client(rust::Str endpoint, rust::Str request);
}
