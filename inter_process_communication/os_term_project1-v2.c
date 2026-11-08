/*
 * OS Term Project #1: CPU Scheduling-Enhanced Round-Robin Simulator (v2)
 *
 * Grounded in:
 * - PA1 (Process Management: fork, wait, kill, exit)
 * - PA2 (IPC: System V Message Queue, msgsnd, msgrcv, IPC_NOWAIT)
 * - CPU Scheduling Lecture Materials (OS_7-8_CPU Scheduling):
 *   * CPU-I/O Burst Cycle Simulation
 *   * Preemptive Round-Robin Scheduling with Time Quantum (q=3)
 *   * Scheduling Criteria Evaluation: CPU Utilization, Throughput,
 *     Average Waiting Time, Turnaround Time, Response Time, Context Switch Overhead.
 *
 * Build: gcc -Wall -Wextra -o os_term_project1-v2 os_term_project1-v2.c
 * Execution: ./os_term_project1-v2
 */

// 헤더 파일
#include <stdio.h>      // 표준 입출력
#include <stdlib.h>     // 메모리 
#include <unistd.h>     // POSIX 시스템 호출 : fork(), getpid() 등
#include <sys/types.h>  // 시스템 자료형 pid_t
#include <sys/wait.h>   // 자식 프로세스 회수 wait()
#include <sys/ipc.h>    // IPC 관련 정의 
#include <sys/msg.h>    // 메시지 큐 msgget(), msgsnd() 등
#include <sys/time.h>   // 시간 및 타이머 settimer()
#include <signal.h>     // 시그널 처리 : kill()
#include <time.h>       // 시간 정보
#include <fcntl.h>      
#include <string.h>
#include <errno.h>      // 

#define CHILDREN_NUM 10         // 생성할 자식 프로세스 개수 10개
#define TIME_QUANTUM 3         // RR 타임 퀀텀 (q = 3 ticks)
#define MAX_TICKS 10000        // 총 시뮬레이션 타임 틱 (10,000 ticks)
#define MSG_KEY 0x20261107     // 메시지 큐 식별용 키

// 프로세스 현재 상태를 표현하기 위한 열거형
typedef enum { 
    STATE_READY,    // 실행 대기
    STATE_RUNNING,  // 실행
    STATE_WAITING   // 대기 - I/O 작업 중
} ProcessState;

// IPC 메시지 구조체
// (1) 부모 메시지 - CPU 실행을 허가할 때 전송
struct msg_p2c { 
    long mtype; // 메시지 유형 (자식 프로세스의 PID)
    int tick_granted; // CPU 실행 허가 TIme Quantum
};

// (2) 자식 메시지 - CPU BURST 완료하면 I/O 작업 요청을 위해 전송
struct msg_c2p { 
    long mtype; // 메시지 유형
    pid_t pid;  // 메시지 보낸 자식 pid
    int child_idx; // 자식 프로세스 배열 인덱스
    int io_burst;  // I/O burst 시간
};

// PCB (Process Control Block)
// 운영체제가 각 프로세스를 관리하기 위한 정보를 저장
// CPU 스케줄링 평가 지표 포함
typedef struct {
    pid_t pid;                  // 자식 프로세스의 PID
    int child_idx;              // PCB 배열에서의 인덱스
    ProcessState state;         // 프로세스 상태(READY, RUNNING, WAITING)
    int cpu_burst;              // CPU BURST 정보
    int io_burst;               // I/O BURST 정보
    int remaining_quantum;      // 남은 TIME_QUANTUM
    
    // CPU 스케줄링 평가 지표
    int ready_enter_tick;     // Ready queue 들어온 시간
    int total_waiting_time;   // 누적 대기 시간
    int total_turnaround_time;// 누적 반환 시간
    int total_response_time;  // 누적 응답 시간
    int completed_bursts;     // 완료한 CPU burst 수
    int context_switches;     // 문맥 교환 횟수
    int first_dispatch;       // 첫 디스패치 여부
} PCB;

static volatile sig_atomic_t timer_tick_flag = 0; 
// volatile : 컴파일러가 변수 접근을 임의로 최적화하지 않도록 함
// sig_atomic_t : 시그널 핸들러와 일반 실행 흐름 사이에서 원자적으로 접근할 수 있는 정수형
// -> 시그널 핸들러가 변경하는 변수의 안정적인 접근을 위해 사용한 것
static int tick_count = 0;
static int msqid = -1; // System V Message Queue id
static int log_fd = -1;

// 시그널 발생 시 실행 함수
void sigalrm_handler(int sig) {
    (void)sig;
    timer_tick_flag = 1; // 시그널 발생 시 timer_tick_flag를 1로 변경
}

// 자식 프로세스 루틴 (CPU-I/O Burst Cycle 무한 루프)
void child_routine(int idx, int msqid) {
    // pid 가져와 난수 생성기 초기값을 설정
    pid_t pid = getpid(); 
    srand(pid ^ time(NULL)); // PID와 현재 시간을 XOR 연산(^)하여 프로세스마다 서로 다른 난수 시퀀스를 생성

    // 랜덤 생성
    int cpu_burst = (rand() % 7) + 3;
    int io_burst = (rand() % 5) + 2;

    struct msg_p2c p2c_msg;
    struct msg_c2p c2p_msg;

    while (1) {
        // 부모 프로세스로부터 CPU 타임 슬라이스 수신 (msgrcv)
        if (msgrcv(msqid, &p2c_msg, sizeof(p2c_msg) - sizeof(long), pid, 0) == -1) {
            if (errno == EIDRM || errno == EINVAL) break;
            continue;
        }

        cpu_burst--;

        // CPU Burst 완료 시 부모에게 I/O 요청 전송
        if (cpu_burst <= 0) {
            c2p_msg.mtype = 1;
            c2p_msg.pid = pid;
            c2p_msg.child_idx = idx;
            c2p_msg.io_burst = io_burst;

            if (msgsnd(msqid, &c2p_msg, sizeof(c2p_msg) - sizeof(long), 0) == -1) {
                if (errno == EIDRM || errno == EINVAL) break;
            }

            // 다음 Burst Cycle 생성
            cpu_burst = (rand() % 7) + 3;
            io_burst = (rand() % 5) + 2;
        }
    }
    exit(0);
}

int main() {
    // 1. System V Message Queue 초기화
    msqid = msgget(MSG_KEY, 0666 | IPC_CREAT); // 메시지 큐 공간 생성
    msgctl(msqid, IPC_RMID, NULL);
    msqid = msgget(MSG_KEY, 0666 | IPC_CREAT);

    // 2. 저수준 I/O 시스템 콜로 schedule_dump.txt 열기
    log_fd = open("schedule_dump.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // PCB 배열
    PCB pcb[CHILDREN_NUM];
    // 자식 프로세스 배열
    pid_t pids[CHILDREN_NUM];

    // 3. fork()로 10개 자식 프로세스 생성 및 PCB 초기화
    for (int i = 0; i < CHILDREN_NUM; i++) {
        pids[i] = fork();
        if (pids[i] == 0) { // 자식일 경우, 자식 작업 수행
            child_routine(i, msqid);
        } else { // 부모일 경우, 생성된 자식을 바탕으로 PCB 데이터 초기화
            pcb[i].pid = pids[i];
            pcb[i].child_idx = i;
            pcb[i].state = STATE_READY;
            pcb[i].io_burst = 0;
            pcb[i].remaining_quantum = TIME_QUANTUM;
            pcb[i].ready_enter_tick = 0;
            pcb[i].total_waiting_time = 0;
            pcb[i].total_turnaround_time = 0;
            pcb[i].total_response_time = 0;
            pcb[i].completed_bursts = 0;
            pcb[i].context_switches = 0;
            pcb[i].first_dispatch = 0;
        }
    }

    // Run-Queue (원형 큐) 및 Wait-Queue
    int run_queue[CHILDREN_NUM], run_q_front = 0, run_q_rear = 0, run_q_count = 0; // READY 상태의 프로세스
    int wait_queue[CHILDREN_NUM], wait_q_count = 0; // WAITING 상태의 프로세스

    // 원형 큐 초기화
    for (int i = 0; i < CHILDREN_NUM; i++) {
        run_queue[run_q_rear] = i;
        run_q_rear = (run_q_rear + 1) % CHILDREN_NUM;
        run_q_count++;
    }

    // 4. setitimer 타이머 설정 (주기적 SIGALRM 발생)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval timer;
    timer.it_interval.tv_sec = 0; 
    timer.it_interval.tv_usec = 1000; // 반복 간격 1000마이크로초
    timer.it_value.tv_sec = 0;    
    timer.it_value.tv_usec = 1000; // 만료 시간 1000 마이크로초
    setitimer(ITIMER_REAL, &timer, NULL); // SIGALRM 발생

    int current_running_idx = -1; // 현재 CPU를 사용하는 프로세스 인덱스
    int total_busy_ticks = 0;     // CPU가 사용된 Tick 수
    int total_context_switches = 0; // 디스패치 횟수로 집계되는 값 

    // 5. 10,000 Tick 시뮬레이션 메인 루프
    while (tick_count < MAX_TICKS) {
        if (!timer_tick_flag) {  // 아직 tick이 발생하지 않은 경우
            usleep(100); // 100 마이크로초 동안 대기
            continue; 
        }
        timer_tick_flag = 0;
        tick_count++; // Tick 발생

        // Ready 큐에 있는 모든 프로세스의 대기 시간 증가
        for (int i = 0; i < run_q_count; i++) {
            int q_idx = (run_q_front + i) % CHILDREN_NUM;
            pcb[run_queue[q_idx]].total_waiting_time++;
        }

        // 5-1. 자식 프로세스 I/O 요청 메시지 비블로킹 감지 (IPC_NOWAIT)
        struct msg_c2p c2p_msg;
        while (msgrcv(msqid, &c2p_msg, sizeof(c2p_msg) - sizeof(long), 1, IPC_NOWAIT) != -1) {
            int c_idx = c2p_msg.child_idx; // 메시지에서 자식 프로세스 인덱스 가져와
            pcb[c_idx].io_burst = c2p_msg.io_burst; // pcb에 i/o 정ㅈ보 저장
            pcb[c_idx].state = STATE_WAITING;       // process waiting 상태로 변경
            pcb[c_idx].completed_bursts++;          // 완료된 cpu burst 횟수 증가
            
            if (current_running_idx == c_idx) {     // 만약 i/o burst 발생한 프로세스가 현재 실행 중인 프로세스면
                current_running_idx = -1; // CPU 선점 해제
            }
            wait_queue[wait_q_count++] = c_idx; // 대기 큐에 추가
        }

        // 5-2. Wait-Queue I/O 잔여시간 차감 및 완료 처리
        int new_wait_q[CHILDREN_NUM], new_wait_count = 0;
        for (int i = 0; i < wait_q_count; i++) {
            int idx = wait_queue[i];
            pcb[idx].io_burst--;
            if (pcb[idx].io_burst <= 0) {
                pcb[idx].io_burst = 0;
                pcb[idx].state = STATE_READY;
                pcb[idx].remaining_quantum = TIME_QUANTUM;
                pcb[idx].ready_enter_tick = tick_count;
                run_queue[run_q_rear] = idx;
                run_q_rear = (run_q_rear + 1) % CHILDREN_NUM;
                run_q_count++;
            } else { // i/o가 끝나지 않으면 다시 대기 큐에 삽입
                new_wait_q[new_wait_count++] = idx;
            }
        }
        wait_q_count = new_wait_count;
        for (int i = 0; i < wait_q_count; i++) wait_queue[i] = new_wait_q[i];

        // 5-3. Round-Robin Time Quantum 만료 및 Preemption
        if (current_running_idx != -1) {
            pcb[current_running_idx].remaining_quantum--;
            if (pcb[current_running_idx].remaining_quantum <= 0) {
                // 선점(Preemption) 발생
                pcb[current_running_idx].state = STATE_READY;
                pcb[current_running_idx].remaining_quantum = TIME_QUANTUM;
                pcb[current_running_idx].ready_enter_tick = tick_count;
                run_queue[run_q_rear] = current_running_idx;
                run_q_rear = (run_q_rear + 1) % CHILDREN_NUM;
                run_q_count++;
                current_running_idx = -1;
            }
        }

        // 5-4. Ready-Queue에서 다음 프로세스 디스패치 (Dispatcher)
        if (current_running_idx == -1 && run_q_count > 0) {
            current_running_idx = run_queue[run_q_front];
            run_q_front = (run_q_front + 1) % CHILDREN_NUM;
            run_q_count--;
            
            pcb[current_running_idx].state = STATE_RUNNING;
            pcb[current_running_idx].context_switches++;
            total_context_switches++;

            if (!pcb[current_running_idx].first_dispatch) {
                pcb[current_running_idx].total_response_time += (tick_count - pcb[current_running_idx].ready_enter_tick);
                pcb[current_running_idx].first_dispatch = 1;
            }
        }

        // 5-5. CPU 실행 및 IPC 메시지 할당
        if (current_running_idx != -1) {
            total_busy_ticks++;
            struct msg_p2c p2c_msg;
            p2c_msg.mtype = pcb[current_running_idx].pid;
            p2c_msg.tick_granted = 1;
            msgsnd(msqid, &p2c_msg, sizeof(p2c_msg) - sizeof(long), 0);
        }

        // 5-6. schedule_dump.txt 로그 기록
        char buf[1024], run_str[256] = "", wait_str[256] = "";
        for (int i = 0; i < run_q_count; i++) {
            int q_idx = (run_q_front + i) % CHILDREN_NUM;
            char tmp[64]; snprintf(tmp, sizeof(tmp), "P%d(PID:%d) ", run_queue[q_idx], (int)pcb[run_queue[q_idx]].pid);
            strcat(run_str, tmp);
        }
        for (int i = 0; i < wait_q_count; i++) {
            char tmp[64]; snprintf(tmp, sizeof(tmp), "P%d(IO_rem:%d) ", wait_queue[i], pcb[wait_queue[i]].io_burst);
            strcat(wait_str, tmp);
        }

        int curr_pid = (current_running_idx != -1) ? pcb[current_running_idx].pid : -1;
        int curr_rem_q = (current_running_idx != -1) ? pcb[current_running_idx].remaining_quantum : 0;

        snprintf(buf, sizeof(buf),
                 "[Time Tick: %5d] Scheduled: P%d (PID: %d) | Rem Quantum: %d\n"
                 "                 Run-Queue  : [ %s]\n"
                 "                 Wait-Queue : [ %s]\n\n",
                 tick_count, current_running_idx, curr_pid, curr_rem_q, run_str, wait_str);
        write(log_fd, buf, strlen(buf));
    }

    // 6. CPU Scheduling Performance Evaluation Summary 계산 및 출력
    double cpu_utilization = ((double)total_busy_ticks / MAX_TICKS) * 100.0;
    int total_bursts = 0;
    long total_wait_sum = 0, total_resp_sum = 0;

    for (int i = 0; i < CHILDREN_NUM; i++) {
        total_bursts += pcb[i].completed_bursts;
        total_wait_sum += pcb[i].total_waiting_time;
        total_resp_sum += pcb[i].total_response_time;
    }

    double avg_waiting_time = (double)total_wait_sum / CHILDREN_NUM;
    double avg_response_time = (double)total_resp_sum / CHILDREN_NUM;
    double throughput = ((double)total_bursts / MAX_TICKS) * 1000.0; // bursts per 1000 ticks

    char summary_buf[2048];
    snprintf(summary_buf, sizeof(summary_buf),
        "\n=========================================================\n"
        "      CPU SCHEDULING PERFORMANCE EVALUATION SUMMARY      \n"
        "=========================================================\n"
        " Algorithm             : Preemptive Round-Robin (q = %d)\n"
        " Total Simulation Ticks: %d ticks\n"
        " Number of Processes   : %d Processes\n"
        "---------------------------------------------------------\n"
        " 1. CPU Utilization   : %.2f %%\n"
        " 2. Throughput        : %.2f bursts / 1,000 ticks\n"
        " 3. Total Bursts Done : %d bursts\n"
        " 4. Avg Waiting Time  : %.2f ticks / process\n"
        " 5. Avg Response Time : %.2f ticks / process\n"
        " 6. Context Switches  : %d times\n"
        "=========================================================\n",
        TIME_QUANTUM, MAX_TICKS, CHILDREN_NUM,
        cpu_utilization, throughput, total_bursts,
        avg_waiting_time, avg_response_time, total_context_switches);

    write(log_fd, summary_buf, strlen(summary_buf));
    printf("%s", summary_buf);

    // 7. 자식 프로세스 회수 및 메시지 큐 삭제 (PA1/PA2 정리)
    for (int i = 0; i < CHILDREN_NUM; i++) {
        kill(pids[i], SIGKILL);
        wait(NULL);
    }
    msgctl(msqid, IPC_RMID, NULL);
    close(log_fd);

    return 0;
}
