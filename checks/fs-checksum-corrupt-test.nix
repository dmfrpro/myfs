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
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "mount failed"
    fs-test /mnt || fail "auto test failed"
    umount /mnt || fail "umount failed"
    rmmod filesystem

    # Corrupt primary checksum - mount should succeed via backup
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=1 seek=20 count=4 conv=notrunc status=none || fail "dd corrupt primary failed"

    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0
    mount -t myfs /dev/vda /mnt || fail "mount should succeed via backup sb"
    fs-test /mnt || fail "auto test via backup sb failed"
    pass "TEST PRIMARY CHECKSUM CORRUPT FALLBACK OK"
    umount /mnt || fail "umount failed"
    rmmod filesystem

    # Restore primary by reformatting
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=512 count=1 conv=notrunc status=none || fail "dd zero primary failed"
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "reformat mount failed"
    fs-test /mnt || fail "auto test after reformat failed"
    umount /mnt || fail "umount after reformat failed"
    rmmod filesystem

    # Corrupt backup checksum - mount should fail (primary valid but backup checked)
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=1 seek=5140 count=4 conv=notrunc status=none || fail "dd corrupt backup failed"

    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail after backup checksum corruption"
    pass "TEST BACKUP CHECKSUM CORRUPT MOUNT FAIL PASSED"
    rmmod filesystem

    # Corrupt both checksums - mount should fail
    /bin/busybox dd if=/dev/zero of=/dev/vda bs=1 seek=20 count=4 conv=notrunc status=none || fail "dd corrupt primary 2 failed"

    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with both sb checksums corrupted"
    pass "TEST BOTH CHECKSUM CORRUPT MOUNT FAIL PASSED"
    rmmod filesystem
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "checksum-corrupt";
  inherit testInitramfs kernel;
}
