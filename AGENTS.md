# Project Overview
KamyshTracker is a native Windows OBS Studio plugin that reads the current media session through Windows System Media Transport Controls (SMTC) and answers configured Twitch chat commands with the current track via Twitch EventSub WebSocket and Helix chat APIs.

# Tech Stack
- C++20 (`CMAKE_CXX_STANDARD 20`, extensions off).
- CMake 3.24+ (`cmake_minimum_required(VERSION 3.24)`).
- Qt 6 with Widgets, Network, and WebSockets (`find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets)`).
- OBS/libobs and OBS Frontend API; README requires OBS Studio 30+, while CMake fallback paths reference OBS Studio 32.1.2 headers/import libraries under `.deps/`.
- Windows APIs/libraries: Windows SMTC via C++/WinRT, `windowsapp`, `winhttp`, `ws2_32`, `crypt32`, and `bcrypt`.

# Core Commands
- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Path\To\Qt;C:\Path\To\OBS"`
- Build: `cmake --build build --config Release`
- Install: `cmake --install build --config Release --prefix "C:\Program Files\obs-studio"`
- Package: `cmake --install build --config Release --prefix package`
- Test: `> TODO: No test command or test target is defined in this repo.`
- Lint: `> TODO: No lint command or formatter configuration is defined in this repo.`

# Key Directories
- `src/`: plugin implementation, including OBS module lifecycle, SMTC media monitoring, settings UI, settings persistence, and Twitch client logic.
- `include/`: public/internal headers for the plugin classes and data structures.
- `data/locale/`: OBS plugin locale files installed under `data/obs-plugins/kamyshtracker`.
- `cmake/obs-generated/`: generated OBS configuration header used when local OBS package targets are unavailable.
- `.github/workflows/`: manual GitHub Actions release workflow.
- `Resources/`: Windows icon resource assets.
- `.deps/`, `build/`, `package/`: local dependency, build, and packaging artifacts; these are ignored and should remain untracked.

# Coding Conventions
- Keep code inside the `kamyshtracker` namespace.
- Use C++20 standard library types and RAII ownership (`std::unique_ptr`, `std::thread`, `std::atomic_bool`, mutex guards) consistent with existing code.
- Use Qt types at UI/API boundaries and convert explicitly between Qt strings and STL strings/wide strings.
- Keep OBS entry points in `src/plugin-main.cpp` and use OBS logging/config APIs for plugin lifecycle and profile settings.
- Use 4-space indentation, opening braces on the next line for functions/classes/namespaces, and concise anonymous-namespace helpers for file-local functions.
- Prefer existing CMake target patterns: add headers and sources to the `kamyshtracker` module target, then link dependencies via `target_link_libraries`.

# Agent Guardrails
- Do not commit or edit generated/local artifacts in `.deps/`, `build/`, `package/`, or `kamyshtracker-obs-plugin.zip`.
- Do not expose Twitch access tokens, refresh tokens, client secrets, or user-specific `kamyshtracker.ini` contents.
- Do not reintroduce the removed tray app, local HTTP API, `127.0.0.1:5050`, `/json`, `/widget`, or OBS Browser Source workflow.
- Do not change OAuth/account-matching safety behavior without preserving the requirement that OBS Twitch account and OAuth account are checked when OBS exposes the login.
- Do not edit release notes, release workflow, or packaging layout unless the task explicitly asks for release/package changes.
- Branching policy: `> TODO: No repository branching policy is documented.`
