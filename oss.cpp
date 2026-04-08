#include <iostream>
#include <fstream>
#include <queue>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>

using namespace std;

#define PERMS 0644
#define INIT 1
#define RUN 2

const int BILLION = 1000000000;
const int QUANTUM = 25000000;
const int MAXIMUM_PROCESS = 20;
const int BUFF_SZ = sizeof(int) * 2;

// overhead constant assumption
const int SCHED_OVERHEAD = 5000;
const int BLOCK_OVERHEAD = 3000;
const int UNBLOCK_OVERHEAD = 3000;
const int FORK_OVERHEAD = 10000;

int logLineCount = 0;
const int MAX_LOG_LINES = 1000;
int shm_id;
int *customClock = nullptr;
int msgqid;

ofstream logfile;

queue<int> readyQ;
vector<int> blockedQ;

struct PCB
{
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNano;
    int serviceTimeSeconds;
    int serviceTimeNano;
    int eventWaitSeconds;
    int eventWaitNano;
    int blocked;
};

PCB processTable[MAXIMUM_PROCESS];

struct msgbuffer
{
    long mtype;
    int intData;
    int flag;
};

void logmsg(string s)
{
    cout << s;

    if (logLineCount < MAX_LOG_LINES)
    {
        logfile << s;
        logfile.flush();

        // Count number of lines in this string
        for (size_t i = 0; i < s.length(); i++)
        {
            if (s[i] == '\n')
                logLineCount++;
        }
    }
}

void incClock(int sec, int nano)
{
    customClock[1] += nano;
    customClock[0] += sec;
    while (customClock[1] >= BILLION)
    {
        customClock[0]++;
        customClock[1] -= BILLION;
    }
}

void normalizeTime(int &sec, int &nano)
{
    while (nano >= BILLION)
    {
        sec++;
        nano -= BILLION;
    }
}

bool timeReached(int s, int n)
{
    return (customClock[0] > s) ||
           (customClock[0] == s && customClock[1] >= n);
}

void initProcessTable()
{
    for (int i = 0; i < MAXIMUM_PROCESS; i++)
    {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
        processTable[i].serviceTimeSeconds = 0;
        processTable[i].serviceTimeNano = 0;
        processTable[i].eventWaitSeconds = 0;
        processTable[i].eventWaitNano = 0;
        processTable[i].blocked = 0;
    }
}

void printOnScreen()
{
    cout << "\n========== SYSTEM STATE ==========\n";
    cout << "OSS PID:" << getpid()
         << " Time: " << customClock[0] << ":" << customClock[1] << "\n";

    cout << "Entry Occupied PID StartS StartN ServiceS ServiceN EventS EventN Blocked\n";

    for (int i = 0; i < MAXIMUM_PROCESS; i++)
    {
        if (processTable[i].occupied)
        {
            cout << i << " "
                 << processTable[i].occupied << " "
                 << processTable[i].pid << " "
                 << processTable[i].startSeconds << " "
                 << processTable[i].startNano << " "
                 << processTable[i].serviceTimeSeconds << " "
                 << processTable[i].serviceTimeNano << " "
                 << processTable[i].eventWaitSeconds << " "
                 << processTable[i].eventWaitNano << " "
                 << processTable[i].blocked << "\n";
        }
        else
        {
            cout << i << " 0\n";
        }
    }

    cout << "ReadyQ: ";
    queue<int> temp = readyQ;
    while (!temp.empty())
    {
        cout << "P" << temp.front() << " ";
        temp.pop();
    }

    cout << "\nBlockedQ: ";
    for (size_t k = 0; k < blockedQ.size(); k++)
    {
        cout << "P" << blockedQ[k] << " ";
    }

    cout << "\n==================================\n";
}
void printReadyQueueLog()
{
    logfile << "OSS: Ready queue [ ";
    queue<int> temp = readyQ;
    while (!temp.empty())
    {
        logfile << "P" << temp.front() << " ";
        temp.pop();
    }
    logfile << "]\n";
}
void printReadyQonScreen()
{
    cout << "ReadyQ: [ ";
    queue<int> temp = readyQ;
    while (!temp.empty())
    {
        cout << "P" << temp.front() << " ";
        temp.pop();
    }
    cout << " ]\n";
}
void printBlockedQonScreen()
{
    cout << "\nBlockedQ: [ ";
    for (size_t k = 0; k < blockedQ.size(); k++)
    {
        cout << "P" << blockedQ[k] << " ";
    }
    cout << " ]\n";
}

void cleanup()
{
    for (int i = 0; i < MAXIMUM_PROCESS; i++)
    {
        if (processTable[i].occupied)
            kill(processTable[i].pid, SIGTERM);
    }
    shmdt(customClock);
    shmctl(shm_id, IPC_RMID, NULL);
    msgctl(msgqid, IPC_RMID, NULL);
    logfile.close();
}

void signalHandler(int sig)
{
    logmsg("\nOSS: Cleaning up...\n");
    cleanup();
    exit(1);
}

int main(int argc, char *argv[])
{

    int n = -1, s = 2;
    double t = 0.1, interval = 0.1;
    string logname = "logfile.txt";

    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1)
    {
        if (opt == 'n')
            n = atoi(optarg);
        if (opt == 's')
            s = atoi(optarg);
        if (opt == 't')
            t = atof(optarg);
        if (opt == 'i')
            interval = atof(optarg);
        if (opt == 'f')
            logname = optarg;
    }

    logfile.open(logname);

    shm_id = shmget(ftok("oss.cpp", 0), BUFF_SZ, 0666 | IPC_CREAT);
    customClock = (int *)shmat(shm_id, NULL, 0);
    customClock[0] = customClock[1] = 0;

    msgqid = msgget(ftok("msgq.txt", 1), PERMS | IPC_CREAT);

    signal(SIGINT, signalHandler);
    alarm(3);

    initProcessTable();
    srand(time(0));

    int launched = 0, active = 0;
    int lastPrintSec = 0, lastPrintNano = 0;

    int intervalSec = (int)interval;
    int intervalNano = (interval - intervalSec) * BILLION;

    int lastLaunchSec = 0;
    int lastLaunchNano = 0;
    long long totalTime = 0;
    long long idleTime = 0;
    long long overheadTime = 0;

    while (launched < n || active > 0)
    {

        // print every 0.5 sec
        int printSec = lastPrintSec;
        int printNano = lastPrintNano + 500000000;
        normalizeTime(printSec, printNano);

        if (timeReached(printSec, printNano))
        {
            printOnScreen();
            lastPrintSec = customClock[0];
            lastPrintNano = customClock[1];
        }

        // unblock
        for (vector<int>::iterator j = blockedQ.begin(); j != blockedQ.end();)
        {
            int i = *j;
            if (timeReached(processTable[i].eventWaitSeconds,
                            processTable[i].eventWaitNano))
            {

                incClock(0, UNBLOCK_OVERHEAD);
                overheadTime += UNBLOCK_OVERHEAD; // overhead to move the process out of block queue
                logmsg("OSS: Unblocking P" + to_string(i) + "\n");

                processTable[i].blocked = 0;
                readyQ.push(i);
                j = blockedQ.erase(j);
            }
            else
                ++j;
        }
        // checking if we can launch
        int nextLaunchSec = lastLaunchSec + intervalSec;
        int nextLaunchNano = lastLaunchNano + intervalNano;
        normalizeTime(nextLaunchSec, nextLaunchNano);

        // launch
        if (launched < n &&
            active < s &&
            (launched == 0 || timeReached(nextLaunchSec, nextLaunchNano)))
        {
            for (int i = 0; i < MAXIMUM_PROCESS; i++)
            {
                if (!processTable[i].occupied)
                {

                    pid_t pid = fork();
                    incClock(0, FORK_OVERHEAD);
                    overheadTime += FORK_OVERHEAD; // time used to fork the process
                    if (pid == 0)
                    {
                        execl("./worker", "./worker", NULL);
                        exit(1);
                    }

                    processTable[i].occupied = 1;
                    processTable[i].pid = pid;

                    logmsg("OSS: Generating process with PID " + to_string(processTable[i].pid) +
                           " and putting it in ready queue at time " +
                           to_string(customClock[0]) + ":" + to_string(customClock[1]) + "\n");
                    int total = (rand() % (int)(t * BILLION)) + 1;
                    msgbuffer msg = {pid, total, INIT};
                    msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0); // sending message to worker with randomly generated t time to inform its now "scheduling it"

                    readyQ.push(i);
                    printReadyQueueLog();
                    printReadyQonScreen();
                    logmsg("OSS: Putting process with PID " +
                           to_string(processTable[i].pid) +
                           " into ready queue\n");
                    launched++;
                    active++;
                    lastLaunchSec = customClock[0];
                    lastLaunchNano = customClock[1];
                    break;
                }
            }
        }

        // schedule
        if (!readyQ.empty())
        {
            incClock(0, SCHED_OVERHEAD);
            overheadTime += SCHED_OVERHEAD; // time spent on scheduling
            int i = readyQ.front();
            readyQ.pop();

            logmsg("OSS: Dispatching process with PID " +
                   to_string(processTable[i].pid) +
                   " from ready queue at time " +
                   to_string(customClock[0]) + ":" +
                   to_string(customClock[1]) + "\n");

            int dispatchTime = rand() % 1000;
            incClock(0, dispatchTime);
            overheadTime += dispatchTime;

            logmsg("OSS: total time this dispatch was " +
                   to_string(dispatchTime) + " nanoseconds\n");
            msgbuffer msg = {processTable[i].pid, QUANTUM, RUN};
            msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0);

            msgrcv(msgqid, &msg, sizeof(msg) - sizeof(long), getpid(), 0);

            int used = msg.intData; // user sends back the time it used
            totalTime += abs(used);
            logmsg("OSS: Receiving that process with PID " +
                   to_string(processTable[i].pid) +
                   " ran for " + to_string(abs(used)) + " nanoseconds\n");

            incClock(0, abs(used));
            processTable[i].serviceTimeNano += abs(used);
            normalizeTime(processTable[i].serviceTimeSeconds,
                          processTable[i].serviceTimeNano);

            if (used < 0)
            {

                logmsg("OSS: P" + to_string(i) + " TERMINATED\n"); // if used in negative process is terminated
                waitpid(processTable[i].pid, NULL, 0);
                processTable[i].pid = 0;
                processTable[i].occupied = 0;
                active--;
            }
            else if (used < QUANTUM)
            {
                incClock(0, BLOCK_OVERHEAD);
                overheadTime += BLOCK_OVERHEAD;                     // handling a block overhead adding a process in block queue
                logmsg("OSS: not using its entire time quantum\n"); // if used is less than quantum it used part of it and got blocked and waiting for event
                logmsg("OSS: Putting process with PID " +
                       to_string(processTable[i].pid) +
                       " into blocked queue\n");

                processTable[i].blocked = 1;
                processTable[i].eventWaitSeconds = customClock[0];
                processTable[i].eventWaitNano = customClock[1] + 100000000; // adding 100ms to event wait so it can wait in block queue for 100ms
                normalizeTime(processTable[i].eventWaitSeconds,
                              processTable[i].eventWaitNano);

                blockedQ.push_back(i);
                printBlockedQonScreen();
            }
            else
            {
                readyQ.push(i); //
                printReadyQueueLog();
                printReadyQonScreen();
                logmsg("OSS: Putting process with PID " +
                       to_string(processTable[i].pid) +
                       " into ready queue\n");
            }
        }
        else
        {
            idleTime += 10000;
            incClock(0, 10000); // incrementing the clock by 10microseconds when the our system/cpu is idle
        }
    }
    double cpuUtil = (double)totalTime /
                     (totalTime + overheadTime + idleTime);

    cout << "Total time: " << totalTime << "\n"; // total time used by process
    // time spent on scheduling, handling a block that is moving a process in and out of blocked queue everything gets added for overheadtime
    cout << "Overhead Time: " << overheadTime << "\n";
    // idle time of cpu
    cout << "Idle Time: " << idleTime << "\n";

    cout << "CPU utilization: " << cpuUtil * 100 << "%\n";
    cleanup();
    return 0;
}