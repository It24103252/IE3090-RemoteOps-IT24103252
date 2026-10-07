# RemoteOps Design Diary
## Final Protocol Validation and Testing

During final testing, I compared the implementation with the fixed RemoteOps protocol in the assignment specification. I found that some features were functionally working but their response formats were not exactly correct. LISTPROC was originally returned as a multi-line response, EXEC also used a multi-line response, GET used an incorrect response header, and EXIT was used instead of QUIT.

I corrected LISTPROC to return `OK PROCS ... SID:2523`, changed EXEC to return `OK EXEC_RESULT ... SID:2523`, added `ERR 002 COMMAND_NOT_ALLOWED SID:2523` for invalid EXEC commands, corrected GET to use `OK FILE_SEND <filename> <filesize> SID:2523`, and replaced EXIT with QUIT.

After these corrections, I tested the Agent and Controller together. PUT and GET were verified using `cmp` and SHA-256 hashes, confirming that the transferred file was byte-for-byte identical. UDP monitoring was tested using port 9500, and MONITOR START/STOP worked correctly. I also tested five simultaneous Controller connections, with each connection handled by a different Agent child process.

Finally, I updated Makefile_252 and verified that both Agent and Controller could be built using `make -f Makefile_252`. This final validation helped me understand that a network application can appear to work while still being incorrect if it does not follow the specified application protocol exactly.
## Initial Setup

The RemoteOps project was started on CentOS Stream 10.

Development tools were verified:
- GCC
- GNU Make
- Git

The project uses the personalised values derived from registration
number IT24103252.

Agent Port: 9410
SID: 2523
Authentication Token: OPS-3252

The project will contain a TCP Agent and TCP Controller.
A secondary UDP channel will later be implemented for periodic
system monitoring.
