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

local function raster(f)
    -- round up frame time to mulitple of 100
    return math.floor(f / 100) * 100 + 100
end
local rate = client:get_sample_rate()

local t0 = client:frame_time()
local t1 = raster(t0)

while true do
    local timeout = (t1 - client:frame_time())/rate
    local c = nocurses.getch(timeout)
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            break
        end
    end
    local t = client:frame_time()
    if t >= t1 then
        for _, e in ipairs(midi_events) do
            local t = raster(t1 + e[1]*rate)
            local b1, b2, b3 = (e[2] * 0x10 + (CHANNEL - 1)), e[3], e[4]
            print(format("%11d: 0x%02X %3d %3d", t, b1, b2, b3))
            midiBuffer:addmsg(t, string.char(b1, b2, b3))
        end
        t1 = raster(t1 + PERIOD * rate)
    end
end

----------------------------------------------------------------------------------------------------
