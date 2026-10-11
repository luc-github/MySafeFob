# Monocypher 4.0.2 (BSD-2-Clause OR CC0-1.0, Loup Vaillant), vendored as-is
# from references/test_apps/x4pro-argon2-bench (upstream: monocypher.org).
# Used for Argon2id, the PIN key derivation of secret_store (ADR-004).
idf_component_register(
    SRCS "monocypher.c"
    INCLUDE_DIRS "."
)

# Argon2id is almost pure BLAKE2b arithmetic: at the project's debug level
# (-Og) it ran at ~0.8 s per MiB per pass (kdfbench 2026-10-10), far from
# the 0.5-1 s per PIN attempt target. Optimise this component only, the
# rest of the firmware stays at its debug level; the known-answer vectors
# of `kdfbench` check the result is unchanged.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
