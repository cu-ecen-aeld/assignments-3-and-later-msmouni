#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
PATH=$PATH:/home/marwan/Documents/Learning/Coursera/Embedded_Linux/arm-cross-compiler/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Note: "'multiple definition of yylloc'" (https://github.com/BPI-SINOVOIP/BPI-M4-bsp/issues/4)
    # Check if the file "${OUTDIR}/linux-stable/scripts/dtc/dtc-lexer.l" already contains "extern YYLTYPE yylloc;"
    if ! grep -q "extern YYLTYPE yylloc;" "${OUTDIR}/linux-stable/scripts/dtc/dtc-lexer.l"; then
        # If not, replace "YYLTYPE yylloc;" with "extern YYLTYPE yylloc;"
        sed -i 's/YYLTYPE yylloc;/extern YYLTYPE yylloc;/g' "${OUTDIR}/linux-stable/scripts/dtc/dtc-lexer.l"
    fi

    # "deep clean" the kernel build tree: removing the .config file with any existing configurations
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper

    # configure for our "virt" art dev board we will simulate in QEMU
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig

    # Build a kernel image for booting with QEMU (j4: run on several cores)
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all

    # # Build any kernel modules
    # make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules

    # Build any kernel devicetree
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

#############################################################################################

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}

else
    cd busybox
fi

#############################################################################################
# TODO: Make and install busybox
make ARCH=${ARCH} CONFIG_PREFIX="${OUTDIR}/rootfs" CROSS_COMPILE=${CROSS_COMPILE} install

cd "${OUTDIR}/rootfs"

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

#############################################################################################
# TODO: Add library dependencies to rootfs
echo "Adding library dependencies to rootfs"

CROSS_COMPILE_SYS_ROOT=$(${CROSS_COMPILE}gcc --print-sysroot)

# program interpreter: /lib/ld-linux-aarch64.so.1
cp "${CROSS_COMPILE_SYS_ROOT}/lib64/ld-2.33.so" "${OUTDIR}/rootfs/lib64/"
cp -a "${CROSS_COMPILE_SYS_ROOT}/lib/ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib/"

# Shared library: [libm.so.6]: libm.so.6 -> libm-2.33.so
cp "${CROSS_COMPILE_SYS_ROOT}/lib64/libm.so.6" "${OUTDIR}/rootfs/lib64/"
cp -a "${CROSS_COMPILE_SYS_ROOT}/lib64/libm-2.33.so" "${OUTDIR}/rootfs/lib64/"

# Shared library: [libresolv.so.2]: libresolv.so.2 -> libresolv-2.33.so
cp "${CROSS_COMPILE_SYS_ROOT}/lib64/libresolv.so.2" "${OUTDIR}/rootfs/lib64/"
cp -a "${CROSS_COMPILE_SYS_ROOT}/lib64/libresolv-2.33.so" "${OUTDIR}/rootfs/lib64/"

# Shared library: [libc.so.6]: libc.so.6 -> libc-2.33.so
cp "${CROSS_COMPILE_SYS_ROOT}/lib64/libc.so.6" "${OUTDIR}/rootfs/lib64/"
cp -a "${CROSS_COMPILE_SYS_ROOT}/lib64/libc-2.33.so" "${OUTDIR}/rootfs/lib64/"

#############################################################################################

# TODO: Make device nodes
echo "Making device nodes"
# Null device is known major 1 minor 3( 666: access permissions for the node | c: character type)
sudo mknod -m 666 dev/null c 1 3
# Console device is known major 5 minor 1
sudo mknod -m 600 dev/console c 5 1

#############################################################################################

# TODO: Clean and build the writer utility
echo "Cleaning and building the writer utility at $FINDER_APP_DIR"
cd $FINDER_APP_DIR
make CROSS_COMPILE=${CROSS_COMPILE} clean
make CROSS_COMPILE=${CROSS_COMPILE} all

#############################################################################################

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copying the finder related scripts and executables to the /home directory"
cp writer ${OUTDIR}/rootfs/home
cp finder.sh ${OUTDIR}/rootfs/home

mkdir ${OUTDIR}/rootfs/home/conf
cp conf/username.txt ${OUTDIR}/rootfs/home/conf/username.txt
cp conf/assignment.txt ${OUTDIR}/rootfs/home/conf/assignment.txt

cp finder-test.sh ${OUTDIR}/rootfs/home

# Replacing by
# sed (stream editor): -i : Edit the file in place. | s/old_string/new_string/ : Substitute old_string with new_string. | g : Global replacement (replace all occurrences).
sed -i 's/..\/conf\/assignment.txt/conf\/assignment.txt/g' "${OUTDIR}/rootfs/home/finder-test.sh"

cp autorun-qemu.sh ${OUTDIR}/rootfs/home
#############################################################################################

# TODO: Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs"

#############################################################################################

# TODO: Create initramfs.cpio.gz
# An initramfs is an initial ram file system based on tmpfs (a size-flexible, in-memory lightweight file system)
# This cpio utility is just going to essentially bundle the content of our root file system in a format that's understood by qemu that we can pass to the qemu as a Run command.
# this is just saying find everything in my root file system that I've set up as using the previous instructions. Then create a cpio file using an owner of root:root
echo "Creating initramfs.cpio.gz"
cd "${OUTDIR}/rootfs"

find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio

gzip -f ${OUTDIR}/initramfs.cpio

echo "Done"