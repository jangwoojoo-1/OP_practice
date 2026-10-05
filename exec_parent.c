#include <stdio.h>
#include <unistd.h>
#include <wait.h>

int main(){
  int pid, status;
  printf("This is parent process!\n");
  pid=fork();

  if(pid == 0){ // Child process
    execl("exec_child", "exec_child", NULL);
  } 
  
  wait(&status);
  printf("Parent process finished!\n");

  return 0;
}