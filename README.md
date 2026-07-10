<p align="center">
  <img src="docs/assets/meccha-camouflage-readme-banner-v151-1600w.jpg" alt="Meccha Camouflage demo" width="900" />
</p>

<h1>
  <img src="resources/app-icons/icon.png" alt="Meccha Camouflage icon" width="36" />
  Meccha Camouflage
</h1>

A standalone Windows desktop tool for MECCHA CHAMELEON camouflage experiments.

## Download

Download the latest `meccha-camouflage.exe` from GitHub Releases:

- https://github.com/acentrist/MecchaCamouflage/releases/latest

## Usage

1. Start MECCHA CHAMELEON.
2. Start `meccha-camouflage.exe`.
3. Confirm the target process and bridge state in the app.
4. Press the saved paint hotkey.

Logs are written under:

```text
%LOCALAPPDATA%\MecchaCamouflage\versions\<version>\logs\
```

## Development

```bash
git clone https://github.com/acentrist/MecchaCamouflage.git
cd MecchaCamouflage
make run
```

Core development references:

- [Repository layout](docs/repository-layout.md)
- [Runtime maintenance](docs/runtime-maintenance.md)
- [Research tools](docs/research-tools.md)
- [Release checklist](docs/release-checklist.md)

## License

This project is licensed under [GPL-3.0-or-later](LICENSE.txt).

The official project repository is:

- https://github.com/acentrist/MecchaCamouflage

Modified builds must preserve the license notice and must not imply they are
official releases. See [BRANDING.md](BRANDING.md).
