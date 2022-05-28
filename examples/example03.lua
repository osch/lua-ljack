local format   = string.format
local nocurses = require("nocurses")  -- https://github.com/osch/lua-nocurses
local mtmsg    = require("mtmsg")     -- https://github.com/osch/lua-mtmsg
local ljack    = require("ljack")

----------------------------------------------------------------------------------------------------

local client     = ljack.client_open("example03.lua")
local myMidiPort = client:port_register("midi_in", "MIDI", "IN")
local midiBuffer = mtmsg.newbuffer()

midiBuffer:notifier(nocurses) -- notify nocurses in case of new messages
client:activate()

----------------------------------------------------------------------------------------------------

local function printbold(...)
    nocurses.setfontbold(true)
    print(...)
    nocurses.resetcolors()
end

local function printred(...)
    nocurses.setfontbold(true)
    nocurses.setfontcolor("RED")
    print(...)
    nocurses.resetcolors()
end

----------------------------------------------------------------------------------------------------

local function choosePort(type, direction)
    local list = client:get_ports(".*", type, direction)
    printbold(format("%s %s Ports:", type, direction))
    for i, p in ipairs(list) do
        print(i, p)
    end
    if #list == 0 then
        print("No ports found")
        os.exit()
    end
    while true do
        io.write(format("Choose %s Port (1-%d or none): ", direction, #list))
        local inp = io.read()
        if inp == "q" then os.exit() end
        if inp ~= "" then
            local p = list[tonumber(inp)]
            if p then
                print()
                return p
            end
        else
            return
        end
    end
end

----------------------------------------------------------------------------------------------------

local otherMidiPort = choosePort("MIDI", "OUT")
if otherMidiPort then
    myMidiPort:connect(otherMidiPort)
end

local receiver = ljack.new_midi_receiver(myMidiPort, midiBuffer)
receiver:activate()

----------------------------------------------------------------------------------------------------

local function parseMidiEvent(event)
    local b0, b1, b2 = event:byte(1, 3)
    local status  = math.floor(b0 / 0x10)
    local channel = b0 % 0x10
    local v1, v2
    if 0x8 <= status  and status <= 0xB then
        v1 = b1
        v2 = b2
    elseif 0xC <= status and status <= 0xD then
        v1 = b1
    elseif 0xE == status then
        v1= (b1 + 128*b2) - 0x2000
    elseif 0xF == status then
        v1 = b1
    end
    return status, channel, v1, v2
end

local statusToString = {
    [0x8] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d %3d", t, c, "Note Off", v1, v2) end,
    [0x9] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d %3d", t, c, "Note On",  v1, v2) end,
    [0xA] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d %3d", t, c, "Poly Aftertouch",  v1, v2) end,
    [0xB] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d %3d", t, c, "Control Change",  v1, v2) end,

    [0xC] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d",     t, c, "Program Change",  v1) end,
    [0xD] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d",     t, c, "Chan Aftertouch",  v1) end,
    [0xE] = function(t, c, v1, v2) return format("%11d %2d %-15s %7d",     t, c, "Pitch Bend",  v1) end,
    [0xF] = function(t, c, v1, v2) return format("%11d %2d %-15s %3d",     t, c, "System Message",  v1) end,
}

----------------------------------------------------------------------------------------------------

printbold("Monitoring midi events... (Press <Q> to Quit)")

while true do
    local c = nocurses.getch() -- returns nil if new messages in midiBuffer
    repeat
        local frameTime, event = midiBuffer:nextmsg(0)
        if frameTime then
            local status, channel, v1, v2 = parseMidiEvent(event)
            local toString = statusToString[status]
            if toString then
                print(toString(frameTime, channel + 1, v1, v2))
            end
        end
    until not frameTime
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            break
        end
    end
end
