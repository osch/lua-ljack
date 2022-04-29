local format   = string.format
local nocurses = require("nocurses")  -- https://github.com/osch/lua-nocurses
local mtmsg    = require("mtmsg")     -- https://github.com/osch/lua-mtmsg
local ljack    = require("ljack")

----------------------------------------------------------------------------------------------------

local client     = ljack.client_open("example04.lua")
local myMidiPort = client:port_register("midi_out", "MIDI", "OUT")
local midiBuffer = mtmsg.newbuffer()

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
    printbold(string.format("%s %s Ports:", type, direction))
    for i, p in ipairs(list) do
        print(i, p)
    end
    if #list == 0 then
        print("No ports found")
        os.exit()
    end
    while true do
        io.write(string.format("Choose %s Port (1-%d): ", direction, #list))
        local inp = io.read()
        if inp == "q" then os.exit() end
        local p = list[tonumber(inp)]
        if p then
            print()
            return p
        end
    end
end

----------------------------------------------------------------------------------------------------

local otherMidiPort = choosePort("MIDI", "IN")
myMidiPort:connect(otherMidiPort)

local sender = ljack.new_midi_sender(myMidiPort, midiBuffer)
sender:activate()

----------------------------------------------------------------------------------------------------

local CHANNEL  = 1
local NOTE_OFF = 0x8
local NOTE_ON  = 0x9

local midi_events = {
    -- seconds, command 
    { 1.000, NOTE_ON,  60, 127},
    { 2.000, NOTE_OFF, 60,   0},
}

local PERIOD = 4.000 -- seconds

----------------------------------------------------------------------------------------------------

printbold("Generating midi events... (Press <Q> to Quit)")

----------------------------------------------------------------------------------------------------

local t0 = client:get_time() -- microseconds
local t1 = math.floor(t0 / 1000000) * 1000000

while true do
    local timeout = (t1 - client:get_time())/1000000
    local c = nocurses.getch(timeout)
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            break
        end
    end
    local t = client:get_time()
    if t >= t1 then
        for _, e in ipairs(midi_events) do
            local t = t1 + e[1]*1000000
            local b1, b2, b3 = (e[2] * 0x10 + (CHANNEL - 1)), e[3], e[4]
            print(format("%8.3f: 0x%02X %3d %3d", t/1000000, b1, b2, b3))
            midiBuffer:addmsg(t, string.char(b1, b2, b3))
        end
        t1 = t1 + PERIOD * 1000000
    end
end

----------------------------------------------------------------------------------------------------
