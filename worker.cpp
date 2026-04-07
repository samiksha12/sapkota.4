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
    int msgqid = msgget(ftok("msgq.txt", 1), 0666);

    msgbuffer msg;

    int totalTime = 0;

    while (true)
    {
        // wait for message from OSS
        msgrcv(msgqid, &msg, sizeof(msg) - sizeof(long), getpid(), 0);

        if (msg.flag == INIT)
        {
            totalTime = msg.intData;

            cout << "WORKER " << getpid()
                 << " received INIT total time: "
                 << totalTime << endl;
        }
        else if (msg.flag == RUN)
        {
            int quantum = msg.intData;

            cout << "WORKER " << getpid()
                 << " received RUN quantum: "
                 << quantum << endl;

            int used = quantum;

            msg.mtype = getppid();
            msg.intData = used;

            msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0);

            cout << "WORKER " << getpid()
                 << " sending back used time: "
                 << used << endl;

            break; // exit after one cycle
        }
    }

    return 0;
}