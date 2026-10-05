#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

int main(){
  key_t key = 1234;
  int shmid = shmget(key, sizeof(int), 0666|IPC_CREAT); // Create shared memory space
  if(shmid == -1){ // Shared memory creation failed
    perror("shmget failed"); 
    exit(1);
  }

  int* data = (int*) shmat(shmid, (void*)0, 0); // Attaches the memory from the process
  if(data == (int*)(-1)){ // if shmat failed, it returns -1
    perror("shmat failed");
    exit(1);
  }

  *data = 0;
  pid_t pid = fork();

  if(pid == 0){
    while(1){
      printf("Child reads: %d\n", *data);
      sleep(1);
    }
  } else if(pid > 0){
    for(int i = 0; i < 10; i++){
      (*data)++;
      printf("Parent changed data to %d\n", (*data));
      sleep(1);
    }
    kill(pid, SIGKILL); // kill child process
    shmdt(data); // detaches the memory from the process
    shmctl(shmid, IPC_RMID, NULL); // removes shared memory information
    printf("Parent process completed.\n");
  } else{
    perror("fork failed");
    exit(1);
  }

  return 0; // main process terminates
}