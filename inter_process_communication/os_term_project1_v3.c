#define _DEFAULT_SOURCE

// 1. 헤더 파일 정의
    #include <stdio.h>      // 표준 입출력    
    #include <stdlib.h>     // 메모리 : rand(), exit() 등
    #include <stdbool.h>    // Boolean 자료형
    #include <unistd.h>     // POSIX 시스템 호출 : fork(), getpid() 등
    #include <time.h>       // 시간 정보 : time()
    #include <errno.h>      // 시스템 오류 확인 : errno
    #include <signal.h>     // 시그널 처리 : kill() 등
    #include <string.h>     // 문자열 처리

    #include <sys/types.h>  // 시스템 자료형 : pid_t
    #include <sys/ipc.h>    // IPC 관련 정의
    #include <sys/msg.h>    // 메시지 큐 : msgget(), msgsnd() 등
    #include <sys/wait.h>   // 자식 프로세스 관리 : wait()
    #include <sys/time.h>   // 시간 및 타이머 : settimer()

// 2. 상수 정의
    #define NUM_CHILDREN 10     // 생성할 자식 프로세스 개수 10개
    #define TIME_QUANTUM 3      // RR 타임 퀸텀
    #define MAX_TICKS 10000     // 총 시뮬레이션 타임 틱
    #define MIN_CPU_BURST 3    
    #define MAX_CPU_BURST 9
    #define MIN_IO_BURST 2
    #define MAX_IO_BURST 6
    #define AGING_THRESHOLD 10  // 에이징 발생 대기 시간
    #define MSG_STOP -1         // 자식 프로세스 종료 메시지


// 3. 열거형 정의
    // 프로세스 현재 상태를 표현하기 위한 열겨형
    typedef enum { 
        READY,    // 실행 대기
        RUNNING,  // 실행
        WAITING   // 대기 - I/O 작업 중
    } ProcessState;

    // 큐 위치 표현하기 위한 열거형
    typedef enum { 
        QUEUE_NONE, // 어떤 큐에도 들어 있지 않음
        QUEUE_HIGH, // Level 0 BST
        QUEUE_LOW,  // Level 1 링크드 리스트
        QUEUE_WAIT  // I/O Waiting Queue
    } QueueLocation;

// 4. PCB 정의
    typedef struct {
        // 프로세스 식별 정보
        pid_t pid;      // 자식 프로세스의 pid
        int child_idx;  // PCB 배열에서의 인덱스

        // 프로세스 상태 및 우선순위 정보
        ProcessState state;           // 프로세스 상태
        QueueLocation queue_location; // 현재 큐 위치
        int priority_level;           // 우선순위 레벨

        // CPU Burst 및 I/o Burst 정보
        int cpu_burst;          
        int io_burst;
        int remaining_quantum;  // 프로세스에 남아 있는 CPU 할당 시간

        // CPU 스케줄링 통계 정보
        int total_waiting_time;     // 누적 대기 시간
        int total_response_time;    // 누적 응답 시간
        int total_turnaround_time;  // 누적 반환 시간
        int completed_cycles;       // 완료한 Burst 횟수
        int context_switches;       // 문맥 교환 횟수
        bool first_dispatch;        // 첫 디스패치 여부
        int cycle_start_tick;       // 시작 시간 틱
        int ready_enter_tick;       // 프로세스가 READY 쿠에 들어온 시간 틱
    } PCB;

// 5. 자료구조 정의
    // (1) Level 0 자료구조 : AVL Tree
    typedef struct AVLNode {
        int process_idx;         // PCB 배열에서 프로세스의 인덱스
        unsigned long sequence;  // READY 큐에 들어온 순서(우선순위대로 들어옴)
        int height;              // 높이

        struct AVLNode *left;    // 왼쪽 자식 노드
        struct AVLNode *right;   // 오른쪽 자식 노드
    } AVLNode;

    // (2) Level 1 자료구조 : Linked List
    typedef struct ListNode {
        int process_idx;       // PCB 배열에서 프로세스의 인덱스
        struct ListNode *next; // 다음 노드
    } ListNode;

    typedef struct {
        ListNode *head; // OUT 위치
        ListNode *tail; // IN 위치
        int count;      // 노드의 개수
    } LinkedQueue;

    // (3) I/O 대기 큐 : Circular Array
    typedef struct {
        int data[NUM_CHILDREN];     // I/O 대기 프로세스 인덱스 배열
        int front;                  // OUT 위치
        int rear;                   // IN 위치
        int count;                  // 개수
        bool present[NUM_CHILDREN]; // 중복 등록 방지 배열
    } WaitQueue;

    // 통신 Message 구조체
    // 부모 -> 자식 메시지
    typedef struct {
        long mtype;     // 메시지 유형( 자식 프로세스의 pid )
        int child_idx;  // 자식 프로세스 인덱스
        int tick;       // 메시지가 전달된 틱
    } Message;

    // 자식 -> 부모 응답 메시지
    typedef struct {
        long mtype;    // 메시지 유형( 자식 프로세스의 pid )
        int child_idx; // 자식 프로세스 인덱스
        int tick;      // 메시지가 전달한 틱
    } AckMessage;

    /* Scheduler 전체 상태를 하나의 구조체로 관리한다. */
    typedef struct {
        PCB pcb[NUM_CHILDREN];                  // PCB 배열
        AVLNode *high_root;                     // Level 0 큐
        LinkedQueue low_queue;                  // Level 1 큐
        WaitQueue wait_queue;                   // WAIT 큐
        int msqid;                              // 
        int running_idx;                        // CPU를 할당받아 실행 중인 프로세스
        int last_cpu_idx;                       // 마지막 CPU 인덱스
        int tick_count;
        int total_cpu_ticks;
        int total_context_switches;
        int total_dispatches;
        unsigned long enqueue_sequence;
    } Scheduler;


// 6. 전역 변수 정의
    // 스케줄러
    static Scheduler scheduler;

    // SIGALRM으로 발생한 타이머 이벤트를 기록하는 변수
    // volatile : 컴파일러가 임의로 값을 캐싱하지 않도록 함
    // sig_atomic_t : 시그널 처리와 관련해 원자적으로 접근할 수 있도록 사용하는 자료형
    static volatile sig_atomic_t pending_ticks = 0;

// 7. 공통 함수 정의
    // 랜덤 숫자 생성 함수
    static int random_between(int min, int max) {
        return min + rand() % (max - min + 1);
    }

// 8. AVL 트리 함수 정의
    // (1) 높이 계산 : 전달받은 노드의 높이를 반환
    static int avl_height(AVLNode *node) {
        return node ? node->height : 0;
    }

    // (2) 더 큰 값 반환
    static int max_int(int a, int b) {
        return (a > b) ? a : b;
    }

    // (3) 높이 갱신 : 왼쪽과 오른쪽 서브트리 중 더 높은 쪽을 기준으로 계산
    static void avl_update_height(AVLNode *node) {
        if (node)
            node->height = 1 + max_int(avl_height(node->left), avl_height(node->right));
    }

    // (4) Balance Factor 계산 : 균형 상태 확인을 위해
    static int avl_balance_factor(AVLNode *node) {
        return node ? avl_height(node->left) - avl_height(node->right) : 0;
    }

    // (5) R 회전
    static AVLNode *avl_rotate_right(AVLNode *y) {
        AVLNode *x = y->left;
        AVLNode *t2 = x->right;
        x->right = y;
        y->left = t2;
        avl_update_height(y);
        avl_update_height(x);
        return x;
    }

    // (6) L 회전
    static AVLNode *avl_rotate_left(AVLNode *x) {
        AVLNode *y = x->right;
        AVLNode *t2 = y->left;
        y->left = x;
        x->right = t2;
        avl_update_height(x);
        avl_update_height(y);
        return y;
    }

    // (7-1) AVL 트리 노드 삽입(순서에 따라 정렬)
    static AVLNode *avl_insert_node(AVLNode *root, int process_idx, unsigned long sequence, bool *ok) {
        // AVL 트리가 없다면
        if (root == NULL) { 
            AVLNode *node = malloc(sizeof(*node));
            if (!node) { // 노드가 생성되지 않으면
                *ok = false;  // 실패
                return NULL;  // NULL 반환
            }
            // 노드 초기 값 입력
            node->process_idx = process_idx;
            node->sequence = sequence;
            node->height = 1;
            node->left = NULL;
            node->right = NULL;
            return node;
        }

        // 삽입할 위치 탐색
        if (sequence < root->sequence) {
            root->left = avl_insert_node(root->left, process_idx, sequence, ok);
        } else if (sequence > root->sequence) {
            root->right = avl_insert_node(root->right, process_idx, sequence, ok);
        } else {
            *ok = false;
            return root;
        }

        // 삽입 실패 시 루트 반환
        if (!*ok) return root;

        // 높이 갱신 및 균형 확인
        avl_update_height(root);
        int balance = avl_balance_factor(root);
        
        // LL
        if (balance > 1 && sequence < root->left->sequence)
            return avl_rotate_right(root);
        // RR
        if (balance < -1 && sequence > root->right->sequence)
            return avl_rotate_left(root);
        // LR
        if (balance > 1 && sequence > root->left->sequence) {
            root->left = avl_rotate_left(root->left);
            return avl_rotate_right(root);
        }
        // RL
        if (balance < -1 && sequence < root->right->sequence) {
            root->right = avl_rotate_right(root->right);
            return avl_rotate_left(root);
        }
        return root;
    }

    // (7) AVL 트리에 프로세스 삽입
    static bool avl_insert(int process_idx) {
        bool ok = true;
        unsigned long seq = ++scheduler.enqueue_sequence;
        AVLNode *new_root = avl_insert_node(scheduler.high_root, process_idx, seq, &ok);
        if (!ok)
            return false;
        scheduler.high_root = new_root; // Level 0 큐 루트 노드
        return true;
    }

    // (8-1) AVL 트리에서 가장 작은 값 삭제( 우선순위 1위 삭제 )
    static AVLNode *avl_delete_min_node(AVLNode *root, int *process_idx) {
        // 루트가 제일 작을 경우
        if (!root->left) {
            AVLNode *right = root->right;
            *process_idx = root->process_idx;
            free(root);
            return right;
        }
        // 왼쪽으로 이동해서 가장 작은 값 찾기
        root->left = avl_delete_min_node(root->left, process_idx);
        // 삭제 후 높이 갱신 및 균형 확인
        avl_update_height(root);
        int balance = avl_balance_factor(root);
        if (balance > 1) {
            if (avl_balance_factor(root->left) >= 0)
                return avl_rotate_right(root);            // LL
            root->left = avl_rotate_left(root->left);     // LR
            return avl_rotate_right(root);
        }
        if (balance < -1) {
            if (avl_balance_factor(root->right) <= 0)
                return avl_rotate_left(root);             // RR
            root->right = avl_rotate_right(root->right);  // RL
            return avl_rotate_left(root);
        }
        return root;
    }

    // (8) 우선순위가 가장 높은 프로세스 꺼내기
    static int avl_dequeue_min() {
        int process_idx = -1;
        if (!scheduler.high_root) return -1;
        scheduler.high_root = avl_delete_min_node(scheduler.high_root, &process_idx);
        return process_idx;
    }

    // (9) AVL 트리 메모리 해제
    static void avl_free(AVLNode *root) {
        if (!root) return;
        avl_free(root->left);
        avl_free(root->right);
        free(root);
    }


// 9. Linked List 함수 정의
    // (1) 노드 삽입
    static bool low_enqueue(int process_idx) {
        ListNode *node = malloc(sizeof(*node));
        if (!node) return false;
        node->process_idx = process_idx;
        node->next = NULL;
        if (scheduler.low_queue.tail)
            scheduler.low_queue.tail->next = node;
        else
            scheduler.low_queue.head = node;
        scheduler.low_queue.tail = node;
        scheduler.low_queue.count++;
        return true;
    }

    // (2) 노드 삭제
    static int low_dequeue() {
        if (!scheduler.low_queue.head) return -1;
        ListNode *node = scheduler.low_queue.head;
        int process_idx = node->process_idx;
        scheduler.low_queue.head = node->next;
        if (!scheduler.low_queue.head)
            scheduler.low_queue.tail = NULL;
        scheduler.low_queue.count--;
        free(node);
        return process_idx;
    }

    // (3) 특정 프로세스 삭제 ( 에이징 상황 )
    static bool low_remove(int process_idx) {
        ListNode *prev = NULL;
        ListNode *cur = scheduler.low_queue.head;
        while (cur) {
            if (cur->process_idx == process_idx) {
                if (prev) prev->next = cur->next;
                else scheduler.low_queue.head = cur->next;
                if (scheduler.low_queue.tail == cur) scheduler.low_queue.tail = prev;
                scheduler.low_queue.count--;
                free(cur);
                return true;
            }
            prev = cur;
            cur = cur->next;
        }
        return false;
    }

    // (4) Linked List 메모리 해제
    static void low_free() {
        while (scheduler.low_queue.head)
            (void)low_dequeue();
    }

// 10. I/O Circular Queue 함수 정의
    // (1) I/O 큐 초기화 
    static void wait_queue_init(void) {
        // memset() : 메모리의 특정 영역을 지정한 값으로 초기화하는 함수
        memset(&scheduler.wait_queue, 0, sizeof(scheduler.wait_queue));
    }

    // (2) 프로세스 삽입
    static bool wait_enqueue(int process_idx) {
        if (process_idx < 0 || process_idx >= NUM_CHILDREN) // 프로세스 인덱스 확인
            return false;
        if (scheduler.wait_queue.present[process_idx] || scheduler.wait_queue.count >= NUM_CHILDREN)  // 중복 확인
            return false;
        scheduler.wait_queue.data[scheduler.wait_queue.rear] = process_idx;
        scheduler.wait_queue.rear = (scheduler.wait_queue.rear + 1) % NUM_CHILDREN;
        scheduler.wait_queue.count++;
        scheduler.wait_queue.present[process_idx] = true;
        return true;
    }

    // (3) 프로세스 삭제
    static int wait_dequeue(void) {
        if (scheduler.wait_queue.count == 0) return -1;
        int process_idx = scheduler.wait_queue.data[scheduler.wait_queue.front];
        scheduler.wait_queue.front = (scheduler.wait_queue.front + 1) % NUM_CHILDREN;
        scheduler.wait_queue.count--;
        scheduler.wait_queue.present[process_idx] = false;
        return process_idx;
    }

// 11. READY 큐 관리 함수 정의
    // (1) Level 0 삽입
    static bool ready_enqueue_high(int process_idx) {
        if (process_idx < 0 || process_idx >= NUM_CHILDREN)
            return false;
        if (scheduler.pcb[process_idx].queue_location != QUEUE_NONE)
            return false;
        if (!avl_insert(process_idx))
            return false;
        // PCB 배열 프로세스 초기 값 설정
        scheduler.pcb[process_idx].queue_location = QUEUE_HIGH;
        scheduler.pcb[process_idx].priority_level = 0;
        scheduler.pcb[process_idx].state = READY;
        scheduler.pcb[process_idx].ready_enter_tick = scheduler.tick_count;
        return true;
    }

    // (2) Level 1 삽입
    static bool ready_enqueue_low(int process_idx) {
        if (process_idx < 0 || process_idx >= NUM_CHILDREN)
            return false;
        if (scheduler.pcb[process_idx].queue_location != QUEUE_NONE)
            return false;
        if (!low_enqueue(process_idx))
            return false;
        // PCB 배열 프로세스 초기 값 설정
        scheduler.pcb[process_idx].queue_location = QUEUE_LOW;
        scheduler.pcb[process_idx].priority_level = 1;
        scheduler.pcb[process_idx].state = READY;
        scheduler.pcb[process_idx].ready_enter_tick = scheduler.tick_count;
        return true;
    }

    // (3) 프로세스 선택
    static int ready_dequeue(void) {
        int process_idx;
        if (scheduler.high_root) { // Level 0 존재 시 먼저 선택
            process_idx = avl_dequeue_min();
            if (process_idx >= 0)
                scheduler.pcb[process_idx].queue_location = QUEUE_NONE;
            return process_idx;
        }
        if (scheduler.low_queue.head) { // Level 1 존재 시 선택
            process_idx = low_dequeue();
            if (process_idx >= 0)
                scheduler.pcb[process_idx].queue_location = QUEUE_NONE;
            return process_idx;
        }
        return -1;
    }

// 12. 타이머와 Tick 관리 함수 정의
    // (1) Time Handler : SIGALRM 시그널 발생 시 실행
    static void timer_handler(int signo) {
        // 컴파일러의 unused parameter 경고 방지를 위해
        (void)signo;
        // 타이머 발생마다 대기 중인 Tick의 개수 증가시킴
        if (pending_ticks < MAX_TICKS) pending_ticks++;
    }

    // (2) 타이머 설정
    static bool setup_timer(void) {
        // 시그널 핸들러 등록
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = timer_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        if (sigaction(SIGALRM, &sa, NULL) == -1) {
            perror("시그널 핸들러 설정 실패");
            return false;
        }

        // 타이머 설정
        struct itimerval timer;
        timer.it_value.tv_sec = 0;        
        timer.it_value.tv_usec = 1000;    // 타이머 최초 발생 시간 1ms 후
        timer.it_interval.tv_sec = 0;     
        timer.it_interval.tv_usec = 1000; // 타이머 발생 간격 1ms
        if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
            perror("타이머 설정 실패");
            return false;
        }

        return true;
    }

    // (3) 타이머 종료
    static void stop_timer(void) {
        struct itimerval timer;
        memset(&timer, 0, sizeof(timer));
        if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
            perror("setitimer 오류(멈춤)");
        }
    }

    // (4) Tick이 발생할 때까지 대기
    static bool wait_for_tick(void) {
        sigset_t block_set, old_set;
        sigemptyset(&block_set);
        sigaddset(&block_set, SIGALRM);

        // SIGALRM 차단
        if (sigprocmask(SIG_BLOCK, &block_set, &old_set) == -1) {
            perror("시그널 마스크 블럭");
            return false;
        }

        // 처리할 Tick이 없다면 대기
        while (pending_ticks == 0) {
            if (sigsuspend(&old_set) == -1 && errno != EINTR) {
                perror("sigsuspend");
                (void)sigprocmask(SIG_SETMASK, &old_set, NULL);
                return false;
            }
        }
        // Tick 하나 소비
        pending_ticks--;
        // 기존 시그널 마스크 복원
        if (sigprocmask(SIG_SETMASK, &old_set, NULL) == -1) {
            perror("시그널 마스크 복구");
            return false;
        }

        return true;
    }



// 13. IPC와 자식 프로세스 통신 함수 정의
    // (1) 부모 -> 자식 메시지 전달
    static bool send_message(long type, int child_idx, int tick) {
        Message msg;
        msg.mtype = type;
        msg.child_idx = child_idx;
        msg.tick = tick;  // 메시지를 보낸 시점

        while (msgsnd(scheduler.msqid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            if (errno == EINTR) continue;
            perror("메세지 전송 오류, msgsnd()");
            return false;
        }

        return true;
    }

    // (2) 자식 -> 부모 메시지 전달
    static void child_process(int child_idx) {
        long my_type = (long)getpid();
        for (;;) {
            Message msg;
            ssize_t received = msgrcv(scheduler.msqid, &msg, sizeof(msg) - sizeof(long), my_type, 0);
            if (received == -1) {
                if (errno == EINTR) continue;
                _exit(EXIT_FAILURE);
            }

            if (msg.child_idx == MSG_STOP)
                _exit(EXIT_SUCCESS);

            AckMessage ack;
            ack.mtype = 1;
            ack.child_idx = child_idx;
            ack.tick = msg.tick;
            while (msgsnd(scheduler.msqid, &ack, sizeof(ack) - sizeof(long), 0) == -1) {
                if (errno == EINTR) continue;
                _exit(EXIT_FAILURE);
            }
        }
    }

    // (3) 부모의 응답 대기
    static bool wait_for_ack(int expected_idx, int expected_tick) {
        AckMessage ack;
        for (;;) {
            ssize_t received = msgrcv(scheduler.msqid, &ack, sizeof(ack) - sizeof(long), 1, 0);
            if (received == -1) {
                if (errno == EINTR) continue;
                perror("msgrcv() 응답 대기 오류");
                return false;
            }
            if (ack.child_idx == expected_idx && ack.tick == expected_tick) return true;
            // 예상치 못한 ACK 또는 오래된 ACK는 무시
        }
    }

// 14. 스케줄러 로직 함수 정의
    // (1) I/O 대기 프로세스 처리 : i/o 장치가 작업을 병렬 처리하는 것을 가정
    static bool process_io_queue() {
        int count_at_start = scheduler.wait_queue.count;

        for (int i = 0; i < count_at_start; i++) {
            int idx = wait_dequeue();
            if (idx < 0) break;

            scheduler.pcb[idx].queue_location = QUEUE_NONE;
            scheduler.pcb[idx].io_burst--;

            if (scheduler.pcb[idx].io_burst <= 0) {
                scheduler.pcb[idx].completed_cycles++;
                scheduler.pcb[idx].total_turnaround_time += scheduler.tick_count - scheduler.pcb[idx].cycle_start_tick;
                scheduler.pcb[idx].cycle_start_tick = scheduler.tick_count;

                scheduler.pcb[idx].cpu_burst = random_between(MIN_CPU_BURST, MAX_CPU_BURST);
                scheduler.pcb[idx].io_burst = random_between(MIN_IO_BURST, MAX_IO_BURST);
                scheduler.pcb[idx].remaining_quantum = TIME_QUANTUM;

                if (!ready_enqueue_high(idx)) {
                    fprintf(stderr, "I/O 작업 이후 P%d Level 0 큐 삽입 실패\n", idx);
                    return false;
                }
            } else {
                if (!wait_enqueue(idx)) {
                    fprintf(stderr, "P%d I/O 큐 재진입 실패\n", idx);
                    return false;
                }
                scheduler.pcb[idx].queue_location = QUEUE_WAIT;
            }
        }
        return true;
    }

    // (2) 에이징 적용
    static bool apply_aging(void) {
        ListNode *cur = scheduler.low_queue.head;

        while (cur) {
            int idx = cur->process_idx;
            ListNode *next = cur->next;

            if (scheduler.tick_count - scheduler.pcb[idx].ready_enter_tick >= AGING_THRESHOLD) {
                if (!low_remove(idx)) {
                    fprintf(stderr, "에이징 삭제 실패 : P%d.\n", idx);
                    return false;
                }
                scheduler.pcb[idx].queue_location = QUEUE_NONE;
                if (!ready_enqueue_high(idx)) {
                    fprintf(stderr, "에이징 승급 실패 : P%d.\n", idx);
                    return false;
                }
            }
            cur = next;
        }
        return true;
    }

    // (3) 낮은 우선순위 프로세스 선점
    static bool preempt_low_process(void) {
        // 높은 우선순위 프로세스가 존재할 경우 현재 실행 중인 프로세스를 선점
        if (scheduler.running_idx >= 0 && scheduler.pcb[scheduler.running_idx].priority_level == 1 && scheduler.high_root != NULL) {
            int idx = scheduler.running_idx;
            scheduler.running_idx = -1;
            scheduler.pcb[idx].state = READY;

            // 높은 우선순위 프로세스에 의해 선점되면 남은 퀸텀은 보존
            if (!ready_enqueue_low(idx)) {
                fprintf(stderr, "P%d Level 1 큐 재진입 실패.\n", idx);
                return false;
            }
        }
        return true;
    }

    // (4) CPU를 사용할 프로세스 선택
    static void dispatch_process(void) {
        // 실행 중인 프로세스가 있을 경우 취소
        if (scheduler.running_idx != -1) return;

        // 프로세스 선택
        int idx = ready_dequeue();
        if (idx == -1) return;
        if (scheduler.pcb[idx].first_dispatch) { // 처음인 경우
            scheduler.pcb[idx].total_response_time = scheduler.tick_count - 1;
            scheduler.pcb[idx].first_dispatch = false;
        }
        if (scheduler.last_cpu_idx != -1 && scheduler.last_cpu_idx != idx) {  // 프로세스가 바뀐 경우
            scheduler.total_context_switches++;
            scheduler.pcb[idx].context_switches++;
        }

        // 스케줄러 프로세스 변경 반영
        scheduler.running_idx = idx;
        scheduler.pcb[idx].state = RUNNING;
        scheduler.last_cpu_idx = idx;
        scheduler.total_dispatches++;
    }

    // (5) Level 0 대기 시간 계산
    static void update_high_waiting(AVLNode *node) {
        if (!node) return;
        // Tree 루트에서 좌우 노드로 이동하며 대기 시간 증가 계산
        update_high_waiting(node->left);
        scheduler.pcb[node->process_idx].total_waiting_time++;
        update_high_waiting(node->right);
    }

    // (6) Level 1 대기 시간 계산
    static void update_low_waiting(void) {
        ListNode *cur = scheduler.low_queue.head;
        while (cur) { // Linked List 끝까지 이동하며 대기 시간 증가 계산
            scheduler.pcb[cur->process_idx].total_waiting_time++;
            cur = cur->next;
        }
    }

    // (7) CPU Tick 실행
    static bool execute_cpu_tick(void) {
        // 실행 중인 프로세스가 없을 경우
        if (scheduler.running_idx < 0) return;

        // 자식에게 현재 tick 전달 후 응답 대기
        int idx = scheduler.running_idx;
        send_message((long)scheduler.pcb[idx].pid, idx, scheduler.tick_count);
        wait_for_ack(idx, scheduler.tick_count);

        // 자식 프로세스의 응답을 받은 뒤 논리적인 CPU Tick을 반영
        scheduler.pcb[idx].cpu_burst--;
        scheduler.pcb[idx].remaining_quantum--;
        scheduler.total_cpu_ticks++;

        // CPU Burst 완료 시 대기 상태로 전환 후 I/O Burst 처리
        if (scheduler.pcb[idx].cpu_burst <= 0) {
            scheduler.pcb[idx].state = WAITING;
            scheduler.pcb[idx].remaining_quantum = TIME_QUANTUM;
            scheduler.running_idx = -1;
            if (!wait_enqueue(idx)) {
                fprintf(stderr, "대기 큐에 P%d 삽입 실패\n", idx);
                return false;
            }
            scheduler.pcb[idx].queue_location = QUEUE_WAIT;
        // CPU Burst 완료 전에 퀸텀이 만료되면 실행 대기 상태로 
        } else if (scheduler.pcb[idx].remaining_quantum <= 0) {
            scheduler.pcb[idx].state = READY;
            scheduler.pcb[idx].remaining_quantum = TIME_QUANTUM;
            scheduler.running_idx = -1;
            scheduler.pcb[idx].queue_location = QUEUE_NONE;
            if (!ready_enqueue_low(idx)) {
                fprintf(stderr, "P%d Level 1 큐 재진입 실패\n", idx);
                return false;
            }
        }
    }

    // (8) 전체 스케줄러 Tick
    static bool scheduler_tick(void) {
        scheduler.tick_count++;

        if (!process_io_queue()) // I/O 작업 수행
            return false;
        if (!apply_aging())     // 에이징 수행
            return false;
        if (!preempt_low_process()) // 선점 수행
            return false;

        // 프로세스 선택 후 대기 시간 변경
        dispatch_process();
        update_high_waiting(scheduler.high_root);
        update_low_waiting();

        // cpu 실행
        return execute_cpu_tick();
    }

// 15. 통계 및 메모리 정리 함수 정의
    // (1) 통계 계산
    static void print_statistics() {
        int total_completed_cycles = 0;
        int total_waiting = 0;
        int total_response = 0;
        int first_dispatch_count = 0;
        int total_turnaround = 0;
        
        // 스케줄링 지표 합산
        for (int i = 0; i < NUM_CHILDREN; i++) {
            total_completed_cycles += scheduler.pcb[i].completed_cycles;
            total_waiting += scheduler.pcb[i].total_waiting_time;
            total_response += scheduler.pcb[i].total_response_time;
            total_turnaround += scheduler.pcb[i].total_turnaround_time;
            if (!scheduler.pcb[i].first_dispatch) first_dispatch_count++;
        }

        // 평균 내기
        double cpu_utilization = (scheduler.tick_count > 0) ? (double)scheduler.total_cpu_ticks / scheduler.tick_count * 100.0 : 0.0;
        double throughput = (scheduler.tick_count > 0) ? (double)total_completed_cycles / scheduler.tick_count * 1000.0 : 0.0;
        double avg_waiting = (double)total_waiting / NUM_CHILDREN;
        double avg_response = (first_dispatch_count > 0) ? (double)total_response / first_dispatch_count : 0.0;
        double avg_turnaround = (total_completed_cycles > 0) ? (double)total_turnaround / total_completed_cycles : 0.0;
        
        printf("\n========== CPU 스케줄링 지표 ==========\n");
        printf("전체 Ticks              : %d\n", scheduler.tick_count);
        printf("전체 CPU Ticks          : %d\n", scheduler.total_cpu_ticks);
        printf("CPU 점유율              : %.2f%%\n", cpu_utilization);
        printf("처리량                  : %.2f cycles/sec (1 Tick = 1 ms)\n", throughput);
        printf("완료된 CPU-I/O 사이클 수 : %d\n", total_completed_cycles);
        printf("평균 대기 시간           : %.2f ticks/process\n", avg_waiting);
        printf("평균 응답 시간           : %.2f ticks/process\n", avg_response);
        printf("평균 소요 시간           : %.2f ticks/cycle\n", avg_turnaround);
        printf("총 디스패치 수           : %d\n", scheduler.total_dispatches);
        printf("총 문맥 교환 수          : %d\n", scheduler.total_context_switches);
        printf("============================================\n");
    }

    // (2) 자식 프로세스 종료
    static void stop_children() {
        // 종료 메시지 전달
        for (int i = 0; i < NUM_CHILDREN; i++) {
            if (scheduler.pcb[i].pid > 0)
                send_message((long)scheduler.pcb[i].pid, MSG_STOP, scheduler.tick_count);
        }

        // 자식 종료 대기
        for (int i = 0; i < NUM_CHILDREN; i++) {
            if (scheduler.pcb[i].pid > 0) {
                // 에러 발생 시
                while (waitpid(scheduler.pcb[i].pid, NULL, 0) == -1) {
                    if (errno == EINTR) continue; // 시스템 콜 에러면 다시
                    if (errno == ECHILD) break;   // 자식 프로세스가 존재하지 않으면 종료
                    perror("waitpid"); // 그 외 오류는 종료
                    break;
                }
            }
        }
    }

    // (3) 메모리 정리
    static void cleanup() {
        avl_free(scheduler.high_root);
        scheduler.high_root = NULL;
        low_free();
        if (scheduler.msqid != -1) {
            if (msgctl(scheduler.msqid, IPC_RMID, NULL) == -1)
                perror("msgctl IPC_RMID 오류");
            scheduler.msqid = -1;
        }
    }

    // (4) 타이머 정리
    static void cleanup_error(int created_children) {
        // 타이머 종료
        stop_timer();

        // 자식 프로세스 종료
        for (int i = 0; i < created_children; i++) {
            if (scheduler.pcb[i].pid > 0)
                kill(scheduler.pcb[i].pid, SIGTERM);
        }

        // 자식 프로세스 종료 대기 및 시스템 콜 오류는 기다리기
        for (int i = 0; i < created_children; i++) {
            if (scheduler.pcb[i].pid > 0)
                while (waitpid(scheduler.pcb[i].pid, NULL, 0) == -1 && errno == EINTR) {}
        }
        // 자식 종료되면 메모리 정리
        cleanup();
    }

// 메인 함수
int main() {
    // 스케줄러 값 초기화
    scheduler.msqid = -1;
    scheduler.running_idx = -1;
    scheduler.last_cpu_idx = -1;

    // 난수 생성기 초기화
    srand((unsigned int)time(NULL));

    // I/O 대기 큐 초기화
    wait_queue_init();

    // Message Queue 공간 생성
    scheduler.msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (scheduler.msqid == -1) {
        perror("msgget 오류");
        return 1;
    }

    // PCB 초기화 및 자식 프로세스 Level 0 삽입
    for (int i = 0; i < NUM_CHILDREN; i++) {
        scheduler.pcb[i].pid = -1;
        scheduler.pcb[i].child_idx = i;
        scheduler.pcb[i].state = READY;
        scheduler.pcb[i].queue_location = QUEUE_NONE;
        scheduler.pcb[i].priority_level = 0;
        scheduler.pcb[i].cpu_burst = random_between(MIN_CPU_BURST, MAX_CPU_BURST);
        scheduler.pcb[i].io_burst = random_between(MIN_IO_BURST, MAX_IO_BURST);
        scheduler.pcb[i].remaining_quantum = TIME_QUANTUM;
        scheduler.pcb[i].total_waiting_time = 0;
        scheduler.pcb[i].total_response_time = 0;
        scheduler.pcb[i].total_turnaround_time = 0;
        scheduler.pcb[i].completed_cycles = 0;
        scheduler.pcb[i].context_switches = 0;
        scheduler.pcb[i].first_dispatch = true;
        scheduler.pcb[i].cycle_start_tick = 0;
        scheduler.pcb[i].ready_enter_tick = 0;
        if (!ready_enqueue_high(i)) {
            fprintf(stderr, "Level 0 큐 삽입 오류\n");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    // 자식 프로세스 생성
    int created_children = 0;
    for (int i = 0; i < NUM_CHILDREN; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork 오류");
            cleanup_error(created_children);
            return EXIT_FAILURE;
        }

        // 자식 프로세스일 경우 작업 실행
        if (pid == 0) child_process(i);

        scheduler.pcb[i].pid = pid;
        created_children++;
    }

    if (!setup_timer()) {
        cleanup_error(created_children);
        return EXIT_FAILURE;
    }

    printf("다단계 피드백 큐 스케줄러 시작 (Level 0: AVL Tree, Level 1: Linked List).\n");
    
    while (scheduler.tick_count < MAX_TICKS) {
        if (!wait_for_tick()) { // 틱 대기 실패 시 종료
            stop_timer();
            cleanup_error(created_children);
            return EXIT_FAILURE;
        }
        if (!scheduler_tick()) {  // 스케줄러 틱 오류 발생 시 종료
            fprintf(stderr, "오류로 인해 스케줄러 동작 멈춤\n");
            stop_timer();
            cleanup_error(created_children);
            return EXIT_FAILURE;
        }
    }

    // 타이머 종료
    stop_timer();
    // 자식 프로세스 종료
    stop_children();
    // 스케줄러 통계 지표 출력
    print_statistics();
    // 메모리 정리
    cleanup();

    return 0;
}

