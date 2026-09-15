cmake_minimum_required(VERSION 3.24)
project(NotepadStarNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  add_compile_options(/EHsc)
  add_compile_options("$<$<CONFIG:Release>:/O2>")
else()
  add_compile_options(-fexceptions)
  add_compile_options("$<$<CONFIG:Release>:-O2>")
endif()
add_compile_definitions("$<$<CONFIG:Release>:NDEBUG>")
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Widgets Core5Compat Test PrintSupport Network)

get_filename_component(REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SCI "${REPOSITORY}/scintilla")
set(LEX "${REPOSITORY}/lexilla")
set(SCI_QT "${SCI}/qt/ScintillaEditBase")

file(GLOB SCINTILLA_SOURCES CONFIGURE_DEPENDS "${SCI}/src/*.cxx")
add_library(star-scintilla STATIC
  ${SCINTILLA_SOURCES}
  "${SCI_QT}/PlatQt.cpp"
  "${SCI_QT}/ScintillaQt.cpp"
  "${SCI_QT}/ScintillaEditBase.cpp"
  "${SCI_QT}/ScintillaQt.h"
  "${SCI_QT}/ScintillaEditBase.h"
  "${REPOSITORY}/boostregex/BoostRegExSearch.cxx"
  "${REPOSITORY}/boostregex/UTF8DocumentIterator.cxx")
target_include_directories(star-scintilla PUBLIC
  "${SCI}/include" "${SCI}/src" "${SCI_QT}"
  "${REPOSITORY}/boostregex")
target_compile_definitions(star-scintilla PUBLIC
  SCINTILLA_QT=1 SCI_OWNREGEX BOOST_REGEX_STANDALONE EXPORT_IMPORT_API=)
target_link_libraries(star-scintilla PUBLIC Qt6::Widgets Qt6::Core5Compat)

file(GLOB LEXILLA_SOURCES CONFIGURE_DEPENDS "${LEX}/lexlib/*.cxx" "${LEX}/lexers/*.cxx")
add_library(star-lexilla STATIC "${LEX}/src/Lexilla.cxx" ${LEXILLA_SOURCES})
target_include_directories(star-lexilla PUBLIC "${LEX}/include" "${LEX}/lexlib" "${SCI}/include")
target_compile_definitions(star-lexilla PRIVATE LEXILLA_NO_EXPORT)
set_target_properties(star-lexilla PROPERTIES AUTOMOC OFF)

add_subdirectory(languages)
add_subdirectory(function_list)
add_library(star-pugixml STATIC "${REPOSITORY}/PowerEditor/src/pugixml/pugixml.cpp")
target_include_directories(star-pugixml PUBLIC "${REPOSITORY}/PowerEditor/src/pugixml")
set_target_properties(star-pugixml PROPERTIES AUTOMOC OFF)
add_library(star-shell STATIC "qt-shell/shell.cpp" "qt-shell/json_panel.cpp" "qt-shell/editor_tabs.cpp" "qt-shell/search_dialog.cpp" "editor/search.cpp" "editor/search_task.cpp" "editor/rich_export.cpp" "editor/project_panel.cpp" "editor/outline.cpp" "editor/macro_import.cpp" "editor/config_import.cpp" "editor/instance_channel.cpp")
target_include_directories(star-shell PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${STAR_CXX_INCLUDE}")
target_link_libraries(star-shell PUBLIC star-pugixml star-function-list star-languages star-scintilla star-lexilla Qt6::Test Qt6::PrintSupport Qt6::Network)
qt_add_resources(star-shell star_application
  PREFIX "/notepad-star"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icons"
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/../resources/icons/notepad-star.png")
qt_add_resources(star-shell star_brand_assets
  PREFIX "/notepad-star/brand"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/../resources/brand"
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/brand/notepad-star-wordmark.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/brand/notepad-star-wordmark-dark.png")
if(MSVC)
  target_compile_options(star-shell PRIVATE /W4 /WX /utf-8)
  target_compile_options(star-scintilla PRIVATE /utf-8)
  target_compile_options(star-lexilla PRIVATE /utf-8)
else()
  target_compile_options(star-shell PRIVATE -Wall -Wextra -Werror)
endif()
install(TARGETS star-shell star-scintilla star-lexilla star-pugixml ARCHIVE DESTINATION lib)
