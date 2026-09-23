cmake_minimum_required(VERSION 3.28)

project(D2RLAutoDepositPlugin VERSION 0.1.4 LANGUAGES CXX RC)

# Where the plugin SDK comes from, in order of preference:
#
#   1. -DD2RLPLUGIN_SDK_DIR=<path>  an existing checkout, used as it is
#   2. a checkout inside or next to this project (external/ first, then the
#      usual "cloned beside the project" layouts)
#   3. GitHub, cloned into build/_deps at configure time
#
# A fresh clone has none of (1) or (2) and gets (3), so there is nothing to set
# up by hand. (2) exists so a machine that already has the SDK builds offline.
set(D2RLPLUGIN_SDK_DIR "" CACHE PATH "Existing D2RLoader Plugin SDK checkout; empty to look beside this project and fall back to GitHub")

set(D2RLPLUGIN_SDK_GIT_REPOSITORY "https://github.com/D2RLoader/PluginSDK.git" CACHE STRING "SDK repository to clone when no local checkout is found")
set(D2RLPLUGIN_SDK_GIT_TAG "v4" CACHE STRING "SDK tag or commit to clone when no local checkout is found")

if(D2RLPLUGIN_SDK_DIR)
	if(NOT EXISTS "${D2RLPLUGIN_SDK_DIR}/include/D2RLPlugin/api.h")
		message(FATAL_ERROR "No D2RLoader Plugin SDK at '${D2RLPLUGIN_SDK_DIR}' - leave D2RLPLUGIN_SDK_DIR empty to fetch one instead.")
	endif()
else()
	foreach(_sdk_candidate
		"${CMAKE_CURRENT_SOURCE_DIR}/external/d2r-pluginsdk"
		"${CMAKE_CURRENT_SOURCE_DIR}/d2r-pluginsdk"
		"${CMAKE_CURRENT_SOURCE_DIR}/../d2r-pluginsdk"
		"${CMAKE_CURRENT_SOURCE_DIR}/../../d2r-pluginsdk")
		if(EXISTS "${_sdk_candidate}/include/D2RLPlugin/api.h")
			get_filename_component(D2RLPLUGIN_SDK_DIR "${_sdk_candidate}" ABSOLUTE)
			set(D2RLPLUGIN_SDK_DIR "${D2RLPLUGIN_SDK_DIR}" CACHE PATH "Existing D2RLoader Plugin SDK checkout; empty to look beside this project and fall back to GitHub" FORCE)
			break()
		endif()
	endforeach()
endif()

# Build only the SDK interface target, not its sample plugins or contract tests.
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(D2RLPLUGIN_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(D2RLPLUGIN_SDK_DIR)
	message(STATUS "Plugin SDK: using the checkout at ${D2RLPLUGIN_SDK_DIR}")
	add_subdirectory("${D2RLPLUGIN_SDK_DIR}" "${CMAKE_BINARY_DIR}/d2rpluginsdk")
else()
	include(FetchContent)
	message(STATUS "Plugin SDK: no local checkout, cloning ${D2RLPLUGIN_SDK_GIT_TAG} from ${D2RLPLUGIN_SDK_GIT_REPOSITORY}")
	FetchContent_Declare(D2RLPluginSDK
		GIT_REPOSITORY "${D2RLPLUGIN_SDK_GIT_REPOSITORY}"
		GIT_TAG "${D2RLPLUGIN_SDK_GIT_TAG}"
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(D2RLPluginSDK)
endif()

# Listed rather than globbed, so a file that stops being compiled is a decision
# someone made and not something a rebuild noticed. One entry per module, in the
# order the plugin reads them: the entry point, then the game-facing half, then
# the bag, then the report that reads without acting, then the pickup that feeds
# both, then the console.
add_library(D2RLAutoDepositPlugin SHARED
	src/deposit_plugin.cpp
	src/deposit_plugin.rc
	src/deposit_common.cpp
	src/deposit_config.cpp
	src/deposit_native.cpp
	src/deposit_stash.cpp
	src/deposit_gem_bag.cpp
	src/deposit_report.cpp
	src/deposit_pickup.cpp
	src/deposit_console.cpp
)

target_include_directories(D2RLAutoDepositPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(D2RLAutoDepositPlugin PRIVATE D2RLPlugin::D2RLPlugin)

set_target_properties(D2RLAutoDepositPlugin PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED TRUE
	CXX_EXTENSIONS FALSE
	OUTPUT_NAME "d2rl-auto-deposit"
)

if(MSVC)
	target_compile_options(D2RLAutoDepositPlugin PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /permissive- /EHsc>)
endif()

# Where to copy the built DLL after a successful build, if anywhere. Empty by
# default: no one machine's paths belong in the project. Point it at the plugins
# folder of the mod you are testing against and CMake remembers it:
#
#   cmake -S . -B build -DD2RL_AUTODEPOSIT_DEPLOY_DIR="<mod>/d2rloader/plugins"
#
# While it is empty the build only builds.
set(D2RL_AUTODEPOSIT_DEPLOY_DIR "" CACHE PATH "Copy the built DLL here after building; empty to only build")

# The default config. D2RLoader writes d2rloader/config/d2rl-auto-deposit.toml
# out of this the first time the plugin loads, and never rewrites it.
d2rlplugin_embed_config(
	TARGET D2RLAutoDepositPlugin
	FILE "${CMAKE_CURRENT_SOURCE_DIR}/auto-deposit.toml"
)

if(D2RL_AUTODEPOSIT_DEPLOY_DIR)
	# A locked target (the game is running) must warn, not fail the build.
	add_custom_command(TARGET D2RLAutoDepositPlugin POST_BUILD
		COMMAND "${CMAKE_COMMAND}"
			"-DD2RL_DEPLOY_SOURCE=$<TARGET_FILE:D2RLAutoDepositPlugin>"
			"-DD2RL_DEPLOY_TARGET=${D2RL_AUTODEPOSIT_DEPLOY_DIR}/d2rl-auto-deposit.dll"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_dll.cmake"
		COMMENT "Placing d2rl-auto-deposit.dll in ${D2RL_AUTODEPOSIT_DEPLOY_DIR}"
		VERBATIM
	)
else()
	message(STATUS "D2RL_AUTODEPOSIT_DEPLOY_DIR is empty - building only. The DLL lands in ${CMAKE_CURRENT_BINARY_DIR}.")
endif()
