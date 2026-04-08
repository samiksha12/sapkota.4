#include <iostream>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <cstdlib>

using namespace std;

#define INIT 1
#define RUN 2

struct msgbuffer
{
    long mtype;
    int intData;
    int flag;
};

int main()
{

    srand(getpid());

    int msgqid = msgget(ftok("msgq.txt", 1), 0666);

    msgbuffer msg;

    int totalNeeded = 0;
    int totalUsed = 0;

    cout << "WORKER PID " << getpid() << " started\n";

    while (true)
    {

        msgrcv(msgqid, &msg, sizeof(msg) - sizeof(long), getpid(), 0);

        if (msg.flag == INIT)
        {
            totalNeeded = msg.intData;

            cout << "WORKER " << getpid()
                 << " INIT total=" << totalNeeded << endl;
        }
        else if (msg.flag == RUN)
        {

            int quantum = msg.intData;
            int decision = rand() % 100;
            int used;
            int remaining = totalNeeded - totalUsed;
            if (remaining <= quantum)
            {
                used = -remaining; // 0 - if negative oss will know it is terminating message
                cout << "WORKER " << getpid() << " TERMINATING\n";
            }
            else if (decision < 20)
            {
                used = rand() % quantum; // 2 - use part & block
                cout << "WORKER " << getpid() << " BLOCKED\n";
            }
            else
            {
                used = quantum; // 1 - use it all
                cout << "WORKER " << getpid() << " FULL QUANTUM\n";
            }

            totalUsed += abs(used);

            msg.mtype = getppid();
            msg.intData = used;

            msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0);
            if (used < 0)
            {
                break;
            }
        }
    }

    return 0;
}