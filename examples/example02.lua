local nocurses = require("nocurses") -- https://github.com/osch/lua-nocurses
local mtmsg    = require("mtmsg")    -- https://github.com/osch/lua-mtmsg
local ljack    = require("ljack")

----------------------------------------------------------------------------------------------------

local jackInfo = mtmsg.newbuffer() -- jack status callback messages

jackInfo:notifier(nocurses) -- notify nocurses in case of new messages

local client = ljack.client_open("example02.lua", jackInfo)

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

local p1 = choosePort("MIDI", "OUT")
local p2 = choosePort("MIDI", "IN")

----------------------------------------------------------------------------------------------------

if p1 and p2 then

    client:activate()
    print(string.format("Connecting %q\n"..
                        "      with %q.", p1, p2))

    local isConnected = client:connect(p1, p2)
    if not isConnected then
        printred("Connection failed")
    end

    local function handleJackInfo(event, ...)
        if event == "PortConnect" then
            local id1, id2, connected = ...
            if not connected
               and client:port_by_id(id1):name() == p1
               and client:port_by_id(id2):name() == p2
            then
                isConnected = client:connect(p1, p2)
                if isConnected then
                    print("Reconnected ports.")
                else
                    printred("Connection lost.")
                end
            end
        elseif event == "GraphOrder" and not isConnected then
            isConnected = client:connect(p1, p2)
            if isConnected then
                print("Reconnected ports.")
            end
        elseif event == "Shutdown" then
            printbold("Jack server shutdown.")
            os.exit()
        end
        return event
    end

    printbold("Observing connection... (Press <Q> to Quit)")
        
    while true do
        local c = nocurses.getch() -- returns for new messages in jackInfo
        repeat
            local hadEvent = handleJackInfo(jackInfo:nextmsg(0)) -- non-blocking read
        until not hadEvent
        if c then
            c = string.char(c)
            if c == "Q" or c == "q" then
                printbold("Quit.")
                break
            end
        end
    end
end

