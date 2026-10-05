#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

struct msgbuf{
  long mtype;
  int mdata;
};

int main(){
  key_t key = 1234;
  int msqid = msgget(key, 0666 | IPC_CREAT); // Create message queue
  if(msqid == -1){ // Creating message queue failed
    perror("msgget failed");
    exit(1);
  }

  pid_t pid = fork();
  if(pid == 0){
    struct msgbuf buf;
    while(1){
      if(msgrcv(msqid, &buf, sizeof(int), 1, IPC_NOWAIT) != -1){ // receive message at the &buf, int size, No message -> return -1
        printf("Child reads: %d\n", buf.mdata);
      }
      sleep(1);
    }
  } else if(pid > 0){
    struct msgbuf buf;
    buf.mtype = 1;
    for(int i = 0; i < 10; i++){
      buf.mdata = i + 1;
      printf("Parent changed data to %d\n", buf.mdata);
      msgsnd(msqid, &buf, sizeof(int), 0); // 0 == IPC_CREAT
      // Send message &buf, int size
      sleep(1);
    }
    kill(pid, SIGKILL); // after for(), kill child process
    msgctl(msqid, IPC_RMID, NULL); // Removes IPC memory information
    printf("Parent process completed.\n");
  } else{
    perror("Parent process completed.\n");
    exit(1);
  }

  // Main process terminates.
  return 0;
}