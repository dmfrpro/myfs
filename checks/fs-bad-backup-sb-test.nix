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

    /bin/busybox dd if=/dev/zero of=/dev/vda bs=1 seek=5120 count=4 conv=notrunc status=none || fail "dd corrupt backup magic failed"

    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail after backup magic corruption"
    pass "TEST BACKUP MAGIC CORRUPT PASSED"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "bad-backup-sb";
  inherit testInitramfs kernel;
}
