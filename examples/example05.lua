----------------------------------------------------------------------------------------------------

local nocurses = require("nocurses") -- https://github.com/osch/lua-nocurses
local carray   = require("carray")   -- https://github.com/osch/lua-carray
local mtmsg    = require("mtmsg")    -- https://github.com/osch/lua-mtmsg
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

local client = ljack.client_open("example05.lua")
client:activate()

local myPort     = client:port_register("audio-out", "AUDIO", "OUT")
local otherPorts = client:get_ports(nil, "AUDIO", "IN")
print("Connecting to", otherPorts[1], otherPorts[2])
myPort:connect(otherPorts[1])
myPort:connect(otherPorts[2])

local queue   = mtmsg.newbuffer()
local qlength = 3
queue:notifier(nocurses, "<", qlength)

local sender = ljack.new_audio_sender(myPort, queue)

local buflen  = client:get_buffer_size()
local rate    = client:get_sample_rate()
local samples = carray.new("float", buflen)

----------------------------------------------------------------------------------------------------

local f1, f2, v1, v2
local frameTime0, frameTime

local function reset()
    f1 = 440
    f2 = 0.2
    v1 = 0.05
    v2 = 0.1
    frameTime0 = client:frame_time()
    frameTime  = frameTime0
    sender:activate()
end
reset()

----------------------------------------------------------------------------------------------------

local function printValues()
    print(format("v1: %8.2f, f1: %8.2f, v2: %8.2f, f2: %8.2f", v1, f1, v2, f2))
end
local function printHelp()
    printbold("Press keys v,V,b,B,f,F,g,G,h,H,r,p for modifying parameters, q for Quit")
end
printHelp()
printValues()

----------------------------------------------------------------------------------------------------

sender:activate()

----------------------------------------------------------------------------------------------------

while true do
    while queue:msgcnt() < qlength do
        for i = 1, buflen do 
            local t = (frameTime - frameTime0 + i)/rate
            samples:set(i, v1*sin(t*(f1+v2*sin(t*f2*2*pi))*2*pi))
        end
        queue:addmsg(frameTime, samples)
        frameTime = frameTime + buflen
    end
    local c = nocurses.getch()
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            break

        elseif c == "F" then
            f1 = f1 + 20
        elseif c == "f" then
            f1 = f1 - 20

        elseif c == "G" then
            f2 = f2 + 0.01
        elseif c == "g" then
            f2 = f2 - 0.01

        elseif c == "H" then
            f2 = f2 + 0.1
        elseif c == "h" then
            f2 = f2 - 0.1

        elseif c == "V" then
            v1 = v1 + 0.01
        elseif c == "v" then
            v1 = v1 - 0.01
        
        elseif c == "B" then
            v2 = v2 + 0.01
        elseif c == "b" then
            v2 = v2 - 0.01
        
        elseif c == 'p' then
            if sender:active() then
                sender:deactivate()
            else
                queue:clear()
                sender:activate()
                local dt = frameTime - frameTime0
                frameTime  = client:frame_time()
                frameTime0 = frameTime - dt
            end
        elseif c == 'r' then
            reset()
        else
            printHelp()
        end
        printValues()
    end
end

----------------------------------------------------------------------------------------------------
