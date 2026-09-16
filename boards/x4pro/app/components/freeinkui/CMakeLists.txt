# FreeInkUI (freeink-sdk, MIT) — FROZEN COPY, specific to the app.
#
# ADR-010 (amendment 2026-09-15, docs/ROADMAP.md): each board/target
# vendors its OWN copy of FreeInkUI, never shared with/linked to the
# factory's (boards/x4pro/factory/components/freeinkui/) — a change
# here must never be able to break the factory, and vice versa.
#
# Freestanding C++17, zero Arduino/ESP-IDF dependency (the SDK's only
# Arduino symbol, src/FreeInkUI.cpp, is gated behind
# #if defined(ARDUINO_ARCH_ESP32) -> never defined here, compiles to a no-op).
idf_component_register(
    SRCS "src/FreeInkUI.cpp"
    INCLUDE_DIRS "include"
)
