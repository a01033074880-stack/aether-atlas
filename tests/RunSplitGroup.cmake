# Runs the Resynth-split test executables of BOTH plugin forms for one group and fails if either fails.
# Usage: cmake -DFX_EXE=<exe> -DSYNTH_EXE=<exe> -DGROUP=<unchanged|split> -P RunSplitGroup.cmake
set(failed 0)
foreach(exe IN ITEMS "${FX_EXE}" "${SYNTH_EXE}")
  message(STATUS "=== ${exe} --group ${GROUP}")
  execute_process(COMMAND "${exe}" --group ${GROUP} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(STATUS "=== FAILED (exit ${rc}): ${exe}")
    set(failed 1)
  endif()
endforeach()
if(failed)
  message(FATAL_ERROR "group '${GROUP}' failed")
endif()
