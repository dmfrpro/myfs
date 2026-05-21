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

    fs-test /mnt --concurrent-test 8 || fail "concurrent test failed"
    pass "TEST CONCURRENT WRITE PASSED"

    umount /mnt || fail "umount failed"
    mount -t myfs /dev/vda /mnt || fail "remount failed"

    fs-test /mnt --concurrent-test 8 || fail "concurrent verify after remount failed"
    pass "TEST CONCURRENT REMOUNT PASSED"

    umount /mnt || fail "umount 2 failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "concurrent";
  inherit testInitramfs kernel;
}
