GAME_ROOT ?= C:\Program Files (x86)\Steam\steamapps\common\MECCHA CHAMELEON
VERSION ?= 0.1.0
RUNTIME_ARGS ?=

.PHONY: build copy-to-game service probe apply shutdown package clean

build:
	./scripts/dev_flow.sh -Action build

copy-to-game: build
	./scripts/dev_flow.sh -Action deploy -GameRoot '$(GAME_ROOT)'

service:
	./scripts/dev_flow.sh -Action run -RuntimeArgString "--mode service $(RUNTIME_ARGS)"

probe:
	./scripts/dev_flow.sh -Action run -RuntimeArgString "--mode probe $(RUNTIME_ARGS)"

apply:
	./scripts/dev_flow.sh -Action run -RuntimeArgString "--mode apply $(RUNTIME_ARGS)"

shutdown:
	./.build/native/bin/meccha-camouflage.exe --mode shutdown

package: build
	pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/package_release.ps1 -Version $(VERSION)

clean:
	rm -rf .build
