# X4 Pro — account store sizing, shared by the app (board_config.cmake ->
# root CMakeLists) AND the factory (factory/CMakeLists.txt), so both
# firmwares agree on the store they read.
#
# MSF_STORE_MAX_ACCOUNTS: how many accounts the device holds (2026-10-12:
# 250, was 64). Bounded by the store format, not chosen freely:
#   - format 2 needs a `secrets` partition of at least 2 MiB (two 512 KiB
#     slots at 0x100000 / 0x180000, partitions.csv: 2.25 MiB here);
#   - the worst case (every field full, 32 recovery codes, ~1.1 KB per
#     account) must fit one 512 KiB slot: checked at compile time in
#     secret_store.c (about 490 at most).
# The PC tools (tools/msf_backup.py, tools/msf_editor.html) use the same
# value: keep them in step if it changes.
set(MSF_STORE_MAX_ACCOUNTS 250)

# Must run after project(): add_compile_definitions() would not reach the
# IDF components (same trap as MSF_BOARD_NAME in the root CMakeLists).
function(msf_store_config_apply)
    idf_build_set_property(COMPILE_DEFINITIONS "MSF_STORE_MAX_ACCOUNTS=${MSF_STORE_MAX_ACCOUNTS}" APPEND)
endfunction()
