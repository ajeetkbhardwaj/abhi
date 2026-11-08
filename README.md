# abhi

AI/ML research workspace. Includes **Karta**, a device-agnostic C++17 coding agent (pi-style) with a terminal UI.

## Karta agent

- Location: [`karta/`](./karta/) — full build/run/docs: [`karta/README.md`](./karta/README.md)
- TUI notes: [`karta/docs/TUI.md`](./karta/docs/TUI.md)
- Feature parity: [`karta/docs/FEATURE_PARITY.md`](./karta/docs/FEATURE_PARITY.md)

### Quick start

```bash
cd karta
cmake -S . -B build -DKARTA_BUILD_TUI=ON   # needs libcurl + ncurses/ncursesw
cmake --build build -j4

./build/karta            # opens TUI on a TTY (pi-style)
# or: ./run-tui.sh / ../run-karta-tui.sh
# PATH (this workspace): ~/.local/bin/karta -> karta/build/karta
```

In the TUI: `/login` (provider + API key, saved to `~/.karta/auth.json`), `/model` (search/select across logged-in providers), `/thinking 0|1|2|3`, `/help`, `/exit`. Non-interactive: `--mode text|json|rpc`, `-p/--print`.
