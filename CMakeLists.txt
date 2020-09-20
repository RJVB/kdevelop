cmake_minimum_required(VERSION 3.0)
project(KDevelop VERSION 5.6.0)

# KDevelop SOVERSION
# E.g. for KDevelop 5.2.0 => SOVERSION 52 (we only promise ABI compatibility between patch version updates)
set(KDEVELOP_SOVERSION 56)

# plugin version as used e.g. in plugin installation path
set(KDEV_PLUGIN_VERSION 34)

option(KDEVELOP_BUILD_IDE "Build the IDE" ON)
option(KDEVELOP_BUILD_CLANG_PARSER "Build the Clang-based C/C++ parser (formerly kdev-clang)" ON)

if(APPLE OR WIN32)
    option(KDEVELOP_NATIVE_DIALOGS "Use native file dialogs instead of KDE dialogs" ON)
else()
    option(KDEVELOP_NATIVE_DIALOGS "Use native file dialogs instead of KDE dialogs" OFF)
endif()

# we need some parts of the ECM CMake helpers
find_package (ECM 5.50.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${KDevelop_SOURCE_DIR}/cmake/modules ${ECM_MODULE_PATH})

if (NOT KDEVELOP_BUILD_IDE)
    if (NOT KDEVELOP_BUILD_CLANG_PARSER)
        message(FATAL_ERROR "KDEVELOP_BUILD_IDE and KDEVELOP_BUILD_CLANG_PARSER cannot both be OFF!")
    else()
        message(WARNING "Invoke make in ${CMAKE_CURRENT_BINARY_DIR}/plugins/clang in order to build only kdev-clang!")
    endif()
endif()

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE) # needs to be first, as set policies influence following macros

include(ECMOptionalAddSubdirectory)
include(ECMInstallIcons)
include(ECMAddAppIcon)
include(ECMSetupVersion)
include(ECMAddTests)
include(ECMMarkNonGuiExecutable)
include(ECMGenerateHeaders)
include(ECMQtDeclareLoggingCategory)

include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(FeatureSummary)
include(WriteBasicConfigVersionFile)
include(CheckFunctionExists)

include(KDevelopMacrosInternal)

if(POLICY CMP0071)
  # CMake 3.10 generates warnings when projects combine AUTOMOC with qt5_wrap_ui() or qt5_add_resources()
  # Avoid that by setting this policy (cf. https://bugreports.qt.io/browse/QTBUG-63442)
  # Note: Once we depend on a Qt which has this fix (likely Qt 5.9.4+), remove this
  cmake_policy(SET CMP0071 OLD)
endif()

include(GetGitRevisionDescription)

set(QT_MIN_VERSION "5.8.0")
find_package(Qt5 ${QT_MIN_VERSION} CONFIG REQUIRED Widgets Concurrent Quick QuickWidgets)
if(BUILD_TESTING)
  find_package(Qt5Test ${QT_MIN_VERSION} CONFIG REQUIRED)
endif()
set(KF5_DEP_VERSION "5.50.0")
find_package(KF5 ${KF5_DEP_VERSION} REQUIRED COMPONENTS
  Config
  Declarative
  DocTools
  IconThemes
  I18n
  ItemModels
  ItemViews
  JobWidgets
  KCMUtils
  KIO
  NewStuff
  NotifyConfig
  Parts
  Service
  TextEditor
  ThreadWeaver
  XmlGui
  WindowSystem
  Crash
  GuiAddons
  Archive
  Notifications
)

set(HAVE_KSYSGUARD FALSE)
find_package(KSysGuard CONFIG QUIET)
set_package_properties(KSysGuard PROPERTIES
    PURPOSE "Framework for process listing. Required for the 'Attach to Process' feature"
    TYPE RECOMMENDED
)
if(NOT KSysGuard_FOUND)
    # try old pre-Plasma 5.19 name
    find_package(KF5SysGuard CONFIG)
    set_package_properties(KF5SysGuard PROPERTIES
        PURPOSE "Framework for process listing (older name of KSysGuard). Required for the 'Attach to Process' feature"
        TYPE RECOMMENDED
    )
endif()
if(KSysGuard_FOUND OR KF5SysGuard_FOUND)
    set(HAVE_KSYSGUARD TRUE)
endif()

find_package(KDevelop-PG-Qt 1.90.90 CONFIG)
set_package_properties(KDevelop-PG-Qt PROPERTIES
    PURPOSE "KDevelop parser generator library. Required for the QMake Builder/Manager plugin."
    TYPE RECOMMENDED
)

find_package(SharedMimeInfo REQUIRED)

add_definitions(
    -DQT_DEPRECATED_WARNINGS
    -DQT_DISABLE_DEPRECATED_BEFORE=0x050800
    -DQT_DEPRECATED_WARNINGS_SINCE=0x060000
    -DQT_NO_SIGNALS_SLOTS_KEYWORDS
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_NO_CAST_FROM_ASCII
    -DQT_NO_CAST_TO_ASCII
    -DQT_NO_CAST_FROM_BYTEARRAY
    -DQT_STRICT_ITERATORS
    -DQT_USE_QSTRINGBUILDER
    -DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    -DKF_DISABLE_DEPRECATED_BEFORE_AND_AT=0x053200
    -DKF_DEPRECATED_WARNINGS_SINCE=0x060000
)
if(NOT Qt5_VERSION VERSION_LESS "5.12.0" AND NOT KF5_VERSION VERSION_LESS "5.56.0")
    add_definitions(
        -DQT_NO_FOREACH
    )
endif()

if(KDEVELOP_NATIVE_DIALOGS)
    add_definitions(-DKDEV_USE_NATIVE_DIALOGS)
endif()

# Turn off missing-field-initializers warning for GCC to avoid noise from false positives with empty {}
# See discussion: https://mail.kde.org/pipermail/kdevelop-devel/2014-February/046910.html
add_compile_flag_if_supported(-Wno-missing-field-initializers)
add_compile_flag_if_supported(-Werror=switch)
add_compile_flag_if_supported(-Werror=undefined-bool-conversion)
add_compile_flag_if_supported(-Werror=tautological-undefined-compare)
add_compile_flag_if_supported(-Werror=implicit-fallthrough)    # Use Q_FALLTHROUGH for false positives.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_flag_if_supported(-Wdocumentation)
    add_compile_flag_if_supported(-Wcovered-switch-default)
    add_compile_flag_if_supported(-Wunreachable-code-break)
    # This warning is triggered by every call to qCDebug()
    add_compile_flag_if_supported(-Wno-gnu-zero-variadic-macro-arguments)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_flag_if_supported(-pedantic)
    add_compile_flag_if_supported(-Wzero-as-null-pointer-constant CXX_ONLY)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_flag_if_supported(-Wsuggest-override CXX_ONLY)
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_TOLOWER)
if(CMAKE_BUILD_TYPE_TOLOWER MATCHES "debug"
  OR CMAKE_BUILD_TYPE_TOLOWER STREQUAL "")
  set(COMPILER_OPTIMIZATIONS_DISABLED TRUE)
else()
  set(COMPILER_OPTIMIZATIONS_DISABLED FALSE)
endif()

# create config-kdevelop.h
configure_file(config-kdevelop.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kdevelop.h)
include_directories(${KDevelop_BINARY_DIR}) # for config-kdevelop.h

# create kdevelop_fullversion.h
set(KDevelop_FULL_VERSION "${KDevelop_VERSION_MAJOR}.${KDevelop_VERSION_MINOR}.${KDevelop_VERSION_PATCH}")
git_describe(GIT_FULL_VERSION "--tags")
if(GIT_FULL_VERSION)
    # development build, i.e. from a git working copy.
    # Get the current commit version using `git describe` and
    # strip the leading v plus the part that matches the version
    # declared in the project definition above.
    string(REPLACE "v${KDevelop_FULL_VERSION}" "" COMMIT_VERSION "${GIT_FULL_VERSION}")
    if(NOT ${COMMIT_VERSION} MATCHES ${GIT_FULL_VERSION})
        # `git describe` agrees with KDevelop_VERSION_STRING;
        # make an atomic version string by appending the additional info
        string(APPEND KDevelop_FULL_VERSION ${COMMIT_VERSION})
    else()
        # `git describe` provides a seemingly unrelated version string;
        # append it in parentheses.
        string(APPEND KDevelop_FULL_VERSION " (${GIT_FULL_VERSION})")
    endif()
    message(STATUS "Setting KDevelop version to: ${KDevelop_FULL_VERSION}")
endif()
configure_file(kdevelop_fullversion.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/kdevelop_fullversion.h)

if (KDEVELOP_BUILD_IDE)
    add_subdirectory(kdevplatform)
else()
    message(STATUS "Checking for installed KDevPlatform v${KDevelop_VERSION_MAJOR}.${KDevelop_VERSION_MINOR}")
    find_package(KDevPlatform ${KDevelop_VERSION_MAJOR}.${KDevelop_VERSION_MINOR} CONFIG)
    set_package_properties(KDevPlatform PROPERTIES
        TYPE REQUIRED
    )
endif()
add_subdirectory(plugins)

add_subdirectory(pics)
add_subdirectory(app)
add_subdirectory(app_templates)
add_subdirectory(file_templates)
add_subdirectory(shortcuts)
add_subdirectory(doc)
add_subdirectory(share)

set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KDevelop")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/KDevelopConfig.cmake.in"
                                  "${CMAKE_CURRENT_BINARY_DIR}/KDevelopConfig.cmake"
                                  INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)
ecm_setup_version(${KDevelop_VERSION_MAJOR}.${KDevelop_VERSION_MINOR}.${KDevelop_VERSION_PATCH}
                  VARIABLE_PREFIX KDEVELOP
                  VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kdevelop_version.h"
                  PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/KDevelopConfigVersion.cmake"
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/kdevelop_version.h"
        DESTINATION "${KDE_INSTALL_INCLUDEDIR}/kdevelop")
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/KDevelopConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/KDevelopConfigVersion.cmake"
        DESTINATION "${CMAKECONFIG_INSTALL_DIR}" )
install(EXPORT KDevelopTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" NAMESPACE KDev:: FILE KDevelopTargets.cmake)

# kdebugsettings file
install_qt_logging_categories(TYPE APP_PLUGIN)

# CTestCustom.cmake has to be in the CTEST_BINARY_DIR.
# in the KDE build system, this is the same as CMAKE_BINARY_DIR.
configure_file(${CMAKE_SOURCE_DIR}/CTestCustom.cmake ${CMAKE_BINARY_DIR}/CTestCustom.cmake)

install(FILES org.kde.kdevelop.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})

# Make it possible to use the po files fetched by the fetch-translations step
ki18n_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
