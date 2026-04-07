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
#include <algorithm>

using namespace std;

#define PERMS 0644
#define INIT 1
#define RUN 2
const int BILLION = 1000000000;
const int QUANTUM = 25000000;
const int MAXIMUM_PROCESS = 20;
const int BUFF_SZ = sizeof(int) * 2;
int shm_key;
int shm_id;
int *customClock = nullptr;
ofstream logfile;
int msgqid;

// PCB Structure

struct PCB
{
    int occupied;           // either true or false
    pid_t pid;              // process id of this child
    int startSeconds;       // time when it was forked
    int startNano;          // time when it was forked
    int serviceTimeSeconds; // total seconds it has been "scheduled"
    int serviceTimeNano;    // total nanoseconds it has been "scheduled"
    int eventWaitSeconds;   // when does its event happen?
    int eventWaitNano;      // when does its event happen?
    int blocked;            // is this process waiting for event
};
struct PCB processTable[MAXIMUM_PROCESS];

struct msgbuffer
{
    long mtype;
    int intData;
    int flag;
};

/////incrementing clock
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
    if (nano >= BILLION)
    {
        sec++;
        nano -= BILLION;
    }
}

bool timeReached(int targetSec, int targetNano)
{
    if (customClock[0] > targetSec)
        return true;
    if (customClock[0] == targetSec && customClock[1] >= targetNano)
        return true;
    return false;
}

void initProcessTable()
{
    for (int i = 0; i < MAXIMUM_PROCESS; i++)
        processTable[i].occupied = 0;
}

void logmsg(string s)
{
    cout << s;
    logfile << s;
}

void printOnScreen()
{

    cout << "\nOSS PID:" + to_string(getpid()) +
                " SysClockS:" + to_string(customClock[0]) +
                " SysClockNano:" + to_string(customClock[1]) + "\n";

    cout << "Entry Occupied PID StartS StartN ServiceS ServiceN EventWaitS EventWaitN Blocked\n";

    for (int i = 0; i < MAXIMUM_PROCESS; i++)
    {
        if (processTable[i].occupied)
        {
            cout << to_string(i) + " " +
                        to_string(processTable[i].occupied) + " " +
                        to_string(processTable[i].pid) + " " +
                        to_string(processTable[i].startSeconds) + " " +
                        to_string(processTable[i].startNano) + " " +
                        to_string(processTable[i].serviceTimeSeconds) + " " +
                        to_string(processTable[i].serviceTimeNano) + " " +
                        to_string(processTable[i].eventWaitSeconds) + "\n";
        }
        else
        {
            cout << to_string(i) + " 0\n";
        }
    }
}

// Clean up
void cleanup()
{
    for (int i = 0; i < MAXIMUM_PROCESS; i++)
    {
        if (processTable[i].occupied)
            kill(processTable[i].pid, SIGTERM);
    }

    if (customClock)
        shmdt(customClock);

    shmctl(shm_id, IPC_RMID, NULL);
    msgctl(msgqid, IPC_RMID, NULL);
    logfile.close();
}

void signalHandler(int sig)
{
    logmsg("\nOSS: Caught signal. Cleaning up...\n");
    cleanup();
    exit(1);
}
void printHelp()
{
    cout << "Usage: oss [-h] [-n proc] [-s simul] [-t iter] [-i fractionofSecond] [-f logfile]\n"
         << "-n proc  total number of user processes (required, 1-100)\n"
         << "-s simul max simultaneous processes (default 2, 1-15)\n"
         << "-t time limit for Children(default 0.1, 0-2seconds)\n"
         << "-i fraction of second to launch children(default 0.1, 0 to 2)\n"
         << "-f log file name(default logfile.txt)\n";
}

int main(int argc, char *argv[])
{
    int n = -10;    // Since it is required I gave a dummy number, for requirement condition I am going to use ahead
    int s = 2;      // Default value is 2
    double t = 0.1; // Default value is 3seconds 0nanosecond
    double interval = 0.1;
    string logname = "logfile.txt";

    ///////////parsing options and getting command line arguments
    int option;
    while ((option = getopt(argc, argv, "hn:s:t:i:f:")) != -1)
    {
        switch (option)
        {
        case 'h':
            printHelp();
            return 0;
        case 'n':
            n = atoi(optarg); // optarg is the global variable set by getopt when a parameter with a value has been provided
            break;
        case 's':
            s = atoi(optarg);
            break;
        case 't':
            t = atof(optarg);
            break;
        case 'i':
            interval = atof(optarg);
            break;
        case 'f':
            logname = optarg;
            break;
        default:
            printHelp();
            return 1;
        }
    }
    if (n == -10)
    {
        cerr << "Error: -n (number of processes) is required. \n";
        return 1;
    }

    if (n < 1 || n > 100)
    {
        cerr << "Error: -n must be between 1 and 100.\n";
        return 1;
    }

    if (s < 1 || s > 15)
    {
        cerr << "Error: -s must be between 1 and 15.\n";
        return 1;
    }

    if (t < 0.0 || t > 2.0)
    {
        cerr << "Error: -t must be between 0.0 and 2.0.\n";
        return 1;
    }

    if (s > n)
    {
        s = n; // just in case simultaneous is greater than number of process
    }
    logfile.open(logname);

    ////////////// Shared Memory/////////////////////
    shm_key = ftok("oss.cpp", 0);
    if (shm_key <= 0)
    {
        fprintf(stderr, "Parent:... Error in ftok\n");
        exit(1);
    }

    shm_id = shmget(shm_key, BUFF_SZ, 0700 | IPC_CREAT);
    if (shm_id <= 0)
    {
        fprintf(stderr, "Parent:... Error in shmget\n");
        exit(1);
    }

    customClock = (int *)shmat(shm_id, 0, 0);
    if (customClock == (int *)-1)
    {
        fprintf(stderr, "Parent:... Error in shmat\n");
        exit(1);
    }
    int *sec = &(customClock[0]);
    int *nano = &(customClock[1]);
    *sec = *nano = 0;

    system("touch msgq.txt");
    key_t msgkey = ftok("msgq.txt", 1);

    msgqid = msgget(msgkey, PERMS | IPC_CREAT);

    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(3);

    initProcessTable();

    int launched = 0, active = 0;

    int lastPrintSec = 0, lastPrintNano = 0;
    int lastLaunchSec = 0, lastLaunchNano = 0;

    int intervalSec = (int)interval;
    int intervalNano = (interval - intervalSec) * BILLION;

    // still children to launch or children in system
    while (launched < n || active > 0)
    {

        // printing every 0.5 sec
        int printSec = lastPrintSec;
        int printNano = lastPrintNano + 500000000;
        normalizeTime(printSec, printNano);

        if (timeReached(printSec, printNano))
        {
            printOnScreen();
            lastPrintSec = *sec;
            lastPrintNano = *nano;
        }

        // checking if we can launch
        int nextLaunchSec = lastLaunchSec + intervalSec;
        int nextLaunchNano = lastLaunchNano + intervalNano;
        normalizeTime(nextLaunchSec, nextLaunchNano);
        if (launched < n &&
            active < s &&
            (launched == 0 || timeReached(nextLaunchSec, nextLaunchNano)))
        {
            for (int i = 0; i < MAXIMUM_PROCESS; i++)
            {
                if (!processTable[i].occupied)
                {

                    pid_t worker = fork();

                    if (worker == 0)
                    {

                        execl("./worker", "./worker", NULL);
                        exit(1);
                    }
                    processTable[i].occupied = 1;
                    processTable[i].pid = worker;
                    processTable[i].startSeconds = *sec;
                    processTable[i].startNano = *nano;

                    logmsg("OSS Generating P" + to_string(launched) + "\n");

                    int totalruntime = rand() % (int)(t * BILLION);
                    msgbuffer msg;
                    msg.mtype = worker;
                    msg.intData = totalruntime;
                    msg.flag = INIT;

                    msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0);
                    logmsg("OSS: Sent INIT to PID " + to_string(worker) + "\n");

                    // message with quantum
                    msg.mtype = worker;
                    msg.intData = QUANTUM;
                    msg.flag = RUN;

                    msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0);

                    logmsg("OSS: Sent RUN to PID " + to_string(worker) + "\n");

                    msgrcv(msgqid, &msg, sizeof(msg) - sizeof(long), getpid(), 0);

                    logmsg("OSS: Received from PID " + to_string(worker) +
                           " used time = " + to_string(msg.intData) + "\n");

                    // wait and cleanup PCB
                    waitpid(worker, NULL, 0);

                    processTable[i].occupied = 0;
                    active--;
                    launched++;
                    lastLaunchSec = *sec;
                    lastLaunchNano = *nano;
                    logmsg("OSS: Launched worker PID " + to_string(worker) + "\n");
                    break;
                }
            }
        }
    }
}
