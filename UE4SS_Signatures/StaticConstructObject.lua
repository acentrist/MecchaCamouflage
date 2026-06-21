function Register()
    return "4C 8B DC 55 53 41 56 49 8D AB 28 FE FF FF 48 81 EC C0 02 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 A0 01 00 00 8B 41 70 33 DB 49 89 73 10 49 89 7B 18 48 8B F9 4D 89 63 20 44 8B 61 18"
end

function OnMatchFound(MatchAddress)
    return MatchAddress
end
