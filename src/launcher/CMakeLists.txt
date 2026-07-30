# Phase 3 replaces this contract target with the native launcher executable.
# Keeping it artifact-free prevents an incomplete launcher from being shipped.
add_library(meccha_launcher_contract INTERFACE)
add_library(MecchaCamouflage::LauncherContract ALIAS meccha_launcher_contract)

target_link_libraries(
    meccha_launcher_contract
    INTERFACE
        meccha_build_identity
        meccha_core
)
