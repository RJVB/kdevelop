# KDevelop

This repository contains the source code for the KDevelop IDE.

The idea that this repository contains
- Every plugin related related to C/C++ development
- Every plugin only specific for KDevelop (e.g. the Welcome Page plugin)

## User Documentation

User documentation is available from:
https://userbase.kde.org/KDevelop4/Manual

## =================
## RJVB's branch(es)

These are branches of the current release branch (starting with the 5.2 branch).
They contain patches aiming to improve usability (and sometimes stability) that are either in the process
of being upstreamed or have been rejected as unworthy of interest by (for?) the core developers.

There are also a number of patches which are purely related to packaging for MacPorts and that will
therefore never be presented upstream even though they should not interfere with regular builds

The list of modifications fluctuates too much to enumerate here; please check the patch repository
at https://github.com/RJVB/macstrop/tree/master/kf5/kf5-kdevelop/files and the build script at
https://github.com/RJVB/macstrop/blob/master/kf5/kf5-kdevelop/Portfile to see which are applied at
any given time.

Probably the main feature differences (at the time of writing) are:
- faster project import which doesn't block the UI (on platforms other than Linux or e.g. when loading projects on NFS shares)
- less UI blocking during the search for solutions in the code editor
- The "Show Differences" feature can generate patches with more or less context lines (Control/long-click the
  Update button); useful for submitting the patch via the integrated Phabricator support
- support for building the documentation viewer without QtWebKit or QtWebEngine
- support for using Qt's Assistant as an external viewer for the integrated the QtHelp feature.
  (see also https://github.com/RJVB/macstrop/blob/master/kf5/kf5-kdevelop/files/kdevelop-qthelp-viewer)
- support for building only or all but the clang-based C/C++ code parser plugin; useful for packaging, if
  you want to upgrade clang without rebuilding all of KDevelop, or if you never plan any C/C++ development.

*IMPORTANT:* any issues that arise in code specific to this fork should be reported here.
## =================

## Compile

KDevelop is built the same way as most KDE projects, using CMake to set up a build directory and build options.
For detailed instructions how to compile KDevelop, please refer to the Wiki:
https://community.kde.org/KDevelop/HowToCompile_v5 .

### Optional dependencies

Most of KDevelop's optional dependencies are opportunistic under the assumption that the build should
always use all available functionality, as well as the most recent version. CMake has a lesser known feature
to control which of such dependencies is used.

For instance, to skip building the Subversion plugin, use
`-DCMAKE_DISABLE_FIND_PACKAGE_SubversionLibrary=ON`. To use QtWebKit instead of QtWebEngine for
documentation rendering when both are available, add `-DCMAKE_DISABLE_FIND_PACKAGE_Qt5WebEngineWidgets=ON` to
the CMake arguments.

## Contribute

If you want to contribute to KDevelop, please read through:
https://www.kdevelop.org/contribute-kdevelop

## Development Infrastructure
- [Bug tracker](https://bugs.kde.org/buglist.cgi?bug_status=UNCONFIRMED&bug_status=CONFIRMED&bug_status=ASSIGNED&bug_status=REOPENED&list_id=1408918&product=kdevelop&query_format=advanced)
- [Phabricator (task tracker, code review and more)](https://phabricator.kde.org/dashboard/view/8/?)
