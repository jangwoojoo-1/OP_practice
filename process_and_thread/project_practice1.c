#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define CHILDREN_NUM 10

void printChildren(int i){
  printf("\t[PID - %d] Child process[%d] created\n", getpid(), i+1);
  printf("\t[PID - %d] Child process[%d] executing\n", getpid(), i+1);
  // 4. sleep() 함수 활용 대기
  sleep(2);
  printf("\t[PID - %d] Child process[%d] terminated\n\n", getpid(), i+1);
  // 4. exit() 함수 활용 종료
  exit(0);
}

int main(){
  // 1. Parent process PID 출력
  printf("----[PID - %d] Parent process created.----\n\n", getpid());
  pid_t pids[CHILDREN_NUM];

  // 2. Child process 10개 생성
  for(int i = 0; i < CHILDREN_NUM; i++){
    // fork() 함수 활용 Child process 생성
    pids[i] = fork();

    if(pids[i] < 0){ // 문제 발생 시
      printf("Error!\n");
      return -1;
    } else if(pids[i] == 0){ // 자식 프로세스일 경우
      // 3. Child process PID 출력
      printChildren(i);
    } else {
      // 5. wait 함수 활용해 각 자식 프로세스가 종료할 때까지 메인 프로세스 대기
      // 다만 자식 프로세스 sleep으로 인해 동시성 상실(약 20초 걸림) -> 출력이 깔끔해서 이렇게 실행
      // 생성 종료가 메인인 과제라서 이렇게 구현
      wait(NULL);
    }
  }
  
  // // 5. Child process 10개 종료까지 대기 -> 비동기(약 2초 걸림)
  // for(int i = 0; i < CHILDREN_NUM; i++){
  //   wait(NULL);
  // }

  // 5. Parent process 종료
  printf("----[PID - %d] Parent process terminated.----\n\n", getpid());
  return 0;
}