{
  pkgs,
  perSystem,
  flake,
  ...
}:

let
  kernelPackages = pkgs.linuxKernel.packages.linux_6_12;
  kernel = kernelPackages.kernel;
  fsModule = perSystem.self.filesystem;
  fsTestProgram = perSystem.self.fs-test;
  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    testScript = "echo '[init] Interactive shell'; exec /bin/sh";
  };
in

pkgs.writeShellScriptBin "runvm" ''
  set -euo pipefail

  # Args before -- are passed to the filesystem module, after -- to QEMU
  MYFS_ARGS=""
  QEMU_ARGS=""
  found_sep=false
  for arg in "$@"; do
    if [ "$found_sep" = true ]; then
      QEMU_ARGS="$QEMU_ARGS $arg"
    elif [ "$arg" = "--" ]; then
      found_sep=true
    else
      MYFS_ARGS="$MYFS_ARGS myfs.$arg"
    fi
  done

  DISK=$(mktemp /tmp/kernel-dev-disk-XXXXXX.img)
  trap "rm -f $DISK" EXIT
  truncate -s 100M "$DISK"

  # Force-kill anything on the GDB stub port
  ${pkgs.psmisc}/bin/fuser -k 1234/tcp 2>/dev/null || true
  sleep 0.2

  exec ${pkgs.qemu}/bin/qemu-system-x86_64 \
    -enable-kvm \
    -m 1G \
    -smp 2 \
    -cpu host \
    -kernel ${kernel}/bzImage \
    -initrd ${testInitramfs}/initrd.gz \
    -drive file="$DISK",format=raw,if=virtio,media=disk \
    -append "console=ttyS0 nokaslr loglevel=7$MYFS_ARGS" \
    -nographic \
    -s \
    $QEMU_ARGS
''
