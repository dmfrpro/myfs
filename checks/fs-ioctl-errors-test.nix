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

    fs-test /mnt --ioctl-invalid || fail "ioctl invalid test failed"
    pass "TEST IOCTL INVALID PASSED"

    fs-test /mnt --ioctl-mapping-bad || fail "ioctl mapping bad test failed"
    pass "TEST IOCTL MAPPING BAD PASSED"

    fs-test /mnt --edge-test || fail "edge test failed"
    pass "TEST EDGE READWRITE PASSED"

    if open /mnt/nonexistent_file 2>/dev/null; then
      fail "open nonexistent should fail"
    fi
    pass "TEST OPEN NOTFOUND PASSED"

    umount /mnt || fail "umount failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "ioctl-errors";
  inherit testInitramfs kernel;
}
