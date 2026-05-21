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
    insmod /modules/filesystem.ko disk_name=vda sb_first=5 sb_second=20 max_filename_len=16 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt || fail "mount failed"

    fs-test /mnt || fail "auto test failed"
    pass "TEST SB_FIRST WRITE PASSED"

    fs-test /mnt --ioctl-mapping file0 || fail "ioctl mapping failed"
    pass "TEST SB_FIRST MAPPING PASSED"

    umount /mnt || fail "umount failed"

    mount -t myfs /dev/vda /mnt || fail "remount failed"
    fs-test /mnt || fail "auto test after remount failed"
    pass "TEST SB_FIRST REMOUNT PASSED"

    umount /mnt || fail "umount 2 failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "sbfirst";
  inherit testInitramfs kernel;
}
