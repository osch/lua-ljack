----------------------------------------------------------------------------------------------------

local nocurses = require("nocurses") -- https://luarocks.org/modules/osch/nocurses
local mtmsg    = require("mtmsg")    -- https://luarocks.org/modules/osch/mtmsg
local ljack    = require("ljack")

----------------------------------------------------------------------------------------------------

local format   = string.format
local function printbold(...) nocurses.setfontbold(true) print(...) nocurses.resetcolors() end

----------------------------------------------------------------------------------------------------

local MY_CHANNEL    = 1
local OTHER_CHANNEL = 2

----------------------------------------------------------------------------------------------------

local function printHelp()
    printbold(format("Press keys c,d,e,f,g,a,b,C for playing MIDI notes on channel %s, q for Quit", MY_CHANNEL))
    print(format("MIDI events on incoming port for channel %s are duplicated to channel %s, "..
                 "all other MIDI events on incoming port are mapped to channel %s.", OTHER_CHANNEL, MY_CHANNEL, OTHER_CHANNEL))
end

----------------------------------------------------------------------------------------------------

local client = ljack.client_open("example07.lua")

client:activate()

----------------------------------------------------------------------------------------------------

local function choosePort(type, direction)
    local list = client:get_ports(".*", type, direction)
    printbold(format("Connect my %s %s port with: ", type, direction == "IN" and "OUT" or "IN"))
    for i, p in ipairs(list) do
        print(i, p)
    end
    if #list == 0 then
        print("No ports found")
        os.exit()
    end
    while true do
        io.write(format("Choose (1-%d or none): ", #list))
        local inp = io.read()
        if inp == "q" then os.exit() end
        if inp ~= "" then
            local p = list[tonumber(inp)]
            if p then
                print()
                return p
            end
        else
            print()
            return
        end
    end
end
----------------------------------------------------------------------------------------------------

local otherMidiOutPort = choosePort("MIDI", "OUT")
local otherMidiInPort  = choosePort("MIDI", "IN")

local myMidiInPort   = client:port_register("midi_in",  "MIDI", "IN")
local myMidiOutPort  = client:port_register("midi_out", "MIDI", "OUT")

if otherMidiOutPort then
    myMidiInPort:connect(otherMidiOutPort)
end
if otherMidiInPort then
    myMidiOutPort:connect(otherMidiInPort)
end

----------------------------------------------------------------------------------------------------

local midiSenderCtrl = mtmsg.newbuffer()
local midiSenderOut  = client:new_process_buffer("MIDI")
local midiSender     = ljack.new_midi_sender(midiSenderOut, midiSenderCtrl)

local midiMixerCtrl = mtmsg.newbuffer() 
local midiMixer     = ljack.new_midi_mixer(myMidiInPort,  -- input1: my MIDI IN port
                                           myMidiInPort,  -- input2: duplicate MIDI IN port
                                           midiSenderOut, -- input3: internal process buffer
                                           myMidiOutPort, -- output: my MIDI OUT port
                                           midiMixerCtrl)

for c = 1, 16 do
    midiMixerCtrl:addmsg(1, c, OTHER_CHANNEL) -- input1: map   channel c -> OTHER_CHANNEL
    midiMixerCtrl:addmsg(2, c, 0)             -- input2: block channel c
end
midiMixerCtrl:addmsg(2, OTHER_CHANNEL, MY_CHANNEL) -- input2: map OTHER_CHANNEL -> MY_CHANNEL

----------------------------------------------------------------------------------------------------

midiSender:activate()
midiMixer:activate()

----------------------------------------------------------------------------------------------------

local notes = {
    c = 60,
    d = 62,
    e = 64,
    f = 65,
    g = 67,
    a = 69,
    b = 71,
    C = 72
}

local NOTE_OFF = 0x8
local NOTE_ON  = 0x9

local function playNote(n)
    local a1, a2, a3 = (NOTE_ON  * 0x10 + (MY_CHANNEL - 1)), n, 127
    local b1, b2, b3 = (NOTE_OFF * 0x10 + (MY_CHANNEL - 1)), n, 0
    midiSenderCtrl:addmsg(string.char(a1, a2, a3))
    midiSenderCtrl:addmsg(string.char(b1, b2, b3))
end

----------------------------------------------------------------------------------------------------

printHelp()

while true do
    local c = nocurses.getch()
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            break
        else
            local n = notes[c]
            if n then
                playNote(n)
                print(format("playing note %s", c))
            else
                printHelp()
            end
        end
    end
end

----------------------------------------------------------------------------------------------------
