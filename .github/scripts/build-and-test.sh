#!/bin/sh
# Configure, build and test NSPR out of tree, as CI does on every platform.
#
# Extra configure arguments are taken as positional arguments. Callers may set
# CC, CFLAGS, CXXFLAGS and LDFLAGS, and:
#   MAKE             GNU make; the BSDs and Solaris ship theirs as gmake
#   NSPR_COVERAGE    1 to instrument for coverage
#   NSPR_32BIT       1 to build 32-bit, omitting --enable-64bit
#   NSPR_SKIP_TESTS  1 to build without building or running the tests

set -e

: "${MAKE:=make}"

if [ -z "${MAKEFLAGS:-}" ]; then
    ncpu=${NUMBER_OF_PROCESSORS:-}
    [ -n "$ncpu" ] || ncpu=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
    [ -n "$ncpu" ] || ncpu=$(sysctl -n hw.ncpu 2>/dev/null || true)
    MAKEFLAGS="-j${ncpu:-1}"
fi
export MAKEFLAGS
# Share with any later steps of the same CI job.
[ -z "${GITHUB_ENV:-}" ] || echo "MAKEFLAGS=$MAKEFLAGS" >>"$GITHUB_ENV"

if [ "${NSPR_COVERAGE:-}" = 1 ]; then
    coverage="-O0 -g --coverage -fprofile-update=atomic"
    CFLAGS="$coverage${CFLAGS:+ $CFLAGS}"
    CXXFLAGS="$coverage${CXXFLAGS:+ $CXXFLAGS}"
    LDFLAGS="--coverage${LDFLAGS:+ $LDFLAGS}"
    export CFLAGS CXXFLAGS LDFLAGS
    set -- --enable-debug "$@"
    test_make_args="XCFLAGS=--coverage"
fi

[ "${NSPR_32BIT:-}" = 1 ] || set -- --enable-64bit "$@"

# Configure by absolute path, so VPATH and gcov source paths are absolute.
unset CDPATH
srcdir=$(cd -- "$(dirname -- "$0")/../.." && pwd)

mkdir target
cd target
"$srcdir/configure" "$@" --prefix="$PWD/dist"
"$MAKE"

if [ "${NSPR_SKIP_TESTS:-}" != 1 ]; then
    # Word splitting of $test_make_args is intended.
    # shellcheck disable=SC2086
    "$MAKE" -C pr/tests ${test_make_args:-}
    cd pr/tests
    "$srcdir/pr/tests/runtests.sh" ../../dist
fi
