# AI Prompt Log – RemoteOps

Registration Number: IT24103252
Project: RemoteOps – Remote System Monitoring and Management Tool over TCP/IP
AI Tool Used: ChatGPT

## 1. Assignment Planning

Prompt:
"Explain the RemoteOps assignment and guide me step by step from the beginning."

AI Assistance:
ChatGPT explained the assignment requirements, Agent/Controller architecture,
personalised values, TCP/UDP communication, required commands, testing plan,
Git workflow and final deliverables.

My Evaluation:
I compared the suggested implementation plan with the assignment specification
before starting development.

---

## 2. Project Personalisation

Prompt:
"Help me calculate my personalised port, SID, authentication token and filenames
using registration number IT24103252."

AI Assistance:
The personalised values were identified as:

TCP Agent Port: 9410
Session ID: SID:2523
Authentication Token: OPS-3252
Agent Source: agent_252.c
Controller Source: controller_252.c
Makefile: Makefile_252
Log File: remoteops_IT24103252.log
Storage Directory: ./agentfiles/IT24103252/

My Evaluation:
I added these values to the project and verified them during runtime testing.

---

## 3. TCP Agent Setup

Prompt:
"Guide me to create the Agent TCP server using C and BSD sockets."

AI Assistance:
ChatGPT explained socket(), setsockopt(), bind(), listen(), accept() and the
purpose of the Agent listening socket.

My Evaluation:
I compiled the Agent using GCC and verified that it successfully listened on
TCP port 9410.

---

## 4. TCP Controller Connection

Prompt:
"Guide me to create the Controller and connect it to my Agent."

AI Assistance:
ChatGPT explained the Controller socket creation, IPv4 address configuration,
connect() and communication with the Agent.

My Evaluation:
I tested the Controller using 127.0.0.1 and confirmed that it connected to the
Agent successfully.

---

## 5. Concurrent Controller Handling

Prompt:
"How can my Agent support at least five Controllers at the same time?"

AI Assistance:
ChatGPT explained process-based concurrency using fork(), where the parent
continues accepting connections and each child handles one Controller.

My Evaluation:
I tested five simultaneous Controller connections and confirmed that separate
Agent child processes handled the sessions.

---

## 6. Authentication and Session Validation

Prompt:
"Help me implement AUTH OPS-3252 and SID:2523 before allowing commands."

AI Assistance:
ChatGPT explained authentication state and how the Agent should reject invalid
authentication before processing protected commands.

My Evaluation:
I tested successful authentication and verified the response:

OK AUTHENTICATED SID:2523

---

## 7. SYSINFO and LISTPROC

Prompt:
"Help me implement SYSINFO and LISTPROC according to the RemoteOps protocol."

AI Assistance:
ChatGPT explained how Linux /proc information could be used for system
statistics and how process information could be collected.

My Evaluation:
I tested both commands. During final protocol validation I corrected LISTPROC
so that it returned the required one-line format:

OK PROCS <process information> SID:2523

---

## 8. Restricted EXEC Commands

Prompt:
"Help me implement EXEC but only allow DATE, UPTIME, DISKFREE, HOSTNAME and
WHOAMI."

AI Assistance:
ChatGPT suggested using an allow-list rather than allowing arbitrary shell
commands.

My Evaluation:
I tested all allowed commands and also tested an unsupported command such as
EXEC LS.

The final invalid-command response was:

ERR 002 COMMAND_NOT_ALLOWED SID:2523

---

## 9. PUT File Upload

Prompt:
"Guide me to implement PUT with exact byte handling."

AI Assistance:
ChatGPT explained that the Agent must first read the PUT header and then receive
exactly the declared number of raw file bytes rather than assuming one recv()
contains the whole file.

My Evaluation:
I created put_test.txt and successfully uploaded it into:

./agentfiles/IT24103252/

---

## 10. GET File Download

Prompt:
"Guide me to implement GET and receive the exact file bytes."

AI Assistance:
ChatGPT explained how the Agent should send a FILE_SEND header followed
immediately by the exact raw file bytes.

My Evaluation:
I downloaded put_test.txt and verified the original and downloaded files using
cmp and SHA-256.

Both SHA-256 hashes were identical.

---

## 11. UDP Periodic Monitoring

Prompt:
"Help me implement MONITOR START and MONITOR STOP using UDP."

AI Assistance:
ChatGPT explained the secondary UDP channel and periodic system-information
datagrams.

My Evaluation:
I tested monitoring on UDP port 9500 and received periodic messages in the
required format:

SYSINFO <cpu_load> <mem_used_mb> <uptime_sec> SID:2523

I also verified MONITOR STOP.

---

## 12. Logging and Graceful Disconnect

Prompt:
"Help me add session logging and graceful disconnect to RemoteOps."

AI Assistance:
ChatGPT explained timestamped logging and cleanup of resources when a Controller
disconnects.

My Evaluation:
I verified the personalised log file:

remoteops_IT24103252.log

I also tested graceful QUIT and received:

OK BYE SID:2523

---

## 13. Final Fixed Protocol Audit

Prompt:
"Compare my implementation with the assignment fixed protocol and identify
anything that is incorrect."

AI Assistance:
The review identified several protocol-format mismatches in the earlier
implementation:

LISTPROC used a multi-line format.
EXEC used a multi-line format.
Invalid EXEC did not use error code 002.
GET used an incorrect FILE response header.
EXIT was being used instead of QUIT.

My Evaluation:
I checked these points against the assignment specification and modified the
Agent and Controller.

The final implementation uses:

LISTPROC:
OK PROCS ... SID:2523

EXEC:
OK EXEC_RESULT ... SID:2523

Invalid EXEC:
ERR 002 COMMAND_NOT_ALLOWED SID:2523

GET:
OK FILE_SEND <filename> <filesize> SID:2523

QUIT:
OK BYE SID:2523

I recompiled and retested the system after these corrections.

---

## 14. Final Testing

Prompt:
"Give me the final tests to verify my complete Agent and Controller."

AI Assistance:
ChatGPT suggested tests for authentication, SYSINFO, LISTPROC, EXEC, PUT, GET,
UDP monitoring, QUIT, logging, file integrity and multiple simultaneous
Controllers.

My Evaluation:
I performed these tests manually on CentOS Stream 10.

File transfer integrity was verified using:

cmp put_test.txt downloads/put_test.txt

and:

sha256sum put_test.txt downloads/put_test.txt

The files matched exactly.

---

## 15. Makefile and Compilation

Prompt:
"Help me check my Makefile and perform a final clean compilation."

AI Assistance:
ChatGPT suggested compiling both programs with warning options including
-Wall, -Wextra and -Wpedantic.

My Evaluation:
I successfully ran:

make -f Makefile_252 clean
make -f Makefile_252

Both Agent and Controller were generated successfully.

---

## 16. Git and Incremental Development

Prompt:
"Guide me to maintain meaningful Git commits for each stage of the project."

AI Assistance:
ChatGPT explained how to stage, commit and push each meaningful implementation
stage separately.

My Evaluation:
I maintained an incremental Git history covering project setup, TCP networking,
concurrency, authentication, system commands, EXEC, PUT, GET, UDP monitoring,
logging and final protocol alignment.

---

## 17. Implementation Report

Prompt:
"Help me structure a professional implementation report using my assignment
requirements and my actual screenshots."

AI Assistance:
ChatGPT helped organise the report into project personalisation, architecture,
Git evidence, Agent, Controller, protocol, commands, file transfer, UDP,
concurrency, logging, testing, design rationale and conclusion.

My Evaluation:
I reviewed the report and selected screenshots from my own implementation as
evidence for the corresponding sections.

---

## Critical Evaluation of AI Use

AI was used as a development and learning assistant rather than as a replacement
for testing. Suggestions were compiled and tested on my CentOS Stream 10
environment before being accepted.

An important example occurred during final protocol validation. Some earlier
responses were functionally working but did not exactly match the fixed
application protocol required by the assignment. I therefore compared the
implementation with the assignment specification, corrected LISTPROC, EXEC,
GET and QUIT, rebuilt the project and repeated the tests.

This process showed that AI-generated suggestions still require verification,
testing and critical evaluation by the developer.
