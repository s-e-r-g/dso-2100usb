# Build environment: native Linux build, Windows cross build (MinGW-w64), format and tidy.
FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Qt for both targets from the Qt archive (aqtinstall). The Windows build needs the Linux Qt
# of the same version for its host tools (moc, rcc). MinGW 13 matches Qt's own MinGW 13.1.
ARG QT_VERSION=6.8.3
ARG AQT_VERSION=3.2.1
ARG LIBUSB_VERSION=1.0.27
ARG CLANG_FORMAT_VERSION=23.1.0
ARG CLANG_TIDY_VERSION=23.1.4

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config gdb \
        libusb-1.0-0-dev usbutils libgtest-dev \
        g++-mingw-w64-x86-64-posix p7zip-full zip \
        libgl1-mesa-dev libegl1 libxkbcommon0 libxkbcommon-x11-0 libfontconfig1 libdbus-1-3 \
        libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
        libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 fonts-dejavu-core \
        python3 pipx \
    && rm -rf /var/lib/apt/lists/*

RUN PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin pipx install aqtinstall==$AQT_VERSION \
    && aqt install-qt --outputdir /opt/qt linux desktop $QT_VERSION linux_gcc_64 \
    && aqt install-qt --outputdir /opt/qt windows desktop $QT_VERSION win64_mingw \
    && rm -rf /root/.local/share/aqt aqtinstall.log

# libusb for Windows: the official release (static MinGW library and header)
RUN set -eu; \
    f=libusb-$LIBUSB_VERSION.7z; \
    curl -fsSL -o /tmp/$f https://github.com/libusb/libusb/releases/download/v$LIBUSB_VERSION/$f; \
    mkdir -p /opt/libusb-win; \
    7z x -o/opt/libusb-win /tmp/$f > /dev/null; \
    rm /tmp/$f

# clang-format / clang-tidy, pinned: other versions format and check differently
RUN PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin pipx install clang-format==$CLANG_FORMAT_VERSION \
    && PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin pipx install clang-tidy==$CLANG_TIDY_VERSION

ENV QT_LINUX=/opt/qt/$QT_VERSION/gcc_64 \
    QT_WINDOWS=/opt/qt/$QT_VERSION/mingw_64 \
    LIBUSB_WINDOWS=/opt/libusb-win
