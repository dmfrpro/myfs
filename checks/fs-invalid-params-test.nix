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
    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=999999 max_filename_len=16 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with invalid sb_second"
    pass "TEST INVALID SB_SECOND PASSED"
    rmmod filesystem

    insmod /modules/filesystem.ko disk_name=vda sb_first=5 sb_second=5 max_filename_len=16 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with sb_second == sb_first"
    pass "TEST INVALID SB_FIRST_EQUAL_SECOND PASSED"
    rmmod filesystem

    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=10 max_filename_len=16 max_file_size_sectors=0
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with max_file_size_sectors=0"
    pass "TEST INVALID MAX_FILE_SIZE PASSED"
    rmmod filesystem

    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=10 max_filename_len=0 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with max_filename_len=0"
    pass "TEST INVALID MAX_FILENAME_LEN PASSED"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "invalid-params";
  inherit testInitramfs kernel;
}
