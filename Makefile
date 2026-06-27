NATIVE_APPLY_MODE ?= template_brush_paint
VERSION ?= 1.0.0
BUILD_PS := scripts/build.ps1
RUN_PS := scripts/dev.ps1
PACKAGE_PS := scripts/release.ps1

.PHONY: build run package clean

define RUN_POWERSHELL
	@if command -v pwsh >/dev/null 2>&1; then \
		pwsh -NoProfile -ExecutionPolicy Bypass -File $(1) $(2); \
	elif command -v powershell.exe >/dev/null 2>&1; then \
		PS_SCRIPT_WIN="$$(if command -v wslpath >/dev/null 2>&1; then wslpath -w $(1); else printf '%s' $(1); fi)"; \
		powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$PS_SCRIPT_WIN" $(2); \
	else \
		echo "PowerShell runtime not found." >&2; exit 127; \
	fi
endef

build:
	$(call RUN_POWERSHELL,$(BUILD_PS),)

run: build
	$(call RUN_POWERSHELL,$(RUN_PS),-NativeApplyMode $(NATIVE_APPLY_MODE))

package: build
	$(call RUN_POWERSHELL,$(PACKAGE_PS),-Version $(VERSION))

clean:
	rm -rf .build
