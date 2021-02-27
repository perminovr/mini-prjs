echo making x86_64

SYSROOTS_N=""
SYSROOTS_D=""
TOOLCHAIN=""
TOOLCHAIN_PATH=""
TOOLCHAIN_CMN=""
TOOLCHAIN_INC=""
TOOLCHAIN_LIB=""

COMPILER_FLAGS=""

export PATH="$PATH:"

export includedir="${SYSROOTS_D}/usr/include"
export SYSROOT="${SYSROOTS_D}/usr/include"
export exec_prefix="/usr"

export CC="${TOOLCHAIN}gcc  ${COMPILER_FLAGS}"
export CXX="${TOOLCHAIN}g++  ${COMPILER_FLAGS}"
export CPP="${TOOLCHAIN}gcc -E ${COMPILER_FLAGS}"
export CCLD="${TOOLCHAIN}gcc  ${COMPILER_FLAGS}"

export AS="${TOOLCHAIN}as"
export AR="${TOOLCHAIN}ar"
export NM="${TOOLCHAIN}nm"
export STRIP="${TOOLCHAIN}strip"
export RANLIB="${TOOLCHAIN}ranlib"
export OBJCOPY="${TOOLCHAIN}objcopy"
export OBJDUMP="${TOOLCHAIN}objdump"
export LD="${TOOLCHAIN}ld"

export CPPFLAGS="-fstack-protector -fmessage-length=0 -D_FORTIFY_SOURCE=1 -pipe"
export CFLAGS="${CPPFLAGS}"
export CXXFLAGS="${CFLAGS}"
export EXTRA_CPPFLAGS="${CPPFLAGS}"
export EXTRA_CFLAGS="${CFLAGS}"
export EXTRA_CXXFLAGS="${CXXFLAGS}"
export TARGET_CPPFLAGS="${CPPFLAGS}"
export TARGET_CFLAGS="${CFLAGS}"
export TARGET_CXXFLAGS="${CXXFLAGS}"

LDFLAGS="-Wl,--hash-style=gnu"
[ "${TOOLCHAIN_LIB}" != "" ] && LDFLAGS="-L${TOOLCHAIN_LIB} ${LDFLAGS}"
export LDFLAGS="${LDFLAGS}"
export TARGET_LDFLAGS="${LDFLAGS}"
export EXTRA_LDFLAGS="${LDFLAGS}"

export PKG_CONFIG_DISABLE_UNINSTALLED="yes"
export PKG_CONFIG_SYSROOT_DIR="${SYSROOTS_D}"
export PKG_CONFIG_DIR="${SYSROOTS_D}/usr/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="${SYSROOTS_D}/usr/lib/pkgconfig"
export PKG_CONFIG_PATH="${SYSROOTS_D}/usr/lib/pkgconfig:${SYSROOTS_D}/usr/share/pkgconfig"
