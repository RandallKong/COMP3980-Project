# COMP3980-Project
COMP3980 - Randall and Dinuja Project

**SET UP STEPS**
1) ./generate-flags.sh
2) ./generate-cmakelists.sh
3) ./change-compiler.sh -c [gcc or clang]
4) ./build.sh

**RUNNING**
1) ./chat -a [IPV4 or IPV6 ip addr] [port]
2) ./chat -c [IPV4 or IPV6 ip addr] [port]

- **-a** to listen on IP addr and port
- **-c** to connect to server network socket

with IO Redirection
- ./chat -c [IPV4 or IPV6 ip addr] [port] < [txt file]

**Keys**
- **\n** aka pressing enter, will send message to other one
- **ctrl-z** closes the connection

**TIPS**
- don't push files .sh executables generate.

