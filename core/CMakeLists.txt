# Rust core (kestrel-core) built via cargo, linked statically.
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust")
if(WIN32)
  set(CORE_STATIC "${CMAKE_CURRENT_BINARY_DIR}/kestrel_core_rust.lib")
else()
  set(CORE_STATIC "${CMAKE_CURRENT_BINARY_DIR}/libkestrel_core_rust.a")
endif()

find_program(CARGO_EXE cargo HINTS $ENV{HOME}/.cargo/bin "$ENV{CARGO_HOME}/bin")
if(NOT CARGO_EXE)
  message(FATAL_ERROR "cargo not found — install Rust toolchain")
endif()

file(GLOB_RECURSE CORE_SOURCES ${CORE_DIR}/src/*.rs)

add_custom_command(
  OUTPUT ${CORE_STATIC}
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CARGO_TARGET_DIR} ${CARGO_EXE} build --release --manifest-path ${CORE_DIR}/Cargo.toml
  COMMAND ${CMAKE_COMMAND}
    -DSRC_DIR=${CARGO_TARGET_DIR}/release
    -DOUT=${CORE_STATIC}
    -P ${CORE_DIR}/PickRustLib.cmake
  DEPENDS ${CORE_SOURCES} ${CORE_DIR}/Cargo.toml
  USES_TERMINAL
  COMMENT "Building kestrel-core (Rust)"
)

add_custom_target(kestrel_core_target DEPENDS ${CORE_STATIC})

add_library(kestrel_core_iface INTERFACE)
add_dependencies(kestrel_core_iface kestrel_core_target)
target_include_directories(kestrel_core_iface INTERFACE ${CORE_DIR})
if(WIN32)
  target_link_libraries(kestrel_core_iface INTERFACE ${CORE_STATIC} ntdll userenv ws2_32 bcrypt crypt32)
else()
  target_link_libraries(kestrel_core_iface INTERFACE ${CORE_STATIC} pthread dl m)
endif()
