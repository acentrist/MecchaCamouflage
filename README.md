<p align="center">
  <img src="assets/meccha-camouflage-banner.png" alt="Meccha Camouflage banner" width="100%" />
</p>

# Meccha Camouflage

A standalone Windows tool for MECCHA CHAMELEON camouflage experiments.

## Download

Download the latest `meccha-camouflage.exe` from GitHub Releases:

- https://github.com/acentrist/MecchaCamouflage/releases/latest

The EXE is self-contained. it finds the running game process by name.

## Usage

1. Start MECCHA CHAMELEON.
2. Start `meccha-camouflage.exe`.
3. Press `F10` in game.

## Logs

Logs and status files are written under:

```text
%LOCALAPPDATA%\MecchaCamouflage\runtime\
```

Useful files:

- `events.jsonl`: structured runtime events.
- `runtime.log`: plain text runtime log.
- `last_status.json`: latest success or failure summary.
- `.progress.json`: transient bridge progress sidecar used by the controller.

If the game crashes after a MECCHA CHAMELEON update, the tracked SDK may need to be regenerated and reviewed.

## Development

```bash
git clone https://github.com/acentrist/MecchaCamouflage.git
cd MecchaCamouflage
make run
```

## License

This project is licensed under the [MIT License](LICENSE.txt).
