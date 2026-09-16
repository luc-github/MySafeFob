# Project language policy

All code, comments, log/UI strings, and documentation in this repository must be written in **English only**. No French (or any other non-English language) is allowed anywhere in the codebase — this includes:

- Source code comments (`.c`, `.cpp`, `.h`)
- Doxygen/API documentation blocks
- Log messages (`ESP_LOGI`/`ESP_LOGE`/`ESP_LOGW`/`printf`/etc.) and any string shown on-screen
- Markdown documentation (`docs/`, `README.md`, board READMEs)
- Build files and scripts (`CMakeLists.txt`, `sdkconfig.defaults`, `.bat`/`.py` scripts, config comments)

When writing new code or docs, or editing existing ones, always use English. When touching a file that still contains French text, translate it to English as part of the change rather than leaving it mixed.

Exception: vendored/third-party code under `references/` and generated `build/` directories are out of scope and should not be modified for this rule.
