cmake_minimum_required(VERSION 3.0)
project(KDevelop VERSION 5.2.1)

# KDevelop SOVERSION
# E.g. for KDevelop 5.2.0 => SOVERSION 52 (we only promise ABI compatibility between patch version updates)
set(KDEVELOP_SOVERSION 52)

# plugin versions listed in the .desktop files
set(KDEV_PLUGIN_VERSION 30)

if(APPLE OR WIN32)
    option(KDEVELOP_NATIVE_DIALOGS "Use native file dialogs instead of KDE dialogs" ON)
else()
    option(KDEVELOP_NATIVE_DIALOGS "Use native file dialogs instead of KDE dialogs" OFF)
endif()

option(KDEVELOP_BUILD_IDE "Build the IDE" ON)
option(KDEVELOP_BUILD_CLANG_PARSER "Build the Clang-based C/C++ parser (formerly kdev-clang)" ON)

# we need some parts of the ECM CMake helpers
find_package (ECM 5.14.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${KDevelop_SOURCE_DIR}/cmake/modules ${ECM_MODULE_PATH})

if (NOT KDEVELOP_BUILD_IDE)
    if (NOT KDEVELOP_BUILD_CLANG_PARSER)
        message(FATAL_ERROR "KDEVELOP_BUILD_IDE and KDEVELOP_BUILD_CLANG_PARSER cannot both be OFF!")
    else()
        message(WARNING "Invoke make in ${CMAKE_CURRENT_BINARY_DIR}/plugins/clang in order to build only kdev-clang!")
    endif()
endif()

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

include(KDEInstallDirs)
include(KDECMakeSettings)

if(POLICY CMP0071)
  # CMake 3.10 generates warnings when projects combine AUTOMOC with qt5_wrap_ui() or qt5_add_resources()
  # Avoid that by setting this policy (cf. https://bugreports.qt.io/browse/QTBUG-63442)
  # Note: Once we depend on a Qt which has this fix (likely Qt 5.9.4+), remove this
  cmake_policy(SET CMP0071 OLD)
endif()

include(GetGitRevisionDescription)

set(QT_MIN_VERSION "5.5.0")
find_package(Qt5 ${QT_MIN_VERSION} CONFIG REQUIRED Widgets Concurrent Quick QuickWidgets)
if(BUILD_TESTING)
  find_package(Qt5Test ${QT_MIN_VERSION} CONFIG REQUIRED)
endif()
set(KF5_DEP_VERSION "5.15.0") # we need KCrash::initialize
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
find_package(KF5SysGuard CONFIG)
set_package_properties(KF5SysGuard PROPERTIES
    PURPOSE "Framework for process listing. Required for the 'Attach to Process' feature"
    TYPE RECOMMENDED
)
find_package(KDevelop-PG-Qt 1.90.90 CONFIG)
set_package_properties(KDevelop-PG-Qt PROPERTIES
    PURPOSE "KDevelop parser generator library. Required for the QMake Builder/Manager plugin."
    TYPE RECOMMENDED
)

find_package(SharedMimeInfo REQUIRED)

if(NOT CMAKE_VERSION VERSION_LESS "3.10.0" AND KF5_VERSION VERSION_LESS "5.42.0")
  # CMake 3.9+ warns about automoc on files without Q_OBJECT, and doesn't know about other macros.
  # 3.10+ lets us provide more macro names that require automoc.
  # KF5 >= 5.42 takes care itself of adding its macros in its cmake config files
  list(APPEND CMAKE_AUTOMOC_MACRO_NAMES
       "K_PLUGIN_FACTORY_WITH_JSON" "K_EXPORT_PLASMA_DATAENGINE_WITH_JSON" "K_EXPORT_PLASMA_RUNNER")
endif()
if(NOT CMAKE_VERSION VERSION_LESS "3.9.0" AND KF5_VERSION VERSION_LESS "5.44.0")
    # CMake's automoc needs help to find names of plugin metadata files in case Q_PLUGIN_METADATA
    # is indirectly used via other C++ preprocessor macros
    # 3.9+ lets us provide some filter rule pairs (keyword, regexp) to match the names of such files
    # in the plain text of the sources. See AUTOMOC_DEPEND_FILTERS docs for details.
    list(APPEND CMAKE_AUTOMOC_DEPEND_FILTERS
        "K_PLUGIN_FACTORY_WITH_JSON"
        "[\n^][ \t]*K_PLUGIN_FACTORY_WITH_JSON[ \t\n]*\\([^,]*,[ \t\n]*\"([^\"]+)\""
        "K_EXPORT_PLASMA_DATAENGINE_WITH_JSON"
        "[\n^][ \t]*K_EXPORT_PLASMA_DATAENGINE_WITH_JSON[ \t\n]*\\([^,]*,[^,]*,[ \t\n]*\"([^\"]+)\""
    )
endif()


add_definitions(
    -DQT_DEPRECATED_WARNINGS
    -DQT_DISABLE_DEPRECATED_BEFORE=0x050500
    -DQT_NO_SIGNALS_SLOTS_KEYWORDS
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_STRICT_ITERATORS
    -DQT_USE_QSTRINGBUILDER
)

if(KDEVELOP_NATIVE_DIALOGS)
    add_definitions(-DKDEV_USE_NATIVE_DIALOGS)
endif()

function(add_compile_flag_if_supported _flag)
    unset(_have_flag CACHE)
    string(REGEX REPLACE "[-=]" "_" _varname ${_flag})
    string(TOUPPER ${_varname} _varname)
    set(_varname "HAVE${_varname}")
    check_cxx_compiler_flag("${_flag}" "${_varname}")
    if (${${_varname}})
        add_compile_options(${_flag})
    endif()
endfunction()

# Turn off missing-field-initializers warning for GCC to avoid noise from false positives with empty {}
# See discussion: http://mail.kde.org/pipermail/kdevelop-devel/2014-February/046910.html
add_compile_flag_if_supported(-Wno-missing-field-initializers)
add_compile_flag_if_supported(-Werror=undefined-bool-conversion)
add_compile_flag_if_supported(-Werror=tautological-undefined-compare)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_flag_if_supported(-Wdocumentation)
    # This warning is triggered by every call to qCDebug()
    add_compile_flag_if_supported(-Wno-gnu-zero-variadic-macro-arguments)
endif()
if (CMAKE_COMPILER_CXX_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_flag_if_supported(-pedantic)
endif()

include_directories(${KDevelop_SOURCE_DIR} ${KDevelop_BINARY_DIR})

string(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_TOLOWER)
if(CMAKE_BUILD_TYPE_TOLOWER MATCHES "debug"
  OR CMAKE_BUILD_TYPE_TOLOWER STREQUAL "")
  set(COMPILER_OPTIMIZATIONS_DISABLED TRUE)
else()
  set(COMPILER_OPTIMIZATIONS_DISABLED FALSE)
endif()

# create config-kdevelop.h
configure_file(config-kdevelop.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kdevelop.h)

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

add_subdirectory(kdevplatform)
add_subdirectory(plugins)

add_subdirectory(pics)
add_subdirectory(app)
add_subdirectory(app_templates)
add_subdirectory(file_templates)
add_subdirectory(shortcuts)
add_subdirectory(doc)

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
install(FILES kdevelop.categories DESTINATION ${KDE_INSTALL_CONFDIR})

# CTestCustom.cmake has to be in the CTEST_BINARY_DIR.
# in the KDE build system, this is the same as CMAKE_BINARY_DIR.
configure_file(${CMAKE_SOURCE_DIR}/CTestCustom.cmake ${CMAKE_BINARY_DIR}/CTestCustom.cmake)

install(FILES org.kde.kdevelop.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR})

# Make it possible to use the po files fetched by the fetch-translations step
ki18n_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
