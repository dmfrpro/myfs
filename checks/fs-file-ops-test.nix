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

    # Test 1: unwritten file reads zeros
    fs-test /mnt --read-unwritten || fail "read unwritten failed"
    pass "TEST READ UNWRITTEN PASSED"

    # Test 2: partial sector read/write
    fs-test /mnt --partial-sector || fail "partial sector failed"
    pass "TEST PARTIAL SECTOR PASSED"

    # Test 3: seek to offset 4, write, read back
    fs-test /mnt --seek-test || fail "seek test failed"
    pass "TEST SEEK PASSED"

    # Test 4: multiple writes to same file
    fs-test /mnt --multi-write 20 || fail "multi-write failed"
    pass "TEST MULTI WRITE PASSED"

    # Test 5: edge read/write at EOF (already covered but re-run for completeness)
    fs-test /mnt --edge-test || fail "edge test failed"
    pass "TEST EDGE PASSED"

    umount /mnt || fail "umount failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "file-ops";
  inherit testInitramfs kernel;
}
