#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(){
  int pid, status;
  printf("This is child process!\n");

  for(int i = 0; i < 5; i++){ 
    sleep(1);
    printf(".\n");
  }
  printf("Child process finished!\n");
  
  return 0;
}