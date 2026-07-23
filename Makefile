NATIVE_APPLY_MODE ?= mesh_first_paint
DIAGNOSTIC_STROKE_LIMIT ?= 0
RESEARCH_ARTIFACTS ?= $(MECCHA_RESEARCH_ARTIFACTS)
# A tagged, clean source tree is a release and deliberately keeps its tag.
# Every development build receives its own identity, however: config, runtime
# assets, logs, and image designs are all version-scoped.  Reusing the old
# `...-dirty` string made separate local builds share those directories.
ifndef VERSION
VERSION := $(shell \
	exact="$$(git describe --tags --exact-match 2>/dev/null)"; \
	dirty="$$(git status --porcelain --untracked-files=normal 2>/dev/null || printf x)"; \
	if [ -n "$$exact" ] && [ -z "$$dirty" ]; then \
		printf '%s' "$$exact"; \
	else \
		base="$$(git describe --tags --always 2>/dev/null || printf dev)"; \
		printf '%s-build-%s' "$$base" "$$(date -u +%Y%m%d%H%M%S%N)"; \
	fi)
endif
BUILD_PS := scripts/build.ps1
RUN_PS := scripts/dev.ps1
START_PS := scripts/start.ps1
PACKAGE_PS := scripts/release.ps1
MESH_PS := scripts/mesh.ps1
REFERENCE_PROFILE_PS := scripts/refresh-image-reference-profile.ps1
REVIEW_DEAD_CODE_PS := scripts/review/runtime-dead-code-inventory.ps1
START_EXE ?= .build/bin/meccha-camouflage.exe
DEV_OUT_DIR ?= .build/bin-dev
RESEARCH_ARTIFACT_FLAGS := $(if $(filter 1 true TRUE yes YES on ON,$(RESEARCH_ARTIFACTS)),-EnableResearchArtifacts,)
MESH_ARGS := $(if $(PAKS),-PaksPath "$(PAKS)",) $(if $(MAPPINGS),-MappingsPath "$(MAPPINGS)",) $(if $(CUE4PARSE),-Cue4ParsePath "$(CUE4PARSE)",) $(if $(OUTPUT),-OutputPath "$(OUTPUT)",) $(if $(ASSET),-AssetPath "$(ASSET)",) $(if $(EXPORT),-ExportName "$(EXPORT)",) $(if $(GAME_VERSION),-GameVersion "$(GAME_VERSION)",) $(if $(OODLE),-OodlePath "$(OODLE)",) $(if $(ZLIB),-ZlibPath "$(ZLIB)",) $(if $(TEXTURE_SIZE),-TextureSize "$(TEXTURE_SIZE)",) $(if $(EXPECTED_VERTICES),-ExpectedVertices "$(EXPECTED_VERTICES)",) $(if $(EXPECTED_INDICES),-ExpectedIndices "$(EXPECTED_INDICES)",) $(if $(EXPECTED_BONES),-ExpectedBones "$(EXPECTED_BONES)",)
REFERENCE_BODY ?= round
REFERENCE_CONFIRM_ARG := $(if $(filter 1 true TRUE yes YES on ON,$(CONFIRM_NEUTRAL_POSE)),-CaptureNeutralPose,)
REFERENCE_ARGS := -BodyType "$(REFERENCE_BODY)" $(REFERENCE_CONFIRM_ARG) $(if $(PAKS),-PaksPath "$(PAKS)",) $(if $(MAPPINGS),-MappingsPath "$(MAPPINGS)",) $(if $(CUE4PARSE),-Cue4ParsePath "$(CUE4PARSE)",) $(if $(GAME_VERSION),-GameVersion "$(GAME_VERSION)",) $(if $(OODLE),-OodlePath "$(OODLE)",) $(if $(ZLIB),-ZlibPath "$(ZLIB)",)

.PHONY: build build-timed build-dev build-dev-timed run dev start package mesh refresh-image-reference review-dead-code clean clean-artifacts clean-all

define RUN_POWERSHELL
	@if command -v powershell.exe >/dev/null 2>&1; then \
		PS_SCRIPT_WIN="$$(if command -v wslpath >/dev/null 2>&1; then wslpath -w $(1); else printf '%s' $(1); fi)"; \
		powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$PS_SCRIPT_WIN" $(2); \
	elif command -v pwsh >/dev/null 2>&1; then \
		pwsh -NoProfile -ExecutionPolicy Bypass -File $(1) $(2); \
	else \
		echo "PowerShell runtime not found." >&2; exit 127; \
	fi
endef

build:
	$(call RUN_POWERSHELL,$(BUILD_PS),-Version $(VERSION))

build-timed:
	$(call RUN_POWERSHELL,$(BUILD_PS),-Version $(VERSION) -ShowTimings)

build-dev:
	$(call RUN_POWERSHELL,$(BUILD_PS),-Version $(VERSION) -BuildMode DevLooseSelfContained -OutDir "$(DEV_OUT_DIR)")

build-dev-timed:
	$(call RUN_POWERSHELL,$(BUILD_PS),-Version $(VERSION) -BuildMode DevLooseSelfContained -OutDir "$(DEV_OUT_DIR)" -ShowTimings)

run: build-dev
	$(call RUN_POWERSHELL,$(RUN_PS),-BuildOutputDir "$(DEV_OUT_DIR)" -NativeApplyMode $(NATIVE_APPLY_MODE) $(RESEARCH_ARTIFACT_FLAGS))

dev: run

start:
	@if [ ! -f "$(START_EXE)" ]; then \
		echo "Built exe not found: $(START_EXE). Run make build first, or pass START_EXE=path." >&2; \
		exit 1; \
	fi
	@if command -v powershell.exe >/dev/null 2>&1; then \
		PS_SCRIPT_WIN="$$(if command -v wslpath >/dev/null 2>&1; then wslpath -w "$(START_PS)"; else printf '%s' "$(START_PS)"; fi)"; \
		EXE_WIN="$$(if command -v wslpath >/dev/null 2>&1; then wslpath -w "$(START_EXE)"; else printf '%s' "$(START_EXE)"; fi)"; \
		powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$PS_SCRIPT_WIN" -SourceExe "$$EXE_WIN" -DiagnosticStrokeLimit "$(DIAGNOSTIC_STROKE_LIMIT)"; \
	elif command -v pwsh >/dev/null 2>&1; then \
		pwsh -NoProfile -ExecutionPolicy Bypass -File "$(START_PS)" -SourceExe "$(START_EXE)" -DiagnosticStrokeLimit "$(DIAGNOSTIC_STROKE_LIMIT)"; \
	else \
		echo "PowerShell runtime not found." >&2; exit 127; \
	fi

package: build
	$(call RUN_POWERSHELL,$(PACKAGE_PS),-Version $(VERSION))

mesh:
	$(call RUN_POWERSHELL,$(MESH_PS),$(MESH_ARGS))

refresh-image-reference:
	$(call RUN_POWERSHELL,$(REFERENCE_PROFILE_PS),$(REFERENCE_ARGS))

review-dead-code:
	$(call RUN_POWERSHELL,$(REVIEW_DEAD_CODE_PS),)

clean:
	rm -rf .build

clean-artifacts:
	rm -rf artifacts/*

clean-all: clean clean-artifacts
