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
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} || {
      dmesg | tail -n 30 > /dev/console
      fail "insmod failed"
    }
    mount -t myfs /dev/vda /mnt || fail "mount failed"

    fs-test /mnt || fail "auto test failed"
    pass "TEST 1 PASSED"

    fs-test /mnt --ioctl-list || fail "ioctl list failed"
    pass "TEST 2 PASSED"

    fs-test /mnt --ioctl-mapping file0 || fail "ioctl mapping failed"
    pass "TEST 3 PASSED"

    fs-test /mnt --ioctl-zero || fail "ioctl zero failed"
    fs-test /mnt || fail "auto test after zero failed"
    pass "TEST 4 PASSED"

    umount /mnt || fail "umount failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "basic";
  inherit testInitramfs kernel;
}
