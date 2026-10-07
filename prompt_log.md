# AI Prompt Log

## Entry 1 - Project Setup

Tool:
ChatGPT

Purpose:
Reviewed the IE3090 assignment requirements and planned the
RemoteOps implementation.

AI assistance:
Helped calculate the personalised values from registration number
IT24103252 and explained how to create the initial project structure.

My actions:
I verified the calculations against Section 2.4 of the assignment
brief and created the project files manually on CentOS Stream 10.

## Final Protocol Audit and Testing

**AI tool:** ChatGPT

**Purpose:** Used to review the RemoteOps implementation against the fixed assignment protocol and identify protocol-format differences.

**Main assistance received:**
- Compared LISTPROC, EXEC, GET and graceful disconnect behavior with the assignment specification.
- Suggested corrections for the required one-line LISTPROC and EXEC responses.
- Assisted with correcting the GET FILE_SEND header and changing EXIT to QUIT.
- Assisted with reviewing Agent and Controller code after the corrections.
- Suggested test commands for PUT/GET integrity, UDP monitoring, concurrency and Makefile validation.

**My evaluation and testing:**
I did not rely only on the generated suggestions. I compiled the modified Agent and Controller on CentOS using GCC warning flags and tested the commands manually. I verified PUT/GET file integrity using `cmp` and SHA-256, tested UDP monitoring, tested graceful QUIT, and opened five Controller sessions to verify concurrency. I also checked the Git changes before committing and pushed the final tested version to GitHub.
