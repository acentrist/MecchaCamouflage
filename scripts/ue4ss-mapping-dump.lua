-- Loaded only by the temporary UE4SS mapping-dump setup.
-- Wait until the game thread is available before producing Mappings.usmap.
local has_dumped = false

ExecuteInGameThreadWithDelay(10000, function()
    if has_dumped then
        return
    end

    has_dumped = true
    print("[MecchaCamouflage] Dumping Mappings.usmap for the current game build.")
    DumpUSMAP()
end)
