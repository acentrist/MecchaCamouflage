<p align="center">
  <img src="assets/icon.png" alt="Meccha Camouflage icon" width="96" />
</p>

# Meccha Camouflage

A standalone Windows desktop tool for MECCHA CHAMELEON camouflage experiments.

## Download

Download the latest `meccha-camouflage.exe` from GitHub Releases:

- https://github.com/acentrist/MecchaCamouflage/releases/latest

## Usage

1. Start MECCHA CHAMELEON.
2. Start `meccha-camouflage.exe`.
3. Confirm the target process and bridge state in the app.
4. Press the saved paint hotkey.

Settings are read-only until `Edit` is selected. Use `Save` to apply changes or
`Cancel` to discard them.

Logs are written under:

```text
%LOCALAPPDATA%\MecchaCamouflage\runtime\
```

## Development

```bash
git clone https://github.com/acentrist/MecchaCamouflage.git
cd MecchaCamouflage
make run
```

## License

This project is licensed under the [MIT License](LICENSE.txt).
