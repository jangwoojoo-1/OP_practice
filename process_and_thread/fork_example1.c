#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(){
  int pid;
  printf("#Parent process [PID:%d] is ready.\n", getpid());
  for(int i = 0; i < 3; i++){
    printf(".\n");
    sleep(1);
  }
  pid = fork();
  if(pid == 0){ // Child process
    printf("\t*Child process [PID:%d]: This is child process.\n", getpid());
    printf("\t*Child process [PID:%d]: Child process finished.\n", getpid());
    exit(0);
  } else if(pid > 0){ // Parent process
    printf("\t*Parent process [PID:%d]: Child process [PID:%d] is created.\n", getpid(), pid);
    printf("\t*Parent process [PID:%d]: Parent process finished.\n", getpid());
  } else{ // On failure
    printf("fork() system call failed!\n");
    return -1;
  }
  
  return 0;
}