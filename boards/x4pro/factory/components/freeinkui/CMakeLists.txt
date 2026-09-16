# FreeInkUI (freeink-sdk, MIT) — COPIE FIGEE, propre a la factory.
#
# ADR-010 (amendement 2026-09-15, docs/ROADMAP.md) : la factory a le droit
# d'utiliser FreeInkUI (ici : juste DisplayTarget, pour le splash), mais via
# une copie INDEPENDANTE de celle que l'app vendorisera pour elle-meme —
# jamais partagee, jamais mise a jour automatiquement en meme temps. Un
# changement cote app (mise a jour freeink-sdk, refonte composants) ne doit
# jamais pouvoir casser la factory. Ne PAS remplacer ce dossier par un lien
# symbolique ou une dependance vers la copie de l'app.
#
# Freestanding C++17, zero dependance Arduino/ESP-IDF (le seul symbole
# Arduino du SDK, src/FreeInkUI.cpp, est gate #if defined(ARDUINO_ARCH_ESP32)
# -> jamais defini ici, compile en no-op).
idf_component_register(
    SRCS "src/FreeInkUI.cpp"
    INCLUDE_DIRS "include"
)
