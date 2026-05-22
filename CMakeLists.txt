cmake_minimum_required(VERSION 3.21)
project(plasma-show-stdout
	VERSION 0.1
	LANGUAGES CXX
)
include(CheckCXXCompilerFlag)
include(FetchContent)

set(CMAKE_CXX_EXTENSIONS OFF)
if (NOT DEFINED CMAKE_CXX_STANDARD)
	set(CMAKE_CXX_STANDARD 17)
endif()

# Adding a Profile build type
get_property(isMultiConfig GLOBAL
	PROPERTY GENERATOR_IS_MULTICONFIG
)
if(isMultiConfig)
	if(NOT "Profile" IN_LIST CMAKE_CONFIGURATION_TYPES)
		list(APPEND CMAKE_CONFIGURATION_TYPES Profile)
	endif()
	if(NOT "Test" IN_LIST CMAKE_CONFIGURATION_TYPES)
		list(APPEND CMAKE_CONFIGURATION_TYPES Test)
	endif()
else()
	set(allowedBuildTypes Debug Release Profile Test)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
		STRINGS "${allowedBuildTypes}"
	)
	if(NOT CMAKE_BUILD_TYPE)
		set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE) # default build type is Release
	elseif(NOT CMAKE_BUILD_TYPE IN_LIST allowedBuildTypes)
		message(FATAL_ERROR
			"Unknown build type ${CMAKE_BUILD_TYPE}"
		)
	endif()
endif()
#
# Set warnings
#
set(CLANG_WARNINGS
	-Wall
	-Wextra
	-Wconversion
	-Wpedantic
	-Wshadow
	-Wnon-virtual-dtor
	-Wold-style-cast
	-Wcast-align
	-Woverloaded-virtual
	-Wsign-conversion
	-Wnull-dereference
	-Wdouble-promotion
	-Wformat=2
	-Wimplicit-fallthrough
)
set(GCC_WARNINGS
	${CLANG_WARNINGS} 
	-Wmisleading-indentation
	-Wduplicated-cond
	-Wduplicated-branches
	-Wlogical-op
	-Wuseless-cast
)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
	set(PROJECT_WARNINGS_CXX ${CLANG_WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(PROJECT_WARNINGS_CXX ${GCC_WARNINGS})
else()
	message(AUTHOR_WARNING "No compiler warnings set for CXX compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

find_package(ECM 1.4.0 QUIET NO_MODULE)
if(NOT ECM_FOUND)
	FetchContent_Declare(
		ECM
		GIT_REPOSITORY https://invent.kde.org/frameworks/extra-cmake-modules.git
		GIT_TAG        v6.0.0
		GIT_SHALLOW    TRUE
		GIT_PROGRESS   TRUE
	)
	FetchContent_GetProperties(ECM)
	if(NOT ecm_POPULATED)
		FetchContent_Populate(ECM)
	endif()
	set(ECM_MODULE_DIR      "${ecm_SOURCE_DIR}/modules")
	set(ECM_FIND_MODULE_DIR "${ecm_SOURCE_DIR}/find-modules")
	set(ECM_KDE_MODULE_DIR  "${ecm_SOURCE_DIR}/kde-modules")
	set(ECM_MODULE_PATH
		"${ECM_MODULE_DIR}"
		"${ECM_FIND_MODULE_DIR}"
		"${ECM_KDE_MODULE_DIR}"
	)
	set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
else()
	set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH})
endif()

find_package(Qt6 REQUIRED COMPONENTS
	Core
	Qml
)

# Qt6's qtpaths is in Qt's private bin dir, not in PATH.
# Use qmake6 to locate it so ECMQueryQt (called by KDEInstallDirs6) can find it.
unset(QUERY_EXECUTABLE CACHE)
find_program(_qt6_qmake NAMES qmake6 qmake)
if(_qt6_qmake)
	execute_process(
		COMMAND "${_qt6_qmake}" -query QT_INSTALL_BINS
		OUTPUT_VARIABLE _qt6_install_bins
		RESULT_VARIABLE _qt6_query_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(_qt6_query_result EQUAL 0)
		find_program(QUERY_EXECUTABLE
			NAMES qtpaths6 qtpaths
			HINTS "${_qt6_install_bins}"
			NO_DEFAULT_PATH
		)
	endif()
endif()

include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Plasma REQUIRED) # Required for cmake plasma_install_package()

plasma_install_package(package com.github.tonymugen.plasma-show-stdout)

set(plasma_show_stdout_SRCS
	plugin/plasma-show-stdout.cpp
	plugin/scriptModules.cpp
)

add_library(plasma_show_stdout
	SHARED ${plasma_show_stdout_SRCS}
)

target_link_libraries(plasma_show_stdout
	Qt::Core
	Qt::Qml
)

install(
	TARGETS plasma_show_stdout
	DESTINATION ${KDE_INSTALL_QMLDIR}/com/github/tonymugen/plasma-show-stdout
	COMPONENT plasmoid
)
install(
	FILES plugin/qmldir
	DESTINATION ${KDE_INSTALL_QMLDIR}/com/github/tonymugen/plasma-show-stdout
	COMPONENT plasmoid
)
#
# Tests with Catch2
#
option(BUILD_TESTS
	"Build tests"
	OFF
)
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTS)
	FetchContent_Declare(
		Catch2
		GIT_REPOSITORY https://github.com/catchorg/Catch2.git
		GIT_TAG        v3.5.2 # or a later release
		GIT_PROGRESS   TRUE
		SYSTEM                # to disable Catch2 warnings
	)
	FetchContent_MakeAvailable(Catch2)
endif()

#
# Find active available sanitizers
#
set(SANITIZER_LIST "")
if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(SANITIZERS "")
	check_cxx_compiler_flag(-fsanitize=address    HAS_ADDRESS_SANITIZER)
	check_cxx_compiler_flag(-fsanitize=leak       HAS_LEAK_SANITIZER)
	#check_cxx_compiler_flag(-fsanitize=undefined  HAS_UNDEFINED_SANITIZER)
	if(${HAS_ADDRESS_SANITIZER})
		list(APPEND SANITIZERS "address")
	endif()
	if(${HAS_LEAK_SANITIZER})
		list(APPEND SANITIZERS "leak")
	endif()
	list(JOIN
		SANITIZERS
		","
		SANITIZER_LIST)
endif()
add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_com.github.tonymugen.plasma-show-stdout\")

set(CMAKE_CXX_FLAGS_RELEASE
	"-O3 -DNDEBUG"
)

set(CMAKE_CXX_FLAGS_PROFILE
	"-g -p -O3 -DNDEBUG"
)

set(CMAKE_CXX_FLAGS_DEBUG
	"-g -O1"
)

set(CMAKE_CXX_FLAGS_TEST
	"-g -O3"
)


# tests
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTS)
	add_executable(tests
		plugin/tests/tests.cpp
		plugin/scriptModules.cpp
	)
	target_link_libraries(tests
		PRIVATE Catch2::Catch2WithMain
	)
	target_include_directories(tests
		PRIVATE plugin
	)
	include(CTest)
	include(Catch)
	catch_discover_tests(tests)
endif()
