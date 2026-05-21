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
    # sb_first negative
    insmod /modules/filesystem.ko disk_name=vda sb_first=-1 sb_second=10 max_filename_len=16 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with negative sb_first"
    pass "TEST NEGATIVE SB_FIRST PASSED"
    rmmod filesystem

    # sb_first beyond disk
    insmod /modules/filesystem.ko disk_name=vda sb_first=999999 sb_second=10 max_filename_len=16 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with sb_first beyond disk"
    pass "TEST SB_FIRST BEYOND DISK PASSED"
    rmmod filesystem

    # sb_second negative
    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=-1 max_filename_len=16 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with negative sb_second"
    pass "TEST NEGATIVE SB_SECOND PASSED"
    rmmod filesystem

    # max_filename_len > MYFS_NAME_LEN
    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=10 max_filename_len=100 max_file_size_sectors=2
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with max_filename_len > 16"
    pass "TEST MAX_FILENAME_LEN TOO LARGE PASSED"
    rmmod filesystem

    # max_file_size_sectors negative
    insmod /modules/filesystem.ko disk_name=vda sb_first=0 sb_second=10 max_filename_len=16 max_file_size_sectors=-1
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail with negative max_file_size_sectors"
    pass "TEST NEGATIVE MAX_FILE_SIZE PASSED"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "param-boundaries";
  inherit testInitramfs kernel;
}
