#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM 10

void child_process(int i){
  printf("process pid -- %d, %d'st child process started\n", getpid(), i+1);
  printf("process pid -- %d, %d'st child process executing\n", getpid(), i+1);
  printf("process pid -- %d, %d'st child process ended\n", getpid(), i+1);
}

int main(){
  pid_t pids[NUM];

  for(int i = 0; i < NUM; i++){
    pids[i] = fork();

    if(pids[i] < 0){
      printf("Error!\n");
      return -1;
    } else if(pids[i] == 0){
      child_process(i);
    } else{
      printf("process pid -- %d, parent process executing\n", getpid());
    }
  }

  for (int i = 0; i < NUM ; i++){
    wait(NULL);
    //exit(0);
  }

  printf("process pid -- %d, parent process ended\n", getpid());

  return 0;
}

