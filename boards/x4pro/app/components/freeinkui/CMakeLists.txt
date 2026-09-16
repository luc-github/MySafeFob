# FreeInkUI (freeink-sdk, MIT) — COPIE FIGEE, propre a l'app.
#
# ADR-010 (amendement 2026-09-15, docs/ROADMAP.md) : chaque board/cible
# vendorise sa PROPRE copie de FreeInkUI, jamais partagee/liee a celle de
# la factory (boards/x4pro/factory/components/freeinkui/) — un changement
# ici ne doit jamais pouvoir casser la factory, et vice-versa.
#
# Freestanding C++17, zero dependance Arduino/ESP-IDF (le seul symbole
# Arduino du SDK, src/FreeInkUI.cpp, est gate #if defined(ARDUINO_ARCH_ESP32)
# -> jamais defini ici, compile en no-op).
idf_component_register(
    SRCS "src/FreeInkUI.cpp"
    INCLUDE_DIRS "include"
)
