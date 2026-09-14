# Deploys the built plugin DLL into the mod the plugin targets.
#
# While the game is running the loader holds the deployed DLL open and it cannot
# be replaced. That is the normal case during testing, so a locked target is a
# warning rather than a build failure: the new binary is left beside it as
# "<target>.new", to rename over the target once the game has closed.
#
# Inputs (all passed with -D): D2RL_DEPLOY_SOURCE, D2RL_DEPLOY_TARGET.

if(NOT DEFINED D2RL_DEPLOY_SOURCE OR NOT DEFINED D2RL_DEPLOY_TARGET)
	message(FATAL_ERROR "deploy_dll.cmake: D2RL_DEPLOY_SOURCE and D2RL_DEPLOY_TARGET are required.")
endif()

if(NOT EXISTS "${D2RL_DEPLOY_SOURCE}")
	message(FATAL_ERROR "deploy_dll.cmake: built DLL not found at '${D2RL_DEPLOY_SOURCE}'.")
endif()

set(pending "${D2RL_DEPLOY_TARGET}.new")
get_filename_component(deploy_dir "${D2RL_DEPLOY_TARGET}" DIRECTORY)

file(MAKE_DIRECTORY "${deploy_dir}")

# Refresh the pending copy first. This one is never locked, so it always works
# and finishing the job is a plain rename.
file(COPY_FILE "${D2RL_DEPLOY_SOURCE}" "${pending}" ONLY_IF_DIFFERENT RESULT pending_error)
if(pending_error)
	message(WARNING "Auto Gem Bag: could not stage '${pending}': ${pending_error}")
	return()
endif()

file(COPY_FILE "${D2RL_DEPLOY_SOURCE}" "${D2RL_DEPLOY_TARGET}" ONLY_IF_DIFFERENT RESULT deploy_error)
if(deploy_error)
	message(STATUS "Auto Gem Bag: '${D2RL_DEPLOY_TARGET}' is in use; staged as '${pending}'. Close the game and rename it over the target.")
else()
	message(STATUS "Auto Gem Bag: deployed to '${D2RL_DEPLOY_TARGET}'.")
endif()
