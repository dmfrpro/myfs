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
    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=100000 max_filename_len=16 max_file_size_sectors=20000
    mount -t myfs /dev/vda /mnt || fail "mount failed"

    fs-test /mnt --ioctl-list || fail "ioctl list failed"
    pass "TEST FILE LIMIT MOUNT PASSED"

    fs-test /mnt || fail "auto test failed"
    pass "TEST FILE LIMIT WRITE PASSED"

    umount /mnt || fail "umount failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "file-limit";
  inherit testInitramfs kernel;
  timeoutSec = 60;
}
