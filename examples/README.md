# LJACK Examples
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example01.lua`](./example01.lua)
     
     This example lists all JACK ports and connects the first MIDI OUT port with
     the first MIDI IN port if these are available.
       
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example02.lua`](./example02.lua)
     
     This example lets the user select a MIDI OUT and a MIDI IN port which are connected and
     observed afterwards. The example reconnects if the connection is disconnected or
     if one of the ports disappears and reappears again.
     
     The example uses [lua-nocurses](https://github.com/osch/lua-nocurses) for user
     interaction and [lua-mtmsg](https://github.com/osch/lua-mtmsg) as receiver for
     JACK status events, see also [Status Messages](https://github.com/osch/lua-ljack/blob/master/doc/README.md#status-messages).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example03.lua`](./example03.lua)
     
     A simple midi monitor. This example demonstrates the usage of a 
     [midi receiver object](https://github.com/osch/lua-ljack/blob/master/doc/README.md#ljack_new_midi_receiver).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example04.lua`](./example04.lua)
     
     This example sends MIDI Note On/Off events. This example demonstrates the usage of a 
     [midi sender object](https://github.com/osch/lua-ljack/blob/master/doc/README.md#ljack_new_midi_sender).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example05.lua`](./example05.lua)
     
     This example generates audio samples from Lua script code on the main thread. 
     This example demonstrates the usage of a 
     [audio sender object](https://github.com/osch/lua-ljack/blob/master/doc/README.md#ljack_new_audio_sender).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example06.lua`](./example06.lua)
     
     This example generates two sounds from Lua script code on the main thread and lets
     the user control balance and volume of each sound.
     This example demonstrates how 
     [audio sender objects](https://github.com/osch/lua-ljack/blob/master/doc/README.md#ljack_new_audio_sender)
     and
     [audio mixer objects](https://github.com/osch/lua-ljack/blob/master/doc/README.md#ljack_new_audio_mixer)
     can be connected using [process buffer objects](https://github.com/osch/lua-ljack/blob/master/doc/README.md#client_new_process_buffer)
     
<!-- ---------------------------------------------------------------------------------------- -->
