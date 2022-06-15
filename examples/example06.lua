----------------------------------------------------------------------------------------------------
--[[
     This example generates two sounds from Lua script code on the main thread and lets
     the user control balance and volume of each sound.
     This example demonstrates how 
     [Auproc audio sender objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_sender)
     and
     [Auproc audio mixer objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_mixer)
     can be connected using [AUDIO process buffer objects](https://github.com/osch/lua-ljack/blob/master/doc/README.md#client_new_process_buffer)
--]]
----------------------------------------------------------------------------------------------------

local nocurses = require("nocurses") -- https://github.com/osch/lua-nocurses
local carray   = require("carray")   -- https://github.com/osch/lua-carray
local mtmsg    = require("mtmsg")    -- https://github.com/osch/lua-mtmsg
local auproc   = require("auproc")   -- https://github.com/osch/lua-auproc
local ljack    = require("ljack")

----------------------------------------------------------------------------------------------------

local pi       = math.pi
local sin      = math.sin
local format   = string.format

local function printbold(...)
    nocurses.setfontbold(true)
    print(...)
    nocurses.resetcolors()
end

----------------------------------------------------------------------------------------------------

local client = ljack.client_open("example06.lua")
client:activate()

local myPort1    = client:port_register("audio-out1", "AUDIO", "OUT")
local myPort2    = client:port_register("audio-out2", "AUDIO", "OUT")
local otherPorts = client:get_ports(nil, "AUDIO", "IN")
print("Connecting to", otherPorts[1], otherPorts[2])
myPort1:connect(otherPorts[1])
myPort2:connect(otherPorts[2])

----------------------------------------------------------------------------------------------------

local sound1  = client:new_process_buffer()
local sound2  = client:new_process_buffer()

local queue1   = mtmsg.newbuffer()
local queue2   = mtmsg.newbuffer()
local qlength = 3
queue1:notifier(nocurses, "<", qlength)
queue2:notifier(nocurses, "<", qlength)

----------------------------------------------------------------------------------------------------

local sender1 = auproc.new_audio_sender(sound1, queue1)
local sender2 = auproc.new_audio_sender(sound2, queue2)

----------------------------------------------------------------------------------------------------

local mix1ctrl = mtmsg.newbuffer()
local mixer1   = auproc.new_audio_mixer(sound1, sound2, myPort1, mix1ctrl)

local mix2ctrl = mtmsg.newbuffer()
local mixer2   = auproc.new_audio_mixer(sound1, sound2, myPort2, mix2ctrl)

----------------------------------------------------------------------------------------------------

local buflen  = client:get_buffer_size()
local rate    = client:get_sample_rate()
local samples = carray.new("float", buflen)

local frameTime1 = client:frame_time()
local frameTime2 = frameTime1

----------------------------------------------------------------------------------------------------

local bal1 = -1
local bal2 =  1
local vol1 = 0.1
local vol2 = 0.1

function setBalance(sound, bal)
    mix1ctrl:addmsg(sound, 0.5 - 0.5*bal)
    mix2ctrl:addmsg(sound, 0.5 + 0.5*bal)
end

setBalance(1, bal1)
setBalance(2, bal2)

----------------------------------------------------------------------------------------------------

local function printValues()
    print(format("vol1: %8.2f, bal1: %8.2f, vol2: %8.2f, bal2: %8.2f", vol1, bal1, vol2, bal2))
end
local function printHelp()
    printbold("Press keys s,d,x,c,h,j,n,m for modifying parameters, q for Quit")
end
printHelp()
printValues()

----------------------------------------------------------------------------------------------------

sender1:activate()
sender2:activate()

mixer1:activate()
mixer2:activate()

----------------------------------------------------------------------------------------------------

while true do
    while queue1:msgcnt() < qlength do
        for i = 1, buflen do 
            local t = (frameTime1 + i)/rate
            samples:set(i, vol1*sin(t*440*2*pi))
        end
        queue1:addmsg(frameTime1, samples)
        frameTime1 = frameTime1 + buflen
    end
    while queue2:msgcnt() < qlength do
        for i = 1, buflen do 
            local t = (frameTime2 + i)/rate
            samples:set(i, vol2*sin(t*435*2*pi))
        end
        queue2:addmsg(frameTime2, samples)
        frameTime2 = frameTime2 + buflen
    end
    local c = nocurses.getch()
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            break
        elseif c == "s" then
            vol1 = vol1 - 0.1
            printValues()
        elseif c == "d" then
            vol1 = vol1 + 0.1
            printValues()
        elseif c == "h" then
            vol2 = vol2 - 0.1
            printValues()
        elseif c == "j" then
            vol2 = vol2 + 0.1
            printValues()
        elseif c == "x" then
            if bal1 > -1 then
                bal1 = bal1 - 0.1
                if bal1 < -1 then bal1 = -1 end
                setBalance(1, bal1)
            end
            printValues()
        elseif c == "c" then
            if bal1 < 1 then
                bal1 = bal1 + 0.1
                if bal1 > 1 then bal1 = 1 end
                setBalance(1, bal1)
            end
            printValues()
        elseif c == "n" then
            if bal2 > -1 then
                bal2 = bal2 - 0.1
                if bal2 < -1 then bal2 = -1 end
                setBalance(2, bal2)
            end
            printValues()
        elseif c == "m" then
            if bal2 < 1 then
                bal2 = bal2 + 0.1
                if bal2 > 1 then bal2 = 1 end
                setBalance(2, bal2)
            end
            printValues()
        else
            printHelp()
        end
    end
end

