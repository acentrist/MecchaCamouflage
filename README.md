<p align="center">
  <img src="assets/meccha-camouflage-banner.png" alt="Meccha Camouflage banner" width="100%" />
</p>

# Meccha Camouflage

A standalone Windows tool for MECCHA CHAMELEON camouflage experiments.

## Download

Download the latest `meccha-camouflage.exe` from GitHub Releases:

- https://github.com/acentrist/MecchaCamouflage/releases/latest

The EXE is self-contained. It does not need to be placed next to `PenguinHotel-Win64-Shipping.exe`; it finds the running game process by name.

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
- `last_status.json`: latest success or failure summary.
- `.progress.json`: transient progress state used by the controller.

If the game crashes after a MECCHA CHAMELEON update, the tracked SDK may need to be regenerated and reviewed.

## Development

Use Windows with Visual Studio 2022 Build Tools. PowerShell 7 is recommended.

```bash
git clone https://github.com/acentrist/MecchaCamouflage.git
cd MecchaCamouflage
make build
```

The development EXE is generated at:

```text
.build/bin/meccha-camouflage.exe
```

To run the controller from the repo:

```bash
make run
```

The default development mode is configured at the top of `Makefile`.

## Updating the game SDK

The managed Dumper-7 SDK is stored in `dumper-sdk/`. The current tracked game SDK folder is:

```text
dumper-sdk/5.6.1-44394996+++UE5+Release-5.6-Chameleon/CppSDK
```

When a game update changes SDK layouts or causes startup crashes, regenerate the SDK:

```bash
make sdk-dump
```

Requirements:

1. Build once with `make build`.
2. Start MECCHA CHAMELEON and wait until the game process is running.
3. Run `make sdk-dump`.
4. Review and commit the generated SDK changes under `dumper-sdk/`.
5. Open a pull request with the SDK folder changes and any required runtime layout fixes.

## License

This project is licensed under the [MIT License](LICENSE.txt).
