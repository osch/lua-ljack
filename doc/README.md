# LJACK Documentation

<!-- ---------------------------------------------------------------------------------------- -->
##   Contents
<!-- ---------------------------------------------------------------------------------------- -->

   * [Overview](#overview)
   * [Module Functions](#module-functions)
        * [ljack.client_open()](#ljack_client_open)
        * [ljack.set_error_log()](#ljack_set_error_log)
        * [ljack.set_info_log()](#ljack_set_info_log)
        * [ljack.client_name_size()](#ljack_client_name_size)
        * [ljack.port_name_size()](#ljack_port_name_size)
   * [Client Methods](#client-methods)
        * [client:name()](#client_name)
        * [client:activate()](#client_activate)
        * [client:deactivate()](#client_deactivate)
        * [client:close()](#client_close)
        * [client:port_register()](#client_port_register)
        * [client:connect()](#client_connect)
        * [client:disconnect()](#client_disconnect)
        * [client:get_ports()](#client_get_ports)
        * [client:port_name()](#client_port_name)
        * [client:port_by_id()](#client_port_by_id)
        * [client:port_by_name()](#client_port_by_name)
        * [client:get_time()](#client_get_time)
        * [client:frame_time()](#client_frame_time)
        * [client:get_sample_rate()](#client_get_sample_rate)
        * [client:get_buffer_size()](#client_get_buffer_size)
        * [client:set_buffer_size()](#client_set_buffer_size)
        * [client:cpu_load()](#client_cpu_load)
        * [client:new_process_buffer()](#client_new_process_buffer)
   * [Port Methods](#port-methods)
        * [port:unregister()](#port_unregister)
        * [port:get_client()](#port_get_client)
        * [port:is_mine()](#port_is_mine)
        * [port:is_input()](#port_is_input)
        * [port:is_output()](#port_is_output)
        * [port:is_midi()](#port_is_midi)
        * [port:is_audio()](#port_is_audio)
        * [port:get_connections()](#port_get_connections)
   * [Connector Objects](#connector-objects)
   * [Processor Objects](#processor-objects)
   * [Status messages](#status-messages)
        * [ClientRegistration](#ClientRegistration)
        * [GraphOrder](#GraphOrder)
        * [PortConnect](#PortConnect)
        * [PortRegistration](#PortRegistration)
        * [PortRename](#PortRename)
        * [BufferSize](#BufferSize)
        * [Shutdown](#Shutdown)
        * [XRun](#XRun)

<!-- ---------------------------------------------------------------------------------------- -->
##   Overview
<!-- ---------------------------------------------------------------------------------------- -->
   
LJACK is a Lua binding for the [JACK Audio Connection Kit](https://jackaudio.org/).

This binding enables Lua scripting code to registrate ports and to manage port
connections and Lua audio processor objects for the JACK Audio Connection Kit. 
Realtime audio processing of the Lua [processor objects](#processor-objects) has 
to be implemented in native C using the [Auproc C API].

<!-- ---------------------------------------------------------------------------------------- -->
##   Module Functions
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="ljack_client_open">**`  ljack.client_open(name[, statusReceiver])
  `**</a>
  
  Creates a new JACK client object with the given name. A client object is used to create
  and manage JACK audio or midi ports. Normally you would only create one client object 
  for an application.
  
  * *name* - mandatory string, client name of at most 
             [ljack.client_name_size()](#jack_client_name_size) characters.
    
  * *statusReceiver* - optional object that implements the [Receiver C API]. This object
                       receivers asynchronous status messages, 
                       see [Status messages](#status-messages).

  The created client object is subject to garbage collection. If the client object
  is garbage collected, all ports that are belonging to this client are closed and
  disconnected.
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="ljack_set_error_log">**`  ljack.set_error_log(arg)
  `**</a>

  Sets how JACK error messages are logged.

  * *arg* - this must be one of the string values *"SILENT"*, *"STDOUT"*, *"STDERR"* or it
            must be an object that implements the [Receiver C API].
 
  If *arg* is a string, the messages are written to *stdout* or *stderr* according to the
  value of this string or are not written at all if the value is *"SILENT"*.
  
  If *arg* is an object implementing the [Receiver C API], the JACK error message is send to 
  this receiver.
 
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="ljack_set_info_log">**`  ljack.set_info_log(arg)
  `**</a>

  Sets how JACK info messages are logged.

  See [ljack.set_error_log(arg)](#ljack_set_error_log).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="ljack_client_name_size">**`  ljack.client_name_size()
  `**</a>

  Returns the maximum number of characters in a JACK client name. This value is a constant.
  

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="ljack_port_name_size">**`  ljack.port_name_size()
  `**</a>

  Returns the maximum number of characters in a full JACK port name. This value is a constant.

  A port's full name contains the owning client name concatenated with a colon (:) followed 
  by its short name.


<!-- ---------------------------------------------------------------------------------------- -->
##   Client Methods
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_name">**`      client:name()
  `** </a>
  
  Returns the actual client name. The actual name will differ from the name given
  in [ljack.client_open()](#ljack_client_open) if there was another JACK client with
  this name. 
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_activate">**`      client:activate()
  `** </a>
  
  Tell the Jack server that the program is ready to start processing audio.
  
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_deactivate">**`    client:deactivate()
  `** </a>
  
  Tell the Jack server to remove this client from the process graph. Also,
  disconnect all ports belonging to it, since inactive clients have no port
  connections. The client may be activated again afterwards.
  
<!-- ---------------------------------------------------------------------------------------- -->
* <a id="client_close">**`         client:close()
  `** </a>
  
  Closes and disconnects all ports that are belonging to this client. The client object 
  becomes invalid and cannot be used furthermore.
  
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_port_register">**`       client:port_register(name, type, direction)
  `** </a>

  Returns a new port object for the client.

  Each port has a short name. The port's full name contains the name of the
  client concatenated with a colon (:) followed by its short name. The
  [ljack.port_name_size()](#ljack_port_name_size) is the maximum length of this full name.
  Exceeding that  will cause the port registration to fail.

  The port name must be unique among all ports owned by this client. If the name
  is not unique, the registration will fail.

  * *name*      - string, short port name
  
  * *type*      - string, one of the possible values *"AUDIO"* or *"MIDI"*.
  
  * *direction* - string, one of the possible values *"IN"* or *"OUT"*.
  
  The created port object is subject to garbage collection. If the port object
  is garbage collected, all connections belonging to this port are closed.

  The created port object can be used as connector for [processor 
  objects](#processor-objects). A port of type IN can be used as input connector 
  by multiple processor objects whereas a port of type OUT can
  only be used by one processor object as output connector.

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_connect">**`       client:connect(sourcePort, destinationPort)
  `** </a>
  
  Establish a connection between two ports. When a connection exists, data
  written to the source port will be available to be read at the destination
  port.
  
  * *sourcePort*       - port name or port object taken as source, i.e. 
                         this must be an output port
  
  * *destinationPort*  - port name or port object taken as destination, i.e. 
                         this must be an input port

  The type of the given ports (*"AUDIO"* or *"MIDI"*) must be the same for both ports.
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_disconnect">**`    client:disconnect(sourcePort, destinationPort)
  `** </a>

  Disconnects the ports.
    
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_get_ports">**`     client:get_ports([portNamePattern[, typeName[, direction]]])
  `** </a>
  
  Returns a list of port names matching the given criteria.
  
  * *portNamePattern* - optional string, a regular expression used to select ports by name.
  
  * *typeName*  - optional string, one of the possible values *"AUDIO"* or *"MIDI"*.
  
  * *direction* - optional string, one of the possible values *"IN"* or *"OUT"*.
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_port_name">**`     client:port_name(id)
  `** </a>

  Returns the port name as string value for the given port ID. 
  Port IDs are given as arguments in the [status messages](#status-messages).
  
  * *id* - integer value

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_port_by_id">**`    client:port_by_id(id)
  `** </a>

  Returns a port object for the given port ID. Port IDs are given as arguments in the 
  [status messages](#status-messages).
  
  * *id* - integer value

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_port_by_name">**`  client:port_by_name(name)
  `** </a>

  Returns a port object for the given full port name.
  
  * *name* - string, full port name

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_get_time">**`      client:get_time()
  `** </a>

  Returns JACK's current system time in microseconds as integer value, using the 
  JACK clock source.

  The value returned is guaranteed to be monotonic, but not linear. 
  

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_frame_time">**`      client:frame_time()
  `** </a>

  Returns the estimated current time in frames.

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_get_sample_rate">**`      client:get_sample_rate()
  `** </a>

  Returns the sample rate of the JACK system, as set by the user when jackd was started.

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_get_buffer_size">**`      client:get_buffer_size()
  `** </a>

  Returns the number of frames that are processed in one process cycle.
  See also [BufferSize status message](#BufferSize).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_set_buffer_size">**`      client:set_buffer_size(n)
  `** </a>

  Sets the number of frames that are processed in one process cycle.
  See also [BufferSize status message](#BufferSize).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_cpu_load">**`      client:cpu_load()
  `** </a>

  Returns the current CPU load estimated by JACK as float number in percent. This is a running 
  average of the time it takes to execute a full process cycle for all clients as a percentage 
  of the real time available per cycle determined by the buffer size and sample rate. 

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="client_new_process_buffer">**` client:new_process_buffer([type])
  `** </a>

  Creates a new process buffer object which can be used as [connector](#connector-objects) 
  for [processor objects](#processor-objects). A process buffer can be used as input connector 
  by multiple processor objects but as output connector it can only be used by one processor 
  object.

  * *type*       - optional string value, must be "AUDIO" or "MIDI". Default value is "AUDIO" 
                   if this parameter is not given.
  
  See also [example06.lua](../examples/example06.lua) for AUDIO process buffer
  or [example07.lua](../examples/example07.lua) for MIDI process buffer usage.

<!-- ---------------------------------------------------------------------------------------- -->
##   Port Methods
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="port_unregister">**`      port:unregister()
  `** </a>
  
  Remove the port from the client, disconnecting any existing connections.  
  The port object becomes invalid and cannot be used furthermore.


* <a id="port_get_client">**`      port:get_client()
  `** </a>
  
  Gives the associated client object, i.e. the client object that the port object was
  created with. 
  
  A port object can refer to a JACK port that was registered by another client, e.g. by 
  calling [client:port_by_name()](#client_port_by_name).
  
  [port:is_mine()](#port_is_mine) gives *true* if the port was registered by the associated
  client object.
  
  The reference to the associated client object is of weak type, i.e. it does not prevent
  the client object from being garbage collected. If a client object is garbage collected all
  associated port objects become invalid and cannot be used furthermore, e.g. calling
  the method *port:get_client()* will raise an error.


<!-- ---------------------------------------------------------------------------------------- -->
* <a id="port_is_mine">**`        port:is_mine()
  `** </a>
  
  Returns *true* if the port belongs to the associated client, i.e. the port was registered 
  by the associated client, see also [port:get_client()](#port_get_client)
  
<!-- ---------------------------------------------------------------------------------------- -->
* <a id="port_is_input">**`       port:is_input()
  `** </a>
  
  Returns *true* if the port is an input port.
  
<!-- ---------------------------------------------------------------------------------------- -->
* <a id="port_is_output">**`      port:is_output()
  `** </a>

  Returns *true* if the port is an output port.
  
<!-- ---------------------------------------------------------------------------------------- -->
* <a id="port_is_midi">**`        port:is_midi()
  `** </a>
  
  Returns *true* if the port is a *MIDI* port.

<!-- ---------------------------------------------------------------------------------------- -->
* <a id="port_is_audio">**`       port:is_audio()
  `** </a>
  
  Returns *true* if the port is an *AUDIO* port.

<!-- ---------------------------------------------------------------------------------------- -->
* <a id="port_get_connections">**`       port:get_connections()
  `** </a>
  
  Returns a string list of full port names to which the port is connected. Returns an empty
  list if there is no port connected.


<!-- ---------------------------------------------------------------------------------------- -->
##   Connector Objects
<!-- ---------------------------------------------------------------------------------------- -->

Connector objects are either *port objects* with 
*[port:is_mine()](#port_is_mine) == true* or *process buffer objects*.

  * [Port objects](#client_port_register) are visible to other JACK clients outside your Lua application.

  * [Process buffer objects](#client_new_process_buffer) are only visible inside your Lua application. They can be used to connect
    [processor objects](#processor-objects) with each other.

<!-- ---------------------------------------------------------------------------------------- -->
##   Processor Objects
<!-- ---------------------------------------------------------------------------------------- -->

Processor objects are Lua objects for processing realtime audio data. They must be implemented
in C using the [Auproc C API].

Processor objects can be connected to audio or midi data streams using 
[connector objects](#connector-objects).

The [LJACK examples](../examples) are using procesor objects that are provided by the
[lua-auproc](https://github.com/osch/lua-auproc) package.

<!-- ---------------------------------------------------------------------------------------- -->
##   Status Messages
<!-- ---------------------------------------------------------------------------------------- -->

  The optional *statusReceiver* object given in [ljack.client_open()](#ljack_client_open) must 
  implement the [Receiver C API] to receive asynchronous information from the 
  [JACK Client Callbacks]. See also [example02.lua](../examples/example02.lua)
                       
  Each message contains as first argument a string value indicating the message type name and 
  further arguments depending on the message type:
  
  <!-- ------------------------------------------- -->

  * <a id="ClientRegistration">**`     "ClientRegistration", name, registered
    `** </a>
  
    a client is registered or unregistered.
    
    * *name* - name of the client

    * *registered* - *true* if the client is being registered, 
                     *false* if the client is being unregistered.

  <!-- ------------------------------------------- -->
  
  * <a id="GraphOrder">**`             "GraphOrder"
    `** </a>
  
    the JACK processing graph is reordered.
  
  <!-- ------------------------------------------- -->

  * <a id="PortConnect">**`            "PortConnect", id1, id2, connected
    `** </a>
  
    ports are connected or disconnected. Invoke [client:port_name(id)](#client_port_name) or
    [client:port_by_id(id)](#client_port_by_id) to obtain the port name or port object for
    the given integer IDs.
    
    * *id1* - integer ID of the first port

    * *id2* - integer ID of the second port

    * *connected* - *true* if ports were connected, 
                    *false* if ports were disconnected.

  <!-- ------------------------------------------- -->

  * <a id="PortRegistration">**`       "PortRegistration", id, registered
    `** </a>
  
    a port is registered or unregistered. Invoke [client:port_name(id)](#client_port_name) or
    [client:port_by_id(id)](#client_port_by_id) to obtain the port name or port object for
    the given integer id.
    
    * *id* - integer ID of the port

    * *registered* - *true* if the port is being registered, 
                     *false* if the port is being unregistered.

  <!-- ------------------------------------------- -->

  * <a id="PortRename">**`             "PortRename", id, old_name, new_name
    `** </a>
  
    a port is renamed.
    
    * *id* - integer ID of the port

    * *old_name* - the name of the port before the rename was carried out.
    * *new_name* - the name of the port after the rename was carried out.


  <!-- ------------------------------------------- -->

  * <a id="BufferSize">**`             "BufferSize", nframes
    `** </a>
  
    the number of frames that are processed in one process cycle has 
    been changed.
    
    * *nframes* - new number of frames


  <!-- ------------------------------------------- -->

  * <a id="Shutdown">**`               "Shutdown", reason
    `** </a>
  
    jackd is shutdown.
    
    * *reason* - a string describing the shutdown reason (backend failure, 
                 server crash... etc...) 

  <!-- ------------------------------------------- -->

  * <a id="XRun">**`                   "XRun"
    `** </a>
  
    xrun has occured.


<!-- ---------------------------------------------------------------------------------------- -->


End of document.

<!-- ---------------------------------------------------------------------------------------- -->

[ljack]:                    https://luarocks.org/modules/osch/ljack
[ljack_cairo]:              https://luarocks.org/modules/osch/ljack_cairo
[ljack_opengl]:             https://luarocks.org/modules/osch/ljack_opengl
[mtmsg]:                    https://github.com/osch/lua-mtmsg#mtmsg
[carray]:                   https://github.com/osch/lua-carray
[light userdata]:           https://www.lua.org/manual/5.4/manual.html#2.1
[Receiver C API]:           https://github.com/lua-capis/lua-receiver-capi
[Sender C API]:             https://github.com/lua-capis/lua-sender-capi
[Auproc C API]:             https://github.com/lua-capis/lua-auproc-capi
[JACK Client Callbacks]:    https://jackaudio.org/api/group__ClientCallbacks.html
