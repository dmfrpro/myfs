{
  pkgs,
  perSystem,
  flake,
  ...
}:

let
  kernel = pkgs.linuxKernel.packages.linux_6_12.kernel;
  fsModule = perSystem.self.filesystem;
  fsTestProgram = perSystem.self.fs-test;

  testScript = flake.lib.mkTestScript ''
    # Format disk
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "mount failed"
    fs-test /mnt || fail "auto test failed"
    umount /mnt || fail "umount failed"
    rmmod filesystem

    # Zero primary superblock (sector 0)
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=512 count=1 conv=notrunc status=none || fail "dd zero primary failed"

    # Mount should succeed via backup superblock
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0
    mount -t myfs /dev/vda /mnt || fail "mount via backup sb failed"
    fs-test /mnt || fail "auto test via backup sb failed"
    pass "TEST PRIMARY SB ZERO BACKUP OK PASSED"
    umount /mnt || fail "umount failed"
    rmmod filesystem

    # Restore primary from backup by reformatting
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "reformat mount failed"
    fs-test /mnt || fail "auto test after reformat failed"
    umount /mnt || fail "umount failed"
    rmmod filesystem

    # Corrupt BOTH superblocks
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=512 count=1 conv=notrunc status=none || fail "dd zero primary 2 failed"
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=512 seek=10 count=1 conv=notrunc status=none || fail "dd zero backup failed"

    # Mount should fail
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with both sb corrupted"
    pass "TEST BOTH SB CORRUPT MOUNT FAIL PASSED"
    rmmod filesystem
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "superblock-resilience";
  inherit testInitramfs kernel;
}
