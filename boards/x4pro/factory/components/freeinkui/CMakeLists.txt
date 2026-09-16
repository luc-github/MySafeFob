# FreeInkUI (freeink-sdk, MIT) — FROZEN COPY, specific to the factory.
#
# ADR-010 (amendment 2026-09-15, docs/ROADMAP.md): the factory is allowed
# to use FreeInkUI (here: just DisplayTarget, for the splash), but via
# an INDEPENDENT copy from the one the app vendors for itself —
# never shared, never auto-updated together. A change on the app side
# (freeink-sdk update, component rework) must never be able to break the
# factory. Do NOT replace this folder with a symlink or a dependency
# on the app's copy.
#
# Freestanding C++17, zero Arduino/ESP-IDF dependency (the SDK's only
# Arduino symbol, src/FreeInkUI.cpp, is gated behind
# #if defined(ARDUINO_ARCH_ESP32) -> never defined here, compiles to a no-op).
idf_component_register(
    SRCS "src/FreeInkUI.cpp"
    INCLUDE_DIRS "include"
)
