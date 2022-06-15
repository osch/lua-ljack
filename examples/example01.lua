----------------------------------------------------------------------------------------------------
--[[
     This example lists all JACK ports and connects the first MIDI OUT port with
     the first MIDI IN port if these are available.
--]]
----------------------------------------------------------------------------------------------------

local ljack = require("ljack")

local client = ljack.client_open("example01.lua")

local function listPorts(type, direction)
    local list = client:get_ports(".*", type, direction)
    print("Ports", type, direction)
    for _, p in ipairs(list) do
        print("     ", p)
    end
    print()
    return list
end

local audioOutList = listPorts("AUDIO", "OUT")
local audioInList  = listPorts("AUDIO", "IN")

local midiOutList = listPorts("MIDI", "OUT")
local midiInList  = listPorts("MIDI", "IN")

if #midiInList > 0 and #midiOutList > 0 then
    local p1, p2 = midiOutList[1], midiInList[1]
    print(string.format("Connecting %q\n"..
                        "      with %q...", p1, p2))
    client:connect(p1, p2)
end
