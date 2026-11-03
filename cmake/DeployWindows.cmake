# Windows package: the .exe plus the Qt and MinGW runtime DLLs it needs, in <build>/dist,
# zipped by the "package-windows" target. windeployqt is a Windows program, so this is done by
# hand for the few modules the app uses.

get_target_property(DSO_QT_CORE_DLL Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT DSO_QT_CORE_DLL)
    get_target_property(DSO_QT_CORE_DLL Qt6::Core IMPORTED_LOCATION)
endif()
get_filename_component(DSO_QT_BIN "${DSO_QT_CORE_DLL}" DIRECTORY)
get_filename_component(DSO_QT_ROOT "${DSO_QT_BIN}" DIRECTORY)

# MinGW runtime of the cross compiler
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++-6.dll
                OUTPUT_VARIABLE DSO_LIBSTDCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libgcc_s_seh-1.dll
                OUTPUT_VARIABLE DSO_LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE)
find_file(DSO_WINPTHREAD libwinpthread-1.dll PATHS /usr/x86_64-w64-mingw32/lib NO_DEFAULT_PATH)

set(DSO_DIST ${CMAKE_BINARY_DIR}/dist/dso2100scope)
add_custom_target(package-windows
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${DSO_DIST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DSO_DIST}/platforms ${DSO_DIST}/styles
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dso2100scope> ${DSO_DIST}/
    COMMAND ${CMAKE_COMMAND} -E copy
        ${DSO_QT_BIN}/Qt6Core.dll ${DSO_QT_BIN}/Qt6Gui.dll ${DSO_QT_BIN}/Qt6Widgets.dll
        ${DSO_LIBSTDCXX} ${DSO_LIBGCC} ${DSO_WINPTHREAD} ${DSO_DIST}/
    COMMAND ${CMAKE_COMMAND} -E copy ${DSO_QT_ROOT}/plugins/platforms/qwindows.dll ${DSO_DIST}/platforms/
    COMMAND ${CMAKE_COMMAND} -E copy ${DSO_QT_ROOT}/plugins/styles/qmodernwindowsstyle.dll ${DSO_DIST}/styles/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/docs/DRIVERS.md ${DSO_DIST}/
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/dist ${CMAKE_COMMAND} -E tar cf dso2100scope-windows.zip --format=zip dso2100scope
    DEPENDS dso2100scope
    COMMENT "Packaging build/windows/dist/dso2100scope-windows.zip"
)
