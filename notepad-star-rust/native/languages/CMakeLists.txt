cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(NotepadStarLanguages LANGUAGES CXX)
  set(STAR_LANGUAGES_TEST_DEFAULT ON)
else()
  set(STAR_LANGUAGES_TEST_DEFAULT OFF)
endif()
option(STAR_LANGUAGES_BUILD_TESTS "Build standalone language catalog tests" ${STAR_LANGUAGES_TEST_DEFAULT})
get_filename_component(STAR_LANGUAGES_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui)

include("${CMAKE_CURRENT_SOURCE_DIR}/generate_assets.cmake")
add_library(star-languages STATIC language_catalog.cpp xml.cpp udl.cpp)
target_compile_features(star-languages PUBLIC cxx_std_17)
target_include_directories(star-languages PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${STAR_LANGUAGES_REPOSITORY}/lexilla/include")
target_link_libraries(star-languages PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(star-languages PROPERTIES AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  set_property(TARGET star-languages PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  target_compile_options(star-languages PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(star-languages PRIVATE -Wall -Wextra -Werror)
endif()
qt_add_resources(star-languages star_language_assets
  PREFIX "/notepad-star/languages"
  BASE "${STAR_LANGUAGES_REPOSITORY}"
  FILES ${STAR_LANGUAGE_ASSETS})
install(TARGETS star-languages ARCHIVE DESTINATION lib)

if(STAR_LANGUAGES_BUILD_TESTS)
  enable_testing()
  file(GLOB LANGUAGE_TEST_LEXLIB CONFIGURE_DEPENDS "${STAR_LANGUAGES_REPOSITORY}/lexilla/lexlib/*.cxx")
  add_library(star-language-test-lexers STATIC ${LANGUAGE_TEST_LEXLIB}
    "${STAR_LANGUAGES_REPOSITORY}/lexilla/lexers/LexCPP.cxx"
    "${STAR_LANGUAGES_REPOSITORY}/lexilla/lexers/LexPython.cxx"
    "${STAR_LANGUAGES_REPOSITORY}/lexilla/lexers/LexRust.cxx"
    "${STAR_LANGUAGES_REPOSITORY}/lexilla/lexers/LexUser.cxx")
  target_compile_features(star-language-test-lexers PUBLIC cxx_std_17)
  target_include_directories(star-language-test-lexers PUBLIC
    "${STAR_LANGUAGES_REPOSITORY}/lexilla/include"
    "${STAR_LANGUAGES_REPOSITORY}/lexilla/lexlib"
    "${STAR_LANGUAGES_REPOSITORY}/scintilla/include")
  set_target_properties(star-language-test-lexers PROPERTIES AUTOMOC OFF)
  add_executable(star-language-tests tests/catalog_tests.cpp)
  target_link_libraries(star-language-tests PRIVATE star-languages star-language-test-lexers)
  set_target_properties(star-language-tests PROPERTIES AUTOMOC OFF)
  add_executable(star-language-archive-test tests/archive_smoke.cpp)
  add_dependencies(star-language-archive-test star-languages)
  target_compile_features(star-language-archive-test PRIVATE cxx_std_17)
  target_include_directories(star-language-archive-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  # Deliberately bypass target transitive resource initialization, like Rust's
  # explicit static-archive link. Q_INIT_RESOURCE must retain the resource.
  target_link_libraries(star-language-archive-test PRIVATE "$<TARGET_FILE:star-languages>" Qt6::Core Qt6::Gui)
  set_target_properties(star-language-archive-test PROPERTIES AUTOMOC OFF)
  if(MSVC)
    set_property(TARGET star-language-test-lexers star-language-tests star-language-archive-test
      PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(star-language-test-lexers PRIVATE /utf-8)
    target_compile_options(star-language-tests PRIVATE /W4 /WX /utf-8)
    target_compile_options(star-language-archive-test PRIVATE /W4 /WX /utf-8)
  endif()
  add_test(NAME star-language-catalog COMMAND star-language-tests)
  add_test(NAME star-language-archive-link COMMAND star-language-archive-test)
  get_target_property(STAR_QMAKE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(STAR_QT_BIN "${STAR_QMAKE}" DIRECTORY)
  set_tests_properties(star-language-catalog star-language-archive-link PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${STAR_QT_BIN}"
    TIMEOUT 60)
endif()
