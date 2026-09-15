# SPDX-License-Identifier: GPL-3.0-or-later
# Read upstream, never rewrite it. Fail configure if its mapping shape changes.
set(view_cpp "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/src/ScintillaComponent/ScintillaEditView.cpp")
set(view_h "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/src/ScintillaComponent/ScintillaEditView.h")
set(lexilla_cpp "${STAR_LANGUAGES_UPSTREAM}/lexilla/src/Lexilla.cxx")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${view_cpp}" "${view_h}" "${lexilla_cpp}")
file(READ "${view_cpp}" view_source)
file(READ "${view_h}" view_header)
file(READ "${lexilla_cpp}" lexilla_source)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")

string(REGEX MATCHALL "setLexer\\(L_[A-Z0-9_]+,[^)]*\\)" generic_calls "${view_header}")
foreach(call IN LISTS generic_calls)
  string(REGEX MATCH "setLexer\\((L_[A-Z0-9_]+)," unused "${call}")
  set(lang_enum "${CMAKE_MATCH_1}")
  string(REGEX MATCHALL "LIST_[0-8]" lists "${call}")
  set(mask_${lang_enum} 0)
  foreach(list IN LISTS lists)
    string(REPLACE "LIST_" "" index "${list}")
    math(EXPR mask_${lang_enum} "${mask_${lang_enum}} | (1 << ${index})")
  endforeach()
endforeach()

string(REGEX MATCHALL "\\{L\"[^\"]+\",[ \t]*L\"[^\"]+\",[ \t]*L\"[^\"]+\",[ \t]*L_[A-Z0-9_]+,[ \t]*\"[^\"]+\"\\}" rows "${view_source}")
list(LENGTH rows mapping_count)
if(mapping_count LESS 80)
  message(FATAL_ERROR "Upstream language mapping changed: expected at least 80 entries")
endif()
set(generated "// Generated from checked-in Notepad++ and Lexilla sources. GPL-3.0-or-later.\n")
string(APPEND generated "static const BuiltinMapping builtinMappings[] = {\n")
foreach(row IN LISTS rows)
  string(REGEX MATCH "\\{L\"([^\"]+)\",[ \t]*L\"([^\"]+)\",[ \t]*L\"[^\"]+\",[ \t]*(L_[A-Z0-9_]+),[ \t]*\"([^\"]+)\"\\}" unused "${row}")
  set(id "${CMAKE_MATCH_1}")
  set(display "${CMAKE_MATCH_2}")
  set(lang_enum "${CMAKE_MATCH_3}")
  set(engine "${CMAKE_MATCH_4}")
  set(mask -1)
  if(DEFINED mask_${lang_enum})
    set(mask "${mask_${lang_enum}}")
  endif()
  string(APPEND generated "{\"${id}\", \"${display}\", \"${engine}\", ${mask}},\n")
endforeach()
string(APPEND generated "};\nstatic const char* const availableEngines[] = {\n")
file(GLOB lexer_files CONFIGURE_DEPENDS "${STAR_LANGUAGES_UPSTREAM}/lexilla/lexers/Lex*.cxx")
set(engines "")
foreach(file IN LISTS lexer_files)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" text)
  string(REGEX MATCHALL "LexerModule[ \t\r\n]+lm[A-Za-z0-9_]+\\([^,]+,[^,]+,[ \t\r\n]*\"[^\"]+\"" modules "${text}")
  foreach(module IN LISTS modules)
    string(REGEX MATCH "LexerModule[ \t\r\n]+(lm[A-Za-z0-9_]+)\\([^,]+,[^,]+,[ \t\r\n]*\"([^\"]+)\"" unused "${module}")
    set(symbol "${CMAKE_MATCH_1}")
    set(engine "${CMAKE_MATCH_2}")
    if(lexilla_source MATCHES "&${symbol}[ \t\r\n]*,")
      list(APPEND engines "${engine}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES engines)
list(SORT engines)
foreach(engine IN LISTS engines)
  string(APPEND generated "\"${engine}\",\n")
endforeach()
string(APPEND generated "};\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/upstream_mappings.h" "${generated}")

file(GLOB completion_assets CONFIGURE_DEPENDS "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/installer/APIs/*.xml")
# The GPL text stayed at the repository root when the upstream checkout moved
# under reference/, so give it an explicit alias to keep the embedded resource
# path at :/notepad-star/languages/LICENSE.
get_filename_component(STAR_LANGUAGES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(STAR_LANGUAGES_LICENSE "${STAR_LANGUAGES_ROOT}/LICENSE")
set_source_files_properties("${STAR_LANGUAGES_LICENSE}" PROPERTIES QT_RESOURCE_ALIAS "LICENSE")
set(STAR_LANGUAGE_ASSETS
  "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/src/langs.model.xml"
  "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/src/stylers.model.xml"
  "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/installer/themes/DarkModeDefault.xml"
  "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/bin/userDefineLangs/markdown._preinstalled.udl.xml"
  "${STAR_LANGUAGES_UPSTREAM}/PowerEditor/bin/userDefineLangs/markdown._preinstalled_DM.udl.xml"
  "${STAR_LANGUAGES_LICENSE}"
  "${STAR_LANGUAGES_UPSTREAM}/lexilla/License.txt"
  ${completion_assets})
