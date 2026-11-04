# Coding style

The rules of the KM-Sweetch project, adapted to a desktop Qt application. They apply to our code
in `src/` and `tests/`. Vendor files (`drivers/`, `original-software/`) are never edited.

Language: C++20, GCC 13+ / MinGW-w64, Qt 6. Unlike the firmware project, **heap, exceptions,
RTTI and the standard library are allowed**. Exceptions are for failures that cannot be handled
where they happen (e.g. a malformed firmware file); they must never cross into Qt's event loop,
so every slot and the acquisition thread loop catch what they call.

## 1. Layers, modules and files

| Layer | Folder | Namespace | May include |
|---|---|---|---|
| Core (portable) | `src/core/` | `Core` | `core/`, standard headers |
| Hardware | `src/hw/` | `Hw` | `core/`, `hw/`, libusb (only in `.cpp` files) |
| Application | `src/app/` | `App` | `core/`, `hw/`, `app/`, QtCore |
| GUI | `src/gui/` | `Gui` | anything |
| Entry point | `src/main.cpp` | none | anything |

- A module is a class in its layer's namespace, one `.hpp`/`.cpp` pair named after the class:
  `hw/Dso2100Usb.hpp`. Folder names are lowercase.
- Stateless helpers are free functions (`Core::MeasureAll()`, `Core::FindTrigger()`); shared
  constants without behaviour go in a nested namespace (`Hw::Dso2100::kPidRunning`).
- `core/` and `hw/` do not include Qt: they are unit-tested on their own and may serve another UI.
- Virtual functions only where the choice is made at run time (`Hw::ScopeBackend`). Backends are
  owned as values, so the base destructor is `protected` and not virtual.
- Includes are relative to `src/` (`"core/Scope.hpp"`). Order: own header, project headers,
  Qt and libusb, then `<system>`.

## 2. Naming

| Thing | Style | Example |
|---|---|---|
| Namespace, type, enum | `CamelCase` | `Core`, `ChannelSettings`, `TriggerEdge` |
| Function, member function, Qt signal | `CamelCase` | `Acquire()`, `SetSettings()`, `FrameReady()` |
| Local variable, parameter | `camelCase` | `secPerDiv`, `levelV` |
| Non-static data member | `m_camelCase` | `m_settings` |
| Static data member, file-local variable | `s_camelCase` | |
| Constant (`constexpr`), table | `kPascalCase` | `kRecordLen`, `kRateDividers` |
| Enum value | `kPascalCase`, `enum class` with a fixed underlying type | `Coupling::kAc` |

- Qt virtual overrides keep Qt's names (`paintEvent`, `run`), like any library name.
- Units in names when not obvious: `Ms`, `Hz`, `V`, `Div`, `Len`.
- No magic numbers in logic: a named constant or a comment (protocol bytes included).

## 3. Formatting

What `clang-format` writes is the format (`.clang-format`, version pinned in
`.devcontainer/Dockerfile`): `cmake --build --preset linux --target format`. CI fails on
unformatted code. 4 spaces, 100 columns, Allman braces, braces always, aligned declarations.

## 4. Code rules

- `std::array`, `std::span`, `std::string_view`, `std::optional`; `const` where you don't write.
- `static_cast`; `reinterpret_cast` only for byte buffers; no C casts (`-Wold-style-cast`).
- Prefix `++i` where the old value is not used.
- No warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wold-style-cast`, errors in CI.
- Threads: the acquisition thread owns the USB device; the GUI talks to it through the
  thread-safe setters in `App::Acquisition` and receives frames by queued signal.
- Busy-waits on hardware have a timeout.

## 5. Comments

Each file opens with a comment saying what it does. Comment *why* and hardware facts (USB IDs,
endpoints, byte layouts), not what the code says. Public functions get a one-line comment in the
header covering the return value.

## 6. Commits and tests

Conventional Commits: `<type>(<scope>): <description>` (`feat`, `fix`, `refactor`, `docs`,
`build`, `style`, `test`, `chore`). Logic in `core/` gets GoogleTest tests in
`tests/core/<Module>Test.cpp`, each starting with a comment that says what it checks and why.
