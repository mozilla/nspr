#!/bin/sh
# Configure, build and test NSPR out of tree, as CI does on every platform.
#
# Extra configure arguments are taken as positional arguments. Callers may set
# CC, CFLAGS, CXXFLAGS and LDFLAGS, and:
#   MAKE             GNU make; the BSDs and Solaris ship theirs as gmake
#   NSPR_COVERAGE    1 to instrument for gcov, msvc for MSVC's collector
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
elif [ "${NSPR_COVERAGE:-}" = msvc ]; then
    DLLFLAGS="-PROFILE${DLLFLAGS:+ $DLLFLAGS}"
    export DLLFLAGS
fi

[ "${NSPR_32BIT:-}" = 1 ] || set -- --enable-64bit "$@"

# Configure by absolute path where possible, so VPATH and gcov source paths are absolute.
unset CDPATH
topdir=$(cd -- "$(dirname -- "$0")/../.." && pwd)
srcdir=$topdir
# Native Windows make splits VPATH at the drive letter's colon.
case $("$MAKE" --version 2>/dev/null) in
*Windows32* | *mingw32*)
    if [ "$topdir" != "$PWD" ]; then
        echo "Run $0 from $topdir with this make" >&2
        exit 1
    fi
    srcdir=..
    ;;
esac

mkdir target
cd target
"$srcdir/configure" "$@" --prefix="$PWD/dist"
"$MAKE"

if [ "${NSPR_SKIP_TESTS:-}" != 1 ]; then
    # Word splitting of $test_make_args is intended.
    # shellcheck disable=SC2086
    "$MAKE" -C pr/tests ${test_make_args:-}
    cd pr/tests
    if [ "${NSPR_COVERAGE:-}" = msvc ]; then
        tool="$(cygpath -u "$VSINSTALLDIR")/Common7/IDE/Extensions/Microsoft/CodeCoverage.Console/Microsoft.CodeCoverage.Console.exe"
        settings=$(cygpath -w "$topdir/target/coverage.runsettings")
        cat >"$topdir/target/coverage.runsettings" <<'EOF'
<Configuration>
  <CodeCoverage>
    <ModulePaths>
      <Include>
        <ModulePath>.*(?:nspr|plc|plds)4[.]dll$</ModulePath>
      </Include>
    </ModulePaths>
    <EnableDynamicNativeInstrumentation>False</EnableDynamicNativeInstrumentation>
    <EnableStaticNativeInstrumentation>True</EnableStaticNativeInstrumentation>
  </CodeCoverage>
</Configuration>
EOF
        for lib in nspr4 plc4 plds4; do
            "$tool" instrument --settings "$settings" "$(cygpath -w "$topdir/target/dist/lib/$lib.dll")"
        done
        PATH="$topdir/target/dist/lib:$PATH" # The collector's children need it.
        "$tool" collect --settings "$settings" \
            --output "$(cygpath -w "$topdir/coverage.cobertura.xml")" \
            --output-format cobertura \
            "$(cygpath -w "$(command -v bash)")" "$topdir/pr/tests/runtests.sh" ../../dist
    else
        "$topdir/pr/tests/runtests.sh" ../../dist
    fi
fi
