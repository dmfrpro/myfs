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

    echo "HELLO" > /mnt/file0 || fail "write pattern failed"
    umount /mnt || fail "umount failed"

    mount -t myfs /dev/vda /mnt || fail "remount failed"
    grep -q "HELLO" /mnt/file0 || fail "data not persisted"
    pass "TEST PERSISTENCE PASSED"
    umount /mnt || fail "umount 2 failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "persistence";
  inherit testInitramfs kernel;
}
