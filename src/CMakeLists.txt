file(GLOB app_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER app_sources EXCLUDE REGEX "/RadioDiagnostic\\.cpp$")

idf_component_register(SRCS ${app_sources})
