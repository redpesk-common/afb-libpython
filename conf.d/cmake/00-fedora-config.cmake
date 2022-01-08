message(STATUS "Custom options: 00-fedora-config.cmake --")

# Libshell is not part of standard Linux Distro (https://libshell.org/)
set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib64/pkgconfig")

# Remeber sharelib path to simplify test & debug
set(BINDINGS_LINK_FLAG "-Xlinker -rpath=/usr/local/lib64")
