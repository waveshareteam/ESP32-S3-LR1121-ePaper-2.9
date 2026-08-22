# Repository structure

| Path | Maintained content |
| --- | --- |
| `examples/esp-idf/` | One first-party ESP-IDF project per direct child |
| `examples/arduino/` | Direct first-party sketches and bundled support files |
| `examples/arduino/libraries/` | Repository-pinned Arduino board library |
| `assets/product/` | Product images |
| `hardware/` | Schematics and mechanical references |
| `config/` | Machine-readable automation settings |
| `scripts/` | Example discovery and static checks |
| `releases/` | Firmware packagers and artifact tools |
| `firmware/` | Factory/recovery firmware placeholder |
| `docs/` | User and maintainer documentation |
| `.github/` | GitHub workflows and collaboration templates |

Only direct ESP-IDF projects and direct Arduino sketches are product CI
inputs. Nested component projects and library examples are not independent CI
jobs.

Build directories, managed components, dependency locks, generated firmware
archives, and downloaded artifacts are generated outputs and should remain
untracked. The internal `_backups/` directory is a local recovery resource
and is not part of the public repository.
