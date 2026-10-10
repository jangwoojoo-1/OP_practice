#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

struct msgbuf{ // message data structure
  long mtype;
  int mdata;
};

int main(){
  clock_t start, finish;
  double duration;

  key_t key = 1111;
  start = clock();

  int msqid = msgget(key, 0666 | IPC_CREAT); // Create message queue space
  if(msqid == -1){ // Creating message queue failed
    perror("msgget failed");
    exit(1);
  }

  pid_t pid = fork(); // create child process
  if(pid == 0){ // if child
    struct msgbuf buf;
    while(1){
      if(msgrcv(msqid, &buf, sizeof(int), 1, IPC_NOWAIT) != -1){ // receive message at the &buf / int size / No message -> return -1
        printf("Child reads: %d\n", buf.mdata);
      }
      sleep(1);
    }
  } else if(pid > 0){ // if parent
    struct msgbuf buf;
    buf.mtype = 1;
    buf.mdata = 0; // 공유 메모리 프로세스와 같게 하기 위해 초기화 후 ++ 하는 방식으로 프로세스 변경
    for(int i = 0; i < 10; i++){
      buf.mdata++;
      printf("Parent changed data to %d\n", buf.mdata);
      msgsnd(msqid, &buf, sizeof(int), 0); // 0 전달 시 대기 모드로 작동
      // Send message -> &buf, int size
      sleep(1);
    }
    kill(pid, SIGKILL); // after for(), kill child process
    msgctl(msqid, IPC_RMID, NULL); // Removes IPC memory information

    finish = clock();
    duration = (double)(finish - start) / CLOCKS_PER_SEC;
    printf("Parent process completed.\n");
    printf("실행 시간: %f 초\n", duration);
  } else{
    perror("Parent process completed.\n");
    exit(1);
  }

  // Main process terminates.
  return 0;
}