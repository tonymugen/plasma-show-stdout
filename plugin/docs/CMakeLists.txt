find_package(Doxygen REQUIRED)

FetchContent_Declare(
	awesome
	GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
	GIT_TAG        v2.3.4 # or a later release
	GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(awesome)

file(GLOB PUBLIC_HEADERS ${PROJECT_SOURCE_DIR}/plugin/*.hpp)

set(DOXYGEN_INPUT_DIR  ${PROJECT_SOURCE_DIR}/plugin)
set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/doxygen)
set(README_FILE        ${PROJECT_SOURCE_DIR}/README.md)
set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/html/index.html)
set(DOXYFILE_IN        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(DOXYFILE_OUT       ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
# Doxyfile.in references @AWESOME_CSS_DIR@/doxygen-awesome.css, so this must be
# the directory holding the stylesheet, not the file itself.
set(AWESOME_CSS_DIR    ${awesome_SOURCE_DIR})

# Replace variables in Doxyfile
configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})
add_custom_command(
	OUTPUT            ${DOXYGEN_INDEX_FILE}
	DEPENDS           ${PUBLIC_HEADERS}
	                  ${README_FILE}
	COMMAND           ${DOXYGEN_EXECUTABLE}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	MAIN_DEPENDENCY   ${DOXYFILE_IN}
	COMMENT           "Generating docs"
	VERBATIM
)
add_custom_target(Doxygen
	ALL
	DEPENDS ${DOXYGEN_INDEX_FILE}
)
