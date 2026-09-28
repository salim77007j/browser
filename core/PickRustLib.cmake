# PickRustLib.cmake — copy whichever static lib cargo produced to the canonical name
foreach(c kestrel_core.lib libkestrel_core.a)
  if(EXISTS "${SRC_DIR}/${c}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC_DIR}/${c}" "${OUT}")
    message(STATUS "kestrel-core: using ${c}")
    return()
  endif()
endforeach()
message(FATAL_ERROR "kestrel-core: no static library found in ${SRC_DIR}")
