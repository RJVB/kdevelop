include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

configure_file(testqthelpconfig.h.in testqthelpconfig.h)

set(test_qthelpplugin_SRCS
    test_qthelpplugin.cpp
    ../qthelpplugin.cpp
    ../qthelpproviderabstract.cpp
    ../qthelpprovider.cpp
    ../qthelpdocumentation.cpp
    ../qthelpexternalassistant.cpp
    ../qthelpqtdoc.cpp
    ../qthelp_config_shared.cpp
    ../qthelpconfig.cpp
    ../qthelpnetwork.cpp
    ${kdevqthelp_LOG_SRCS}
)
if(APPLE)
    set(test_qthelpplugin_SRCS ${test_qthelpplugin_SRCS}
        ../qthelpexternalassistant_mac.mm
    )
endif()
ki18n_wrap_ui(test_qthelpplugin_SRCS
   ../qthelpconfig.ui
   ../qthelpconfigeditdialog.ui
)


ecm_add_test(${test_qthelpplugin_SRCS}
    TEST_NAME test_qthelpplugin
    LINK_LIBRARIES Qt5::Test KF5::NewStuff KF5::KIOWidgets KF5::TextEditor KF5::IconThemes Qt5::Help KDev::Tests KDev::Documentation
)
if(APPLE)
    target_link_libraries(test_qthelpplugin "-framework AppKit")
endif()
