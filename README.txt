Building the server and client binaries
1. Install development ncurses libraries (libncurses5-dev)
2. Install cmake 2.8+
3. Go to the directory containing CMakeLists.txt and run "cmake ." Makefiles will be generated
4. Run "make" and the binaries will be created in bin/

Using the server
Server has several launch parameters which can be used
1. -m Specifies directory in which maps are located. Default maps/
2. -v Do verbose logging (player spawning points, map loading etc)
3. -vv Do very verbose logging (also logs sent/received packet details, game ticks)
4. -p [PORT], listen on specific port. Default 8888