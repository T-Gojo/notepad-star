cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(NotepadStarFunctionList LANGUAGES CXX)
  set(STAR_FUNCTION_TEST_DEFAULT ON)
else()
  set(STAR_FUNCTION_TEST_DEFAULT OFF)
endif()
option(STAR_FUNCTION_LIST_BUILD_TESTS "Build standalone function-list tests" ${STAR_FUNCTION_TEST_DEFAULT})
get_filename_component(STAR_FUNCTION_UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/../../reference/notepad-plus-plus" ABSOLUTE)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)

add_library(star-function-list STATIC definition_catalog.cpp extractor.cpp)
target_compile_features(star-function-list PUBLIC cxx_std_17)
target_include_directories(star-function-list PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(star-function-list PUBLIC Qt6::Core)
set_target_properties(star-function-list PROPERTIES AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  set_property(TARGET star-function-list PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  target_compile_options(star-function-list PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(star-function-list PRIVATE -Wall -Wextra -Werror)
endif()
file(GLOB function_assets CONFIGURE_DEPENDS "${STAR_FUNCTION_UPSTREAM}/PowerEditor/installer/functionList/*.xml")
# The GPL text stayed at the repository root when the upstream checkout moved
# under reference/, so give it an explicit alias to keep the embedded resource
# path at :/notepad-star/function-list/LICENSE.
get_filename_component(STAR_FUNCTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set_source_files_properties("${STAR_FUNCTION_ROOT}/LICENSE" PROPERTIES QT_RESOURCE_ALIAS "LICENSE")
qt_add_resources(star-function-list star_function_list_assets
  PREFIX "/notepad-star/function-list" BASE "${STAR_FUNCTION_UPSTREAM}"
  FILES ${function_assets} "${STAR_FUNCTION_ROOT}/LICENSE")
install(TARGETS star-function-list ARCHIVE DESTINATION lib)

if(STAR_FUNCTION_LIST_BUILD_TESTS)
  enable_testing()
  # Same core list as scintilla/test/unit/makefile; no editor or platform UI.
  set(core_names CaseConvert CaseFolder CellBuffer ChangeHistory CharacterCategoryMap
    CharClassify ContractionState Decoration Document Geometry PerLine RESearch
    RunStyles Selection UndoHistory UniConversion UniqueString)
  set(core_sources "")
  foreach(name IN LISTS core_names)
    list(APPEND core_sources "${STAR_FUNCTION_UPSTREAM}/scintilla/src/${name}.cxx")
  endforeach()
  add_library(star-function-test-search STATIC ${core_sources}
    "${STAR_FUNCTION_UPSTREAM}/boostregex/BoostRegExSearch.cxx"
    "${STAR_FUNCTION_UPSTREAM}/boostregex/UTF8DocumentIterator.cxx"
    tests/scintilla_search.cpp)
  target_include_directories(star-function-test-search PRIVATE
    "${STAR_FUNCTION_UPSTREAM}/scintilla/include"
    "${STAR_FUNCTION_UPSTREAM}/scintilla/src"
    "${STAR_FUNCTION_UPSTREAM}/boostregex")
  target_compile_definitions(star-function-test-search PRIVATE SCI_OWNREGEX BOOST_REGEX_STANDALONE)
  target_link_libraries(star-function-test-search PUBLIC star-function-list)
  set_target_properties(star-function-test-search PROPERTIES AUTOMOC OFF)
  add_executable(star-function-list-tests tests/function_list_tests.cpp)
  target_link_libraries(star-function-list-tests PRIVATE star-function-test-search)
  set_target_properties(star-function-list-tests PROPERTIES AUTOMOC OFF)
  if(MSVC)
    set_property(TARGET star-function-test-search star-function-list-tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(star-function-test-search PRIVATE /utf-8)
    target_compile_options(star-function-list-tests PRIVATE /W4 /WX /utf-8)
  endif()
  set(fixture_assets "")
  foreach(language rust python cpp xml)
    file(GLOB_RECURSE fixtures LIST_DIRECTORIES false
      "${STAR_FUNCTION_UPSTREAM}/PowerEditor/Test/FunctionList/${language}/unitTest"
      "${STAR_FUNCTION_UPSTREAM}/PowerEditor/Test/FunctionList/${language}/unitTest.expected.result")
    list(APPEND fixture_assets ${fixtures})
  endforeach()
  qt_add_resources(star-function-list-tests star_function_test_fixtures
    PREFIX "/notepad-star/function-list-tests" BASE "${STAR_FUNCTION_UPSTREAM}" FILES ${fixture_assets})
  add_test(NAME star-function-list COMMAND star-function-list-tests)
  add_executable(star-function-list-archive-test tests/archive_smoke.cpp)
  add_dependencies(star-function-list-archive-test star-function-list)
  target_compile_features(star-function-list-archive-test PRIVATE cxx_std_17)
  target_include_directories(star-function-list-archive-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(star-function-list-archive-test PRIVATE "$<TARGET_FILE:star-function-list>" Qt6::Core)
  set_target_properties(star-function-list-archive-test PROPERTIES AUTOMOC OFF)
  if(MSVC)
    set_property(TARGET star-function-list-archive-test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(star-function-list-archive-test PRIVATE /W4 /WX /utf-8)
  endif()
  add_test(NAME star-function-list-archive-link COMMAND star-function-list-archive-test)
  get_target_property(STAR_FUNCTION_QMAKE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(STAR_FUNCTION_QT_BIN "${STAR_FUNCTION_QMAKE}" DIRECTORY)
  set_tests_properties(star-function-list star-function-list-archive-link PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${STAR_FUNCTION_QT_BIN}" TIMEOUT 60)
endif()
