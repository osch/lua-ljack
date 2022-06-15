----------------------------------------------------------------------------------------------------
--[[
     This example demonstrates how an audio stream can be recorded and replayed using  
     [Auproc audio receiver objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_receiver)
     and 
     [Auproc audio sender objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_sender).
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

local client = ljack.client_open("example08.lua")
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

local myInPort = client:port_register("audio-in", "AUDIO", "IN")
local otherOut = choosePort("AUDIO", "OUT")
print("Connecting my AUDIO IN to", otherOut)
if otherOut then
    myInPort:connect(otherOut)
end
local myOutPort = client:port_register("audio-out", "AUDIO", "OUT")
local otherIn = client:get_ports(nil, "AUDIO", "IN")
print("Connecting my AUDIO OUT to", otherIn[1], otherIn[2])
if otherIn[1] then
    myOutPort:connect(otherIn[1])
end
if otherIn[2] then
    myOutPort:connect(otherIn[2])
end


local received  = mtmsg.newbuffer()
local sendQueue = mtmsg.newbuffer()

received:notifier(nocurses, ">")
local qlength = 3
sendQueue:notifier(nocurses, "<", qlength)

local receiver = auproc.new_audio_receiver(myInPort, received)

local sender = auproc.new_audio_sender(myOutPort, sendQueue)


----------------------------------------------------------------------------------------------------

local rate    = client:get_sample_rate()
local bufsize = client:get_buffer_size()

local t0 = nil
local t  = nil
local allSamples = {}

local quit = false

printbold("Press <Space> to replay recording, <Q> to quit")
print("Recording...")

receiver:activate()

while not quit do
    repeat
        local frameTime, samples = received:nextmsg(0)
        if frameTime then
            if not t0 then
                t0 = frameTime
                t = t0
            end
            allSamples[#allSamples + 1] = samples
            t = t + samples:len()
        end
    until not frameTime
    local c = nocurses.getch()
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            quit = true
        elseif c == " " then
            printbold(format("Recording finished (%8.3f secs).", (t - t0)/rate))
            break
        end
    end
end
receiver:deactivate()

print("Playing...")

local i    = 1
local imax = #allSamples
sender:activate()

while not quit do
    while sendQueue:msgcnt() < qlength and i <= imax do
        local samples = allSamples[i]
        sendQueue:addmsg(samples)
        i = i + 1
        t = t + samples:len()
    end
    if i > imax then
        printbold("Finished.")
        break
    end
    local c = nocurses.getch()
    if c then
        c = string.char(c)
        if c == "Q" or c == "q" then
            printbold("Quit.")
            quit = true
        end
    end
end
----------------------------------------------------------------------------------------------------
