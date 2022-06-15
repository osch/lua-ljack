# LJACK Examples

<!-- ---------------------------------------------------------------------------------------- -->

Most of the examples are using procesor objects that are provided by the
[lua-auproc](https://github.com/osch/lua-auproc) package.

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
     [Auproc midi receiver object](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_midi_receiver).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example04.lua`](./example04.lua)
     
     This example sends MIDI Note On/Off events. This example demonstrates the usage of a 
     [Auproc midi sender object](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_midi_sender).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example05.lua`](./example05.lua)
     
     This example generates audio samples from Lua script code on the main thread. 
     This example demonstrates the usage of a 
     [Auproc audio sender object](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_sender).
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example06.lua`](./example06.lua)
     
     This example generates two sounds from Lua script code on the main thread and lets
     the user control balance and volume of each sound.
     This example demonstrates how 
     [Auproc audio sender objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_sender)
     and
     [Auproc audio mixer objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_mixer)
     can be connected using [AUDIO process buffer objects](https://github.com/osch/lua-ljack/blob/master/doc/README.md#client_new_process_buffer)
     
<!-- ---------------------------------------------------------------------------------------- -->

   * [`example07.lua`](./example07.lua)
     
     This example demonstrates how 
     [Auproc midi sender objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_midi_sender)
     and
     [Auproc midi mixer objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_midi_mixer)
     can be connected using [MIDI process buffer objects](https://github.com/osch/lua-ljack/blob/master/doc/README.md#client_new_process_buffer)

<!-- ---------------------------------------------------------------------------------------- -->

   * [`example08.lua`](./example08.lua)
     
     This example demonstrates how an audio stream can be recorded and replayed using  
     [Auproc audio receiver objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_receiver)
     and 
     [Auproc audio sender objects](https://github.com/osch/lua-auproc/blob/master/doc/README.md#auproc_new_audio_sender).

<!-- ---------------------------------------------------------------------------------------- -->
