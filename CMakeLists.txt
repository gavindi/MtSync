cmake_minimum_required(VERSION 3.25)
project(MtSync VERSION 0.9.14 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(SOUP REQUIRED IMPORTED_TARGET libsoup-3.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
find_package(nlohmann_json 3.2 REQUIRED)

# Compile GLib resources into a generated C file
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(RESOURCE_XML "${CMAKE_SOURCE_DIR}/data/mtsync.gresource.xml")
set(RESOURCE_C  "${CMAKE_BINARY_DIR}/mtsync_resources.c")
set(PROVIDER_ICON_DIR "${CMAKE_SOURCE_DIR}/data/provider-icons")
set(PROVIDER_ICONS
    "${PROVIDER_ICON_DIR}/drive.svg"           "${PROVIDER_ICON_DIR}/drive-dark.svg"
    "${PROVIDER_ICON_DIR}/dropbox.svg"         "${PROVIDER_ICON_DIR}/dropbox-dark.svg"
    "${PROVIDER_ICON_DIR}/b2.svg"              "${PROVIDER_ICON_DIR}/b2-dark.svg"
    "${PROVIDER_ICON_DIR}/mega.svg"            "${PROVIDER_ICON_DIR}/mega-dark.svg"
    "${PROVIDER_ICON_DIR}/box.svg"             "${PROVIDER_ICON_DIR}/box-dark.svg"
    "${PROVIDER_ICON_DIR}/gcs.svg"             "${PROVIDER_ICON_DIR}/gcs-dark.svg"
    "${PROVIDER_ICON_DIR}/protondrive.svg"     "${PROVIDER_ICON_DIR}/protondrive-dark.svg"
    "${PROVIDER_ICON_DIR}/internetarchive.svg" "${PROVIDER_ICON_DIR}/internetarchive-dark.svg"
    "${PROVIDER_ICON_DIR}/gphotos.svg"         "${PROVIDER_ICON_DIR}/gphotos-dark.svg"
    "${PROVIDER_ICON_DIR}/seafile.svg"         "${PROVIDER_ICON_DIR}/seafile-dark.svg"
    "${PROVIDER_ICON_DIR}/zoho.svg"            "${PROVIDER_ICON_DIR}/zoho-dark.svg"
    "${PROVIDER_ICON_DIR}/filen.svg"           "${PROVIDER_ICON_DIR}/filen-dark.svg"
    "${PROVIDER_ICON_DIR}/swift.svg"           "${PROVIDER_ICON_DIR}/swift-dark.svg"
    "${PROVIDER_ICON_DIR}/hdfs.svg"            "${PROVIDER_ICON_DIR}/hdfs-dark.svg"
    "${PROVIDER_ICON_DIR}/sharefile.svg"       "${PROVIDER_ICON_DIR}/sharefile-dark.svg"
    "${PROVIDER_ICON_DIR}/digitalocean.svg"    "${PROVIDER_ICON_DIR}/digitalocean-dark.svg"
    "${PROVIDER_ICON_DIR}/wasabi.svg"          "${PROVIDER_ICON_DIR}/wasabi-dark.svg"
    "${PROVIDER_ICON_DIR}/cloudflare.svg"      "${PROVIDER_ICON_DIR}/cloudflare-dark.svg"
    "${PROVIDER_ICON_DIR}/hetzner.svg"         "${PROVIDER_ICON_DIR}/hetzner-dark.svg"
    "${PROVIDER_ICON_DIR}/onedrive.svg"        "${PROVIDER_ICON_DIR}/onedrive-dark.svg"
    "${PROVIDER_ICON_DIR}/pcloud.svg"          "${PROVIDER_ICON_DIR}/pcloud-dark.svg"
    "${PROVIDER_ICON_DIR}/s3.svg"              "${PROVIDER_ICON_DIR}/s3-dark.svg"
    "${PROVIDER_ICON_DIR}/azureblob.svg"       "${PROVIDER_ICON_DIR}/azureblob-dark.svg"
    "${PROVIDER_ICON_DIR}/yandex.svg"          "${PROVIDER_ICON_DIR}/yandex-dark.svg"
    "${PROVIDER_ICON_DIR}/mailru.svg"          "${PROVIDER_ICON_DIR}/mailru-dark.svg"
    "${PROVIDER_ICON_DIR}/koofr.svg"           "${PROVIDER_ICON_DIR}/koofr-dark.svg"
    "${PROVIDER_ICON_DIR}/jottacloud.svg"      "${PROVIDER_ICON_DIR}/jottacloud-dark.svg"
    "${PROVIDER_ICON_DIR}/putio.svg"           "${PROVIDER_ICON_DIR}/putio-dark.svg"
    "${PROVIDER_ICON_DIR}/premiumize.svg"      "${PROVIDER_ICON_DIR}/premiumize-dark.svg"
)

add_custom_command(
    OUTPUT "${RESOURCE_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
        "--target=${RESOURCE_C}"
        "--generate-source"
        "--sourcedir=${CMAKE_SOURCE_DIR}/assets/icons/application"
        "--sourcedir=${CMAKE_SOURCE_DIR}/assets/icons/ui"
        "--sourcedir=${PROVIDER_ICON_DIR}"
        "${RESOURCE_XML}"
    DEPENDS
        "${RESOURCE_XML}"
        "${CMAKE_SOURCE_DIR}/assets/icons/application/MtSync App Icon 1.png"
        "${CMAKE_SOURCE_DIR}/assets/icons/ui/MtSync App Indicator Icon 1.png"
        "${CMAKE_SOURCE_DIR}/assets/icons/ui/MtSync_smoke_spritesheet_16frames_47px.png"
        "${CMAKE_SOURCE_DIR}/assets/icons/ui/MtSync_smoke_spritesheet_16frames_256px.png"
        ${PROVIDER_ICONS}
    COMMENT "Compiling GLib resources"
)

# Collect sources and include the generated resource file
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(mtsync ${SOURCES} "${RESOURCE_C}")

target_include_directories(mtsync PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mtsync PRIVATE
    PkgConfig::GTKMM
    PkgConfig::ADWAITA
    PkgConfig::SOUP
    PkgConfig::CAIRO
    nlohmann_json::nlohmann_json
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mtsync PRIVATE -O3 -flto)
    target_link_options(mtsync PRIVATE -flto)
endif()

install(TARGETS mtsync DESTINATION bin)
install(FILES
    "${CMAKE_SOURCE_DIR}/assets/icons/application/MtSync App Icon 1.png"
    DESTINATION share/icons/hicolor/256x256/apps
    RENAME com.mtsync.MtSync.png
)
install(FILES
    "${CMAKE_SOURCE_DIR}/assets/icons/application/MtSync App Icon 1.svg"
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME com.mtsync.MtSync.svg
)
install(FILES
    "${CMAKE_SOURCE_DIR}/data/com.mtsync.MtSync.desktop"
    DESTINATION share/applications
)

# ── Packaging (CPack) ──────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME             "mtsync")
set(CPACK_PACKAGE_VENDOR           "Mt. Sync")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Mount or sync network storage in comfort")
set(CPACK_PACKAGE_CONTACT          "https://github.com/gavindi/MtSync")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}_${PROJECT_VERSION}_${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES TRUE)

# DEB
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

# Auto-detect gtkmm runtime package name — it varies across Ubuntu releases:
# libgtkmm-4.0-1v5 (22.04), libgtkmm-4.0-1t64 (24.04/25.10), etc.
# find locates the versioned .so installed by libgtkmm-4.0-dev,
# then dpkg -S resolves the owning runtime package.
set(_GTKMM_DEB_PKG "libgtkmm-4.0-1v5")
find_program(_DPKG_CMD dpkg)
if(_DPKG_CMD)
    execute_process(
        COMMAND sh -c "find /usr/lib -maxdepth 3 -name 'libgtkmm-4.0.so.[0-9]*' 2>/dev/null | head -1 | xargs -r dpkg -S 2>/dev/null | cut -d: -f1"
        OUTPUT_VARIABLE _GTKMM_DEB_PKG_DETECTED
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_GTKMM_DEB_PKG_DETECTED)
        set(_GTKMM_DEB_PKG "${_GTKMM_DEB_PKG_DETECTED}")
    endif()
endif()

set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "${_GTKMM_DEB_PKG} (>= 4.6), libadwaita-1-0 (>= 1.1), libsoup-3.0-0, librsvg2-2, rclone")

# RPM
set(CPACK_RPM_PACKAGE_LICENSE  "GPL-2.0-only")
set(CPACK_RPM_PACKAGE_GROUP    "Applications/Internet")
set(CPACK_RPM_PACKAGE_REQUIRES "gtkmm40, libadwaita, libsoup3, rclone")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/applications"
    "/usr/share/icons"
    "/usr/share/icons/hicolor"
    "/usr/share/icons/hicolor/256x256"
    "/usr/share/icons/hicolor/256x256/apps"
    "/usr/share/icons/hicolor/scalable"
    "/usr/share/icons/hicolor/scalable/apps")

include(CPack)
