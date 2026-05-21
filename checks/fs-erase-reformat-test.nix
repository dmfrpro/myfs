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
    mount -t myfs /dev/vda /mnt || fail "first mount failed"
    fs-test /mnt || fail "first auto test failed"
    umount /mnt || fail "umount failed"

    mount -t myfs /dev/vda /mnt || fail "mount for erase failed"
    fs-test /mnt --ioctl-erase || fail "ioctl erase failed"
    umount /mnt || fail "umount after erase failed"

    rmmod filesystem
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0

    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail after erase"
    pass "TEST ERASE PASSED"

    rmmod filesystem
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "remount after reformat failed"
    fs-test /mnt || fail "auto test after reformat failed"
    umount /mnt || fail "umount after reformat failed"
    pass "TEST REFORMAT PASSED"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "erase-reformat";
  inherit testInitramfs kernel;
}
