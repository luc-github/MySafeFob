# ESP3D Log Component
#
# Supports compile-time backend selection via ESP3D_LOG_BACKEND:
#   0 = Serial (implemented here - no app-level dependency)
#   1 = SD card   (implemented in main/modules/log/sd - depends on ESP3DSd)
#   2 = UART2     (implemented in main/modules/log/uart2 - requires the target
#                  board's board_config.h to define a genuinely separate UART;
#                  #error at compile time if it doesn't - see that file)
#   3 = Telnet    (implemented in main/modules/log/telnet - depends on esp3dSocketServer)
#   4 = WebSocket (implemented in main/modules/log/websocket - depends on esp3dWsServerDataService)
#
# Backends 1-4 depend on app-level services (ESP3DSd, uart_ext, network,
# websocket_server) that this component must stay agnostic of, so they live
# in main/modules/log instead of here (see main/CMakeLists.txt) and provide
# esp3d_log_get_backend() themselves. Only the dependency-free serial backend
# is compiled inside the component itself. Each implemented non-serial value
# MUST have its own real (not commented-out) branch below that adds nothing
# here - falling through to the else() would compile serial.c here too and
# collide with the main/modules/log backend's own esp3d_log_get_backend().

# Core sources (always included)
set(SOURCES "esp3d_log.c")

# Backend selection
if(NOT DEFINED ESP3D_LOG_BACKEND OR ESP3D_LOG_BACKEND EQUAL 0)
    list(APPEND SOURCES "backends/esp3d_log_serial.c")
elseif(ESP3D_LOG_BACKEND EQUAL 1)
    # SD card backend: provided by main/modules/log/sd (added conditionally in
    # main/CMakeLists.txt) - nothing to compile in this component for it.
elseif(ESP3D_LOG_BACKEND EQUAL 3)
    # Telnet backend: provided by main/modules/log/telnet (added conditionally
    # in main/CMakeLists.txt) - nothing to compile in this component for it.
elseif(ESP3D_LOG_BACKEND EQUAL 4)
    # WebSocket backend: provided by main/modules/log/websocket (added
    # conditionally in main/CMakeLists.txt) - nothing to compile here for it.
elseif(ESP3D_LOG_BACKEND EQUAL 2)
    # Secondary UART backend: provided by main/modules/log/uart2 (added
    # conditionally in main/CMakeLists.txt) - nothing to compile here for it.
else()
    message(WARNING "Unknown or not-yet-implemented ESP3D_LOG_BACKEND=${ESP3D_LOG_BACKEND}, defaulting to serial")
    list(APPEND SOURCES "backends/esp3d_log_serial.c")
endif()

set(INCLUDES ".")

idf_component_register(SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDES}
    REQUIRES esp_timer)

# Backends 1+ implement esp3d_log_get_backend() in main/modules/log, i.e. in
# libmain.a - not in this component. The reference to that symbol only
# appears once esp3d_log_init() (in esp3d_log.c, this component) is linked,
# but ESP-IDF links libmain.a exactly once and *before* this component's
# library, with no multi-pass/whole-archive handling for main the way it has
# for components in a declared circular dependency. Without this, the linker
# never revisits libmain.a to pull in the backend's object file, and the
# build fails with an undefined reference - even though the backend code
# itself compiled without any error or warning (verify with a *fresh* build
# directory when changing ESP3D_LOG_BACKEND: an existing one may not detect
# that this file changed and silently keep whatever backend it linked last
# time - see docs/guides/esp3d_log_guide.md).
# "-u <symbol>" forces the linker to treat it as wanted from the very start
# of the link, so it gets pulled out of libmain.a regardless of order -
# same idiom ESP-IDF's own components use (see components/esp_timer/CMakeLists.txt).
if(DEFINED ESP3D_LOG_BACKEND AND NOT ESP3D_LOG_BACKEND EQUAL 0)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp3d_log_get_backend")
endif()
