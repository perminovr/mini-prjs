echo making arm

SYSROOTS_N="/home/hcs/tisdk1/build/arago-tmp-external-linaro-toolchain/sysroots/i686-linux"
SYSROOTS_D="/home/hcs/ti1/ps-04.02.00.09/build/arago-tmp-external-linaro-toolchain/sysroots/am335x-esom"
TOOLCHAIN="arm-linux-gnueabihf-"
TOOLCHAIN_PATH="/opt/gcc-linaro-6.2.1-2016.11-x86_64_arm-linux-gnueabihf"
TOOLCHAIN_CMN="${TOOLCHAIN_PATH}/arm-linux-gnueabihf"
TOOLCHAIN_INC="${TOOLCHAIN_CMN}/include"
TOOLCHAIN_LIB="${TOOLCHAIN_CMN}/lib"

COMPILER_FLAGS="-march=armv7-a -marm -mthumb-interwork -mfloat-abi=hard -mfpu=neon -mtune=cortex-a8 --sysroot=${SYSROOTS_D}"

export PATH="${SYSROOTS_N}/usr/bin/armv7ahf-vfp-neon-oe-linux-gnueabi:${SYSROOTS_N}/usr/sbin:${SYSROOTS_N}/sbin:${SYSROOTS_N}/bin:${TOOLCHAIN}/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games"

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
export LD="${TOOLCHAIN}ld --sysroot=${SYSROOTS_D}"

export CPPFLAGS="-isystem${TOOLCHAIN_INC} -fstack-protector -D_FORTIFY_SOURCE=1"
export CFLAGS="${CPPFLAGS} -O2 -pipe -g -feliminate-unused-debug-types"
export EXTRA_CFLAGS="${CFLAGS}"
export TARGET_CPPFLAGS="${CPPFLAGS}"
export TARGET_CFLAGS="${CFLAGS}"
export CXXFLAGS="${CFLAGS} -fpermissive"
export TARGET_CXXFLAGS="${CFLAGS} -fpermissive"

export LDFLAGS="-L${TOOLCHAIN_LIB} -Wl,-rpath-link,${TOOLCHAIN_LIB} -Wl,-O1 -Wl,--hash-style=gnu"
export TARGET_LDFLAGS="${LDFLAGS}"
export EXTRA_LDFLAGS="${LDFLAGS}"

export PKG_CONFIG_DISABLE_UNINSTALLED="yes"
export PKG_CONFIG_SYSROOT_DIR="${SYSROOTS_D}"
export PKG_CONFIG_DIR="${SYSROOTS_D}/usr/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="${SYSROOTS_D}/usr/lib/pkgconfig"
export PKG_CONFIG_PATH="${SYSROOTS_D}/usr/lib/pkgconfig:${SYSROOTS_D}/usr/share/pkgconfig"
