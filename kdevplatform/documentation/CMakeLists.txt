add_definitions(-DTRANSLATION_DOMAIN=\"kdevplatform\")

# store KDEVELOP_USE_QTEXTBROWSER in the cache because it needs to be accessible
# to plugins/qthelp/CMakeLists.txt too. Reset it to 0 on each run.
set(KDEVELOP_USE_QTEXTBROWSER 0 CACHE INTERNAL "Documentation will be rendered using QTextBrowser")

find_package(Qt5WebEngineWidgets CONFIG)
if(TARGET Qt5::WebEngineWidgets)
    set_package_properties(Qt5WebEngineWidgets PROPERTIES
        PURPOSE "QtWebEngine, for integrated documentation"
        URL "http://qt-project.org/"
        TYPE REQUIRED)
else()
    find_package(Qt5WebKitWidgets CONFIG)
    if(TARGET Qt5::WebKitWidgets)
        set_package_properties(Qt5WebKitWidgets PROPERTIES
            PURPOSE "QtWebKit, for integrated documentation"
            URL "http://qt-project.org/"
            TYPE REQUIRED)
        set(USE_QTWEBKIT 1)
    else()
        set(KDEVELOP_USE_QTEXTBROWSER 1 CACHE INTERNAL "Documentation will be rendered using QTextBrowser")
    endif()
endif()

if(KDEVELOP_USE_QTEXTBROWSER)
    set(KDevPlatformDocumentation_LIB_SRCS
        standarddocumentationview_qtb.cpp)
endif()
set(KDevPlatformDocumentation_LIB_SRCS ${KDevPlatformDocumentation_LIB_SRCS}
    standarddocumentationview.cpp
    documentationfindwidget.cpp
    documentationview.cpp
)

ecm_qt_declare_logging_category(KDevPlatformDocumentation_LIB_SRCS
    HEADER debug.h
    IDENTIFIER DOCUMENTATION
    CATEGORY_NAME "kdevplatform.documentation"
)

ki18n_wrap_ui(KDevPlatformDocumentation_LIB_SRCS documentationfindwidget.ui)
kdevplatform_add_library(KDevPlatformDocumentation SOURCES ${KDevPlatformDocumentation_LIB_SRCS})

target_link_libraries(KDevPlatformDocumentation PUBLIC KDev::Interfaces PRIVATE KDev::Util KDev::Sublime)

if(KDEVELOP_USE_QTEXTBROWSER)
    target_link_libraries(KDevPlatformDocumentation PRIVATE Qt5::Widgets)
    target_compile_definitions(KDevPlatformDocumentation PRIVATE -DUSE_QTEXTBROWSER)
elseif(USE_QTWEBKIT)
    target_link_libraries(KDevPlatformDocumentation PRIVATE Qt5::WebKitWidgets)
    target_compile_definitions(KDevPlatformDocumentation PRIVATE -DUSE_QTWEBKIT)
else()
    target_link_libraries(KDevPlatformDocumentation PRIVATE Qt5::WebEngineWidgets)
endif()

install(FILES
    documentationfindwidget.h
    standarddocumentationview.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kdevplatform/documentation COMPONENT Devel
 )
