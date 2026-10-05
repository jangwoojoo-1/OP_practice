#include <stdio.h>
#include <unistd.h>
#include <wait.h>

int main(){
  int pid, count, child_pid, status;
  printf("#Parent process [PID:%d] is ready.\n", getpid());
  for(int i = 0; i < 3; i++){
    printf(".\n");
    sleep(1);
  }
  for(count = 0; count < 10; count++){
    pid=fork();
    if(pid == 0){ // Child process
      break;
    } else if (pid > 0){ // Parent process
      printf("#Parent process [PID:%d]: Child process No.%d is created.\n", getpid(), pid);
    } else { // On failure
      printf("fork() system call failed!\n");
      return -1;
    }
  }
  
  if(pid == 0){ // Child process
    sleep(2);
    for(int i = 0; i < 5; i++){
      printf("\t*Child process [PID:%d] is working: Loop %d\n", getpid(), i+1);
      sleep(2);
    }
  } else{ // Parent Process
    while(count > 0){
      child_pid = wait(&status);
      printf("\t*Child process [PID:%d] finished.\n", child_pid);
      count--;
      sleep(1);
    }

    printf("#Parent process [PID:%d]: finished.\n", getpid());
  }
  
  return 0;
}