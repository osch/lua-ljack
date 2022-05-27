# LJACK 
[![Licence](http://img.shields.io/badge/Licence-MIT-brightgreen.svg)](LICENSE)
[![Install](https://img.shields.io/badge/Install-LuaRocks-brightgreen.svg)](https://luarocks.org/modules/osch/ljack)

<!-- ---------------------------------------------------------------------------------------- -->

Lua binding for the [JACK Audio Connection Kit](https://jackaudio.org/).

This binding enables [Lua] scripting code to registrate ports and to manage port
connections and Lua audio processor objects for the JACK Audio Connection Kit. 
Realtime audio processing of [Lua processor objects](./doc/README.md#processor-objects) 
has to be implemented in native C code. 

[Lua]:          https://www.lua.org

<!-- ---------------------------------------------------------------------------------------- -->

#### Further reading:
   * [Documentation](./doc/README.md#ljack-documentation)
   * [Examples](./examples/README.md#ljack-examples)

<!-- ---------------------------------------------------------------------------------------- -->

## First Example

* This example lists all JACK ports and connects the first MIDI OUT port with
  the first MIDI IN port if these are available:
  
    ```lua
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
    ```

<!-- ---------------------------------------------------------------------------------------- -->
