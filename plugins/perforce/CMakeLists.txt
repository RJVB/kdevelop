# temporary while patch series in work 08/2018
remove_definitions(
    -DQT_NO_CAST_FROM_ASCII
    -DQT_NO_CAST_TO_ASCII
    -DQT_NO_CAST_FROM_BYTEARRAY
)

add_subdirectory(p4clientstub)
ecm_qt_declare_logging_category(kdevperforce_LOG_PART_SRCS
    HEADER debug.h
    IDENTIFIER PLUGIN_PERFORCE
    CATEGORY_NAME "kdevelop.plugins.perforce"
)
set(kdevperforce_UIS
    ui/perforceimportmetadatawidget.ui
)
set(kdevperforce_PART_SRCS
    ui/perforceimportmetadatawidget.cpp
    perforceplugin.cpp
    perforcepluginmetadata.cpp
    ${kdevperforce_LOG_PART_SRCS}
)
ki18n_wrap_ui(kdevperforce_PART_SRCS ${kdevperforce_UIS})
kdevplatform_add_plugin(kdevperforce JSON kdevperforce.json SOURCES ${kdevperforce_PART_SRCS})

target_link_libraries(kdevperforce
   KDev::Interfaces
   KDev::Vcs
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
