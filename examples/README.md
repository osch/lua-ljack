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
