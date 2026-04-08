Name: Samiksha Sapkota
Date: March 29, 2026
Environment: Mac , Visual Studio Code
How to compile the project:
Type 'make'
Example of how to run the project:

./oss -n 4 -s 3 -t 1.5 -i 0.05 -f log

Program behaviour:

In this project, I am using round-robin scheduler(STL queue and quantum for scheduling the process), message queues for synchronization, and shared memory clock and a process table that stores the data of process created. In my project oss is changing clock, the clock increment is only happening when work is done or if there is no process in the system. My base quantum for round robin scheduler is 25ms(25000000), and STL queue for ready queue which have the process name stored which are ready to get CPU time. My oss.cpp communicate with worker.cpp(child process) using message queue. The first message oss sends with the randomly generated "0-t" time as a data which informs worker that oss is "scheduling it" for random 0-t time. oss then sends second message when it is ready to run the process with quantum data. worker runs for quantum time, if worker uses it all, it sends back the same quantum value to oss, which means the process can be added again to our ready queue. If worker send smaller value than our quantum that means it used part of it and got blocked and now is waiting for event to happen. Once it gets blocked we add it to our blocked queue, and add 100ms to our eventWaitSecond and eventWaitNano process table column. So now our oss will wait for event time to complete to get the blocked process back to ready queue. Next is if the worker sends back negative value that means the process is terminated. We add this "used" time sent by worker to our oss clock. Our worker also sees to it that 20% of our process will get block and remaining 80% will either terminate or use all quantum. Finally we calculate total time CPU run the process, we also caluclate overhead time which is spent on scheduling, handling block i.e moving prcoess in and out of blocked queue, forking the child process, etc and also calculating the idle time of our CPU. Using all this we calculated CPU Utilization percent.


Generative AI used: ChatGPT and Google AI overview:
Prompt : how to use STL queue in cpp - Google AI overview.
Suggestion: Setup and declartion, Example of ready queue. 

Prompt: I'am adding overheadtime when process is dispatching, but I have not considered time spent on scheduling, handling a block, putting process in blockedqueue, move in and out of blocked queue. this all need to be calculated under overheadtime , how do I accomplish that? - Chat GPT
Suggestion: To assume constants for all the repeated overheads
// overhead constant assumption
const int SCHED_OVERHEAD = 5000;
const int BLOCK_OVERHEAD = 3000;
const int UNBLOCK_OVERHEAD = 3000;
const int FORK_OVERHEAD = 10000;
and add them to clock whereever I am using CPU activity like scheduling the process from ready queue , Adding the process to block queue, and etc.
