{ ... }:

rec {
  mkInitramfs =
    pkgs:
    { kernel, fsModule, fsTestProgram, testScript }:
    let
      busyboxStatic = pkgs.busybox.override { enableStatic = true; };
    in
    pkgs.runCommand "initramfs"
      { nativeBuildInputs = [ pkgs.cpio pkgs.gzip ]; }
      ''
        mkdir -p root/{bin,sbin,modules,proc,sys,dev,tmp,run,mnt}

        cp ${busyboxStatic}/bin/busybox root/bin/
        for cmd in sh insmod lsmod rmmod mount umount mkdir mknod \
                   chmod sleep echo cat ls pwd reboot poweroff killall \
                   dmesg tail grep; do
          ln -s busybox root/bin/$cmd
        done
        ln -s ../bin/busybox root/sbin/init

        cp ${fsModule}/lib/modules/${kernel.modDirVersion}/misc/filesystem.ko \
           root/modules/

        cp ${kernel.modules}/lib/modules/${kernel.modDirVersion}/kernel/drivers/virtio/virtio.ko.xz \
           root/modules/virtio.ko.xz
        xz -d root/modules/virtio.ko.xz
        cp ${kernel.modules}/lib/modules/${kernel.modDirVersion}/kernel/drivers/virtio/virtio_pci.ko.xz \
           root/modules/virtio_pci.ko.xz
        xz -d root/modules/virtio_pci.ko.xz
        cp ${kernel.modules}/lib/modules/${kernel.modDirVersion}/kernel/drivers/virtio/virtio_pci_legacy_dev.ko.xz \
           root/modules/virtio_pci_legacy_dev.ko.xz
        xz -d root/modules/virtio_pci_legacy_dev.ko.xz
        cp ${kernel.modules}/lib/modules/${kernel.modDirVersion}/kernel/drivers/virtio/virtio_pci_modern_dev.ko.xz \
           root/modules/virtio_pci_modern_dev.ko.xz
        xz -d root/modules/virtio_pci_modern_dev.ko.xz
        cp ${kernel.modules}/lib/modules/${kernel.modDirVersion}/kernel/drivers/virtio/virtio_ring.ko.xz \
           root/modules/virtio_ring.ko.xz
        xz -d root/modules/virtio_ring.ko.xz
        cp ${kernel.modules}/lib/modules/${kernel.modDirVersion}/kernel/drivers/block/virtio_blk.ko.xz \
           root/modules/virtio_blk.ko.xz
        xz -d root/modules/virtio_blk.ko.xz

        cp ${fsTestProgram}/bin/fs-test root/bin/

        cat > root/init <<'INITEOF'
        #!/bin/sh
        export PATH=/bin

        mount -t proc    none /proc
        mount -t sysfs   none /sys
        mount -t devtmpfs none /dev
        mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null

        echo "[init] Block devices:"
        ls -l /sys/block/ 2>/dev/null || true

        INITEOF
        cat >> root/init <<'EOF'
        ${loadVirtioModules}

        FS_ARGS=""
        for arg in $(cat /proc/cmdline); do
          case "$arg" in
            myfs.*)
              FS_ARGS="$FS_ARGS ''${arg#myfs.}"
              ;;
          esac
        done

        if [ -n "$FS_ARGS" ]; then
          echo "[init] Loading filesystem module with args:$FS_ARGS"
          insmod /modules/filesystem.ko $FS_ARGS
        fi

        ${testScript}
        EOF
        chmod +x root/init

        mkdir -p $out
        cd root
        find . | sort | cpio -o -H newc | gzip -9 > $out/initrd.gz
      '';

  runQemuTest =
    pkgs:
    { name, testInitramfs, kernel, timeoutSec ? 30 }:
    pkgs.runCommand "fs-test-${name}"
      {
        nativeBuildInputs = [ pkgs.qemu ];
        requiredSystemFeatures = [ "kvm" ];
      }
      ''
        mkdir -p $out
        DISK=$(pwd)/disk.img
        truncate -s 100M "$DISK"

        timeout ${toString timeoutSec} ${pkgs.qemu}/bin/qemu-system-x86_64 \
          -enable-kvm \
          -m 512M \
          -smp 1 \
          -cpu host \
          -kernel ${kernel}/bzImage \
          -initrd ${testInitramfs}/initrd.gz \
          -drive file="$DISK",format=raw,if=virtio,media=disk \
          -append "console=ttyS0 nokaslr loglevel=3 panic=-1" \
          -nographic \
          -serial file:$out/qemu.log \
          -no-reboot \
          || true

        if grep -q "ALL TESTS PASSED" $out/qemu.log; then
          echo "fs-test-${name}: PASSED" >> $out/result
        else
          echo "fs-test-${name}: FAILED" >> $out/result
          cat $out/qemu.log
          exit 1
        fi
      '';

  testHelpers = ''
    fail() {
      echo "TEST FAILED: $1" > /dev/console
      poweroff -f
    }
    pass() {
      echo "$1" > /dev/console
    }
  '';

  loadVirtioModules = ''
    insmod /modules/virtio.ko
    insmod /modules/virtio_ring.ko
    insmod /modules/virtio_pci_legacy_dev.ko
    insmod /modules/virtio_pci_modern_dev.ko
    insmod /modules/virtio_pci.ko
    insmod /modules/virtio_blk.ko
  '';

  shutdownSequence = ''
    echo "ALL TESTS PASSED" > /dev/console
    poweroff -f
  '';

  commonFsParams = "disk_name=vda sb_first=0 sb_second=10 max_filename_len=16 max_file_size_sectors=2";

  mkTestScript = body: ''
    ${testHelpers}
    ${body}
    ${shutdownSequence}
  '';
}
