#!/bin/sh

set -e
set -u
set -x

script_path="$(cd "$(dirname "${0}")" && pwd)"
readonly script_path
readonly rackctl_device=test-device

device_ip="$(rackctl-config get "${rackctl_device}" ip)"

readonly device_ip
readonly ssh_target="Administrator@${device_ip}"

eval "$(cleanup init)"

# ATTENTION: We can't use /tmp here. NFS root mount would complain about
# missing permissions, so we put our tmpdir in the working directory.
tmpdir="$(pwd)/tmpdir"
"${CLEANUP}/add" "rm -rf '${tmpdir}'"

# Fake directory structure for rackctl-netboot
readonly rootfs="${tmpdir}/nfs/rootfs"
mkdir -p "${rootfs}"

sudo tar \
	--directory="${rootfs}" \
	--extract \
	--file="image.tar.zst" \
	--zstd

# Disable SSH host key checking.
. 03_init_ssh.sh

# Netboot an upstream Debian kernel on a device with Beckhoff BIOS
	cat > "${rootfs}/etc/resolv.conf" <<- EOF
		domain beckhoff.com
		search beckhoff.com
		nameserver $(rackctl-config get "${rackctl_device}" rackcontroller/ip)
	EOF

	# We need noninteractive sudo for modules install and to access /dev/ccat*
	printf 'ALL\tALL = (ALL) NOPASSWD: ALL\n' >> "${rootfs}/etc/sudoers"

	rackctl-netboot \
		--workdir="${tmpdir}" \
		"${rackctl_device}" local &
        "${CLEANUP}/add" "neokill $!"
        wait_ssh "${ssh_target}"

./test_stage/test_stage.sh "$@" --config-dir=./.test-stage --target-os=tclur rackctl "${rackctl_device}"
