#!/bin/sh
# Checks and upgrades the pinned third-party versions. They live as ARGs in
# .devcontainer/Dockerfile (the one place CI and developers take them from):
#
#   sh tools/thirdparty.sh check                  pinned and newest version of each part
#   sh tools/thirdparty.sh upgrade [name [ver]]   write the newest (or the given) version
#
# Parts: qt, aqt, libusb, clang-format, clang-tidy. An upgrade only edits the Dockerfile; then
# rebuild the container, run make && make test && make windows, check the app on the scope,
# and commit ("build(deps): ..."). Qt: stays on the major version pinned now; a new minor may
# need a newer MinGW, check the Qt release notes. Needs curl, sed, awk.

cd "$(dirname "$0")/.." || exit 1

DOCKERFILE=.devcontainer/Dockerfile

fail() {
    echo "error: $*" >&2
    exit 1
}

# part -> Dockerfile ARG
Arg() {
    case "$1" in
    qt)           echo QT_VERSION ;;
    aqt)          echo AQT_VERSION ;;
    libusb)       echo LIBUSB_VERSION ;;
    clang-format) echo CLANG_FORMAT_VERSION ;;
    clang-tidy)   echo CLANG_TIDY_VERSION ;;
    *)            fail "unknown part '$1' (qt, aqt, libusb, clang-format, clang-tidy)" ;;
    esac
}

Pinned() {
    sed -n "s/^ARG $(Arg "$1")=\(.*\)/\1/p" "$DOCKERFILE"
}

# newest version number from a list on stdin (one per line)
Newest() {
    sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1
}

Latest() {
    case "$1" in
    qt)
        major=$(Pinned qt | cut -d. -f1)
        base=https://download.qt.io/official_releases/qt
        minor=$(curl -fsSL "$base/" | grep -o "href=\"$major\.[0-9]*/" | sed 's/href="//; s/\///' | Newest)
        curl -fsSL "$base/$minor/" | grep -o "href=\"$minor\.[0-9]*/" | sed 's/href="//; s/\///' | Newest
        ;;
    aqt)          curl -fsSL https://pypi.org/pypi/aqtinstall/json | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' | tail -n 1 ;;
    clang-format) curl -fsSL https://pypi.org/pypi/clang-format/json | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' | tail -n 1 ;;
    clang-tidy)   curl -fsSL https://pypi.org/pypi/clang-tidy/json | sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' | tail -n 1 ;;
    libusb)
        curl -fsSL https://api.github.com/repos/libusb/libusb/releases/latest |
            sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p' | head -n 1
        ;;
    esac
}

Parts="qt aqt libusb clang-format clang-tidy"

case "$1" in
check)
    printf '%-14s %-12s %-12s\n' part pinned newest
    for p in $Parts; do
        pinned=$(Pinned "$p")
        latest=$(Latest "$p")
        [ -n "$latest" ] || latest="?"
        mark=""
        [ "$latest" != "?" ] && [ "$latest" != "$pinned" ] && mark="  <- upgrade available"
        printf '%-14s %-12s %-12s%s\n' "$p" "$pinned" "$latest" "$mark"
    done
    ;;
upgrade)
    if [ -n "$2" ]; then
        list=$2
    else
        list=$Parts
    fi
    for p in $list; do
        to=${3:-$(Latest "$p")}
        [ -n "$to" ] || fail "cannot find the newest version of $p"
        from=$(Pinned "$p")
        if [ "$from" = "$to" ]; then
            echo "$p: $from is current"
            continue
        fi
        sed -i "s/^ARG $(Arg "$p")=.*/ARG $(Arg "$p")=$to/" "$DOCKERFILE"
        echo "$p: $from -> $to"
    done
    echo "Now: rebuild the devcontainer, make && make test && make windows, try it on the scope, commit."
    ;;
*)
    sed -n '2,11s/^# \{0,1\}//p' "$0"
    exit 1
    ;;
esac
