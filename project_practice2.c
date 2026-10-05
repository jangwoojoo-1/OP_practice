#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// 전역 변수
int global_value = 100;
int global_count = 10;

// // Mutex 자물쇠 초기화 (전역 변수 보호용)
// // 찾아보니 동기화를 위해 mutex 라는 자물쇠가 있는 것을 확인
// // 일단 코드에 적용 X
// pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//Thread1 : 전역 변수 증가
void* plusValueThread(void* arg){
  // main 함수의 void *retval에 반환하기 위해서 8비트 long 사용
  // void *도 8비트기 때문에 주소인척 count 값을 바꿔서 main 영역의 retval에 전달
  long count = 0;
  while(1){
    // // 자물쇠 잠금: 다른 스레드가 global_value에 접근하지 못하게 막음
    // pthread_mutex_lock(&mutex);

    int old_value = global_value;
    global_value++;
    printf("[Thread1] increase global_value : %d + 1 = %d\n", old_value, global_value); 
    
    // // 자물쇠 해제: 다른 스레드가 접근할 수 있도록 열어줌
    // pthread_mutex_unlock(&mutex);

    sleep(1);
    count++;
    if(count == global_count) {
      return (void*)count;
    }
  }
}

// Thread2 : 전역 변수 감소 
void* minusValueThread(void* arg){
  long count = 0;
  while(1){
    // pthread_mutex_lock(&mutex);

    int old_value = global_value;
    global_value--;
    printf("[Thread2] decrease global_value : %d - 1 = %d\n", old_value, global_value); 
    
    // pthread_mutex_unlock(&mutex);

    sleep(1);
    count++;
    if(count == global_count) {
      return (void*)count;
    }
  }
}


int main(){
  // Multi-threading을 위함
  pthread_t p_thread[2];
  // 오류 확인. 0이면 thread 생성 성공
  int thread_id;
  // pthread_join() 함수에서 void* 받을 변수
  void* retval;

  // thread1 생성
  thread_id = pthread_create(&p_thread[0], NULL, plusValueThread, NULL);
  if(thread_id != 0){
    perror("Thead create error : ");
    exit(0);
  }

  // thread2 생성
  thread_id = pthread_create(&p_thread[1], NULL, minusValueThread, NULL);
  if(thread_id != 0){
    perror("Thead create error : ");
    exit(0);
  }

  // thread1 종료 기다리기
  pthread_join(p_thread[0], &retval);
  printf("[Thread1] 반복 횟수 : %ld\n", (long)retval);
  
  //thread2 종료 기다리기
  pthread_join(p_thread[1], &retval);
  printf("[Thread2] 반복 횟수 : %ld\n", (long)retval);

  // 메인 프로세스 종료
  return 0;
}