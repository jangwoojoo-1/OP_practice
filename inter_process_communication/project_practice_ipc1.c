#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h> // 시간 한 번 재보려고 추가

struct sharedData { // 공유 메모리 데이터 -> 메세지 큐와 같은 구조를 만들기 위해서
  long shtype;
  int shdata;
};

int main(){
  clock_t start, finish;
  double duration;

  key_t key = 1111;
  start = clock(); // 측정 시작
  int shmid = shmget(key, sizeof(struct sharedData), 0666|IPC_CREAT); // Create shared memory space
  if(shmid == -1){ // Shared memory creation failed
    perror("shmget failed"); 
    exit(1);
  }

  struct sharedData* data = (struct sharedData*) shmat(shmid, (void*)0, 0); // Attaches the memory from the process
  if(data == (struct sharedData*)(-1)){ // if shmat failed, it returns -1
    perror("shmat failed");
    exit(1);
  }

  data->shtype = 0; // 현재는 그냥 0으로 지정, 나중에 오류 찾을 때 사용 가능
  data->shdata = 0; // initializing memory values
  pid_t pid = fork(); // create child process

  if(pid == 0){ // if child
    while(1){
      printf("Child reads: %d\n", data->shdata);
      sleep(1);
    }
  } else if(pid > 0){ // if parent
    for(int i = 0; i < 10; i++){
      data->shdata++; // increases the memory 10 times
      printf("Parent changed data to %d\n", data->shdata);
      sleep(1);
    }
    kill(pid, SIGKILL); // kill child process
    shmdt(data); // detaches the memory from the process
    shmctl(shmid, IPC_RMID, NULL); // removes shared memory information
    finish = clock();
    duration = (double)(finish - start) / CLOCKS_PER_SEC;
    printf("Parent process completed.\n");
    printf("실행 시간: %f 초\n", duration); // clock() cpu 점유시간만 재기 때문에 sleep()시간 제외됨.
  } else{
    perror("fork failed");
    exit(1);
  }

  return 0;
}