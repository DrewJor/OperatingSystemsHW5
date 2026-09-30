#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* CPU scheduling simulator. Times are integer milliseconds. Input begins
   with a task count, followed by PID, arrival time, and burst time triples. */

#define MAX_TASKS 20

typedef struct {
    int pid;
    int arrival;
    int burst;
    int remaining;
    int start;
    int end;
    int waiting;
    int finished;
    int input_order;
} Task;

typedef struct {
    int data[MAX_TASKS];
    int front;
    int rear;
    int count;
} Queue;

/* Circular FIFO queue: each ready process appears at most once. */
static void queue_init(Queue *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}

static int queue_empty(const Queue *q) {
    return q->count == 0;
}

static void enqueue(Queue *q, int value) {
    if (q->count >= MAX_TASKS) {
        fprintf(stderr, "Ready queue overflow.\n");
        exit(EXIT_FAILURE);
    }
    q->data[q->rear] = value;
    q->rear = (q->rear + 1) % MAX_TASKS;
    q->count++;
}

static int dequeue(Queue *q) {
    if (queue_empty(q)) {
        fprintf(stderr, "Ready queue underflow.\n");
        exit(EXIT_FAILURE);
    }
    int value = q->data[q->front];
    q->front = (q->front + 1) % MAX_TASKS;
    q->count--;
    return value;
}

static int equals_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Read a complete integer token, rejecting overflow and suffixes such as 3ms.
   A fixed token buffer also keeps malformed input from overrunning memory. */
static int read_integer(FILE *fp, int *value) {
    char token[64];
    size_t length = 0;
    int ch;
    do {
        ch = fgetc(fp);
    } while (ch != EOF && isspace((unsigned char)ch));
    if (ch == EOF) return 0;
    do {
        if (length + 1 >= sizeof(token)) return 0;
        token[length++] = (char)ch;
        ch = fgetc(fp);
    } while (ch != EOF && !isspace((unsigned char)ch));
    token[length] = '\0';
    char *end;
    errno = 0;
    long number = strtol(token, &end, 10);
    if (errno == ERANGE || end == token || *end != '\0' ||
        number < INT_MIN || number > INT_MAX) return 0;
    *value = (int)number;
    return 1;
}

static int read_tasks(const char *filename, Task tasks[]) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("Unable to open input file");
        exit(EXIT_FAILURE);
    }

    int n;
    if (!read_integer(fp, &n) || n < 1 || n > MAX_TASKS) {
        fprintf(stderr, "Invalid number of tasks. Expected 1 to %d as the first value in %s.\n"
                "Use plain numeric data; do not include shell commands or EOF markers.\n",
                MAX_TASKS, filename);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    long long total_burst = 0;
    int latest_arrival = 0;
    for (int i = 0; i < n; i++) {
        if (!read_integer(fp, &tasks[i].pid) ||
            !read_integer(fp, &tasks[i].arrival) ||
            !read_integer(fp, &tasks[i].burst)) {
            fprintf(stderr, "Invalid task data on row %d.\n", i + 2);
            fclose(fp);
            exit(EXIT_FAILURE);
        }
        if (tasks[i].arrival < 0 || tasks[i].burst <= 0) {
            fprintf(stderr, "Arrival times must be >= 0 and burst times must be > 0.\n");
            fclose(fp);
            exit(EXIT_FAILURE);
        }
        for (int j = 0; j < i; j++) {
            if (tasks[j].pid == tasks[i].pid) {
                fprintf(stderr, "Duplicate PID %d. PIDs must be unique.\n", tasks[i].pid);
                fclose(fp);
                exit(EXIT_FAILURE);
            }
        }
        tasks[i].remaining = tasks[i].burst;
        tasks[i].start = -1;
        tasks[i].end = -1;
        tasks[i].waiting = 0;
        tasks[i].finished = 0;
        tasks[i].input_order = i;
        total_burst += tasks[i].burst;
        if (tasks[i].arrival > latest_arrival) latest_arrival = tasks[i].arrival;
    }

    /* This conservative bound prevents overflow of the integer simulation clock. */
    if (total_burst + latest_arrival > INT_MAX) {
        fprintf(stderr, "Arrival times plus total burst time must fit in a signed int.\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (!isspace((unsigned char)ch)) {
            fprintf(stderr, "Unexpected data after %d tasks. Remove extra rows or EOF markers.\n", n);
            fclose(fp);
            exit(EXIT_FAILURE);
        }
    }
    fclose(fp);
    return n;
}

static void reset_tasks(Task tasks[], int n) {
    for (int i = 0; i < n; i++) {
        tasks[i].remaining = tasks[i].burst;
        tasks[i].start = -1;
        tasks[i].end = -1;
        tasks[i].waiting = 0;
        tasks[i].finished = 0;
    }
}

/* Stable arrival order: simultaneous arrivals keep their input-file order. */
static void build_arrival_order(const Task tasks[], int n, int order[]) {
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }

    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 &&
               (tasks[order[j]].arrival > tasks[key].arrival ||
                (tasks[order[j]].arrival == tasks[key].arrival &&
                 tasks[order[j]].input_order > tasks[key].input_order))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

static void print_summary_nonpreemptive(const Task tasks[], int order[], int n, const char *name) {
    double total_wait = 0.0;

    printf("\n%s Statistical Information:\n", name);
    printf("%-8s %-14s %-12s %-10s %-14s %-13s\n",
           "PID", "Arrival Time", "Start Time", "End Time", "Running Time", "Waiting Time");

    for (int k = 0; k < n; k++) {
        int i = order[k];
        printf("%-8d %-14d %-12d %-10d %-14d %-13d\n",
               tasks[i].pid,
               tasks[i].arrival,
               tasks[i].start,
               tasks[i].end,
               tasks[i].burst,
               tasks[i].waiting);
        total_wait += tasks[i].waiting;
    }

    printf("Average Waiting Time: %.2f\n", total_wait / n);
}

/* Non-preemptive FCFS advances between events, logging arrivals during a burst. */
static void simulate_fcfs(Task tasks[], int n) {
    reset_tasks(tasks, n);
    int order[MAX_TASKS];
    int arrived_logged[MAX_TASKS] = {0};
    build_arrival_order(tasks, n, order);

    int time = 0;
    printf("FCFS Scheduling Progress:\n");

    for (int k = 0; k < n; k++) {
        int idx = order[k];

        if (time < tasks[idx].arrival) {
            printf("[time %d -> %d] CPU IDLE\n", time, tasks[idx].arrival);
            time = tasks[idx].arrival;
        }

        for (int a = 0; a < n; a++) {
            int j = order[a];
            if (!arrived_logged[j] && tasks[j].arrival <= time) {
                printf("[time %d] PID %d NEW -> READY\n", tasks[j].arrival, tasks[j].pid);
                arrived_logged[j] = 1;
            }
        }

        tasks[idx].start = time;
        tasks[idx].waiting = tasks[idx].start - tasks[idx].arrival;
        printf("[time %d] PID %d READY -> RUNNING\n", time, tasks[idx].pid);

        int finish_time = time + tasks[idx].burst;
        for (int a = 0; a < n; a++) {
            int j = order[a];
            if (!arrived_logged[j] && tasks[j].arrival <= finish_time) {
                printf("[time %d] PID %d NEW -> READY\n", tasks[j].arrival, tasks[j].pid);
                arrived_logged[j] = 1;
            }
        }

        time = finish_time;
        tasks[idx].end = time;
        tasks[idx].finished = 1;
        printf("[time %d] PID %d RUNNING -> COMPLETED\n", time, tasks[idx].pid);
    }

    print_summary_nonpreemptive(tasks, order, n, "FCFS");
}

/* Only ready tasks compete for SJF. Ties use arrival time, then file order. */
static int choose_sjf_task(const Task tasks[], int n, int time) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (!tasks[i].finished && tasks[i].arrival <= time) {
            if (best == -1 ||
                tasks[i].burst < tasks[best].burst ||
                (tasks[i].burst == tasks[best].burst && tasks[i].arrival < tasks[best].arrival) ||
                (tasks[i].burst == tasks[best].burst && tasks[i].arrival == tasks[best].arrival &&
                 tasks[i].input_order < tasks[best].input_order)) {
                best = i;
            }
        }
    }
    return best;
}

/* A selected SJF task runs to completion, even if a shorter task arrives. */
static void simulate_sjf(Task tasks[], int n) {
    reset_tasks(tasks, n);
    int arrival_order[MAX_TASKS];
    int schedule_order[MAX_TASKS];
    int arrived_logged[MAX_TASKS] = {0};
    int completed = 0;
    int time = 0;

    build_arrival_order(tasks, n, arrival_order);
    printf("SJF Scheduling Progress:\n");

    while (completed < n) {
        for (int a = 0; a < n; a++) {
            int j = arrival_order[a];
            if (!arrived_logged[j] && tasks[j].arrival <= time) {
                printf("[time %d] PID %d NEW -> READY\n", tasks[j].arrival, tasks[j].pid);
                arrived_logged[j] = 1;
            }
        }

        int idx = choose_sjf_task(tasks, n, time);
        if (idx == -1) {
            int next_time = -1;
            for (int a = 0; a < n; a++) {
                int j = arrival_order[a];
                if (!tasks[j].finished && tasks[j].arrival > time) {
                    next_time = tasks[j].arrival;
                    break;
                }
            }
            if (next_time < 0) {
                break;
            }
            printf("[time %d -> %d] CPU IDLE\n", time, next_time);
            time = next_time;
            continue;
        }

        tasks[idx].start = time;
        tasks[idx].waiting = tasks[idx].start - tasks[idx].arrival;
        schedule_order[completed] = idx;
        printf("[time %d] PID %d READY -> RUNNING\n", time, tasks[idx].pid);

        int finish_time = time + tasks[idx].burst;
        for (int a = 0; a < n; a++) {
            int j = arrival_order[a];
            if (!arrived_logged[j] && tasks[j].arrival <= finish_time) {
                printf("[time %d] PID %d NEW -> READY\n", tasks[j].arrival, tasks[j].pid);
                arrived_logged[j] = 1;
            }
        }

        time = finish_time;
        tasks[idx].end = time;
        tasks[idx].finished = 1;
        completed++;
        printf("[time %d] PID %d RUNNING -> COMPLETED\n", time, tasks[idx].pid);
    }

    print_summary_nonpreemptive(tasks, schedule_order, n, "SJF");
}

typedef struct {
    int pid;
    int start;
    int end;
    int ran;
} Slice;

static void enqueue_arrivals(Task tasks[], int n, int arrival_order[], int *next_arrival,
                             int time, Queue *ready, int in_queue[]) {
    while (*next_arrival < n) {
        int idx = arrival_order[*next_arrival];
        if (tasks[idx].arrival > time) {
            break;
        }
        if (!tasks[idx].finished && tasks[idx].remaining > 0 && !in_queue[idx]) {
            printf("[time %d] PID %d NEW -> READY\n", tasks[idx].arrival, tasks[idx].pid);
            enqueue(ready, idx);
            in_queue[idx] = 1;
        }
        (*next_arrival)++;
    }
}

/* RR logs each dispatch and stores slices for the final execution table. */
static void simulate_rr(Task tasks[], int n, int quantum) {
    reset_tasks(tasks, n);

    int arrival_order[MAX_TASKS];
    int in_queue[MAX_TASKS] = {0};
    int next_arrival = 0;
    int completed = 0;
    int time = 0;
    Queue ready;
    Slice *slices = NULL;
    size_t slice_count = 0;
    size_t slice_capacity = 0;

    build_arrival_order(tasks, n, arrival_order);
    queue_init(&ready);

    printf("RR Scheduling Progress (time quantum = %d):\n", quantum);

    while (completed < n) {
        enqueue_arrivals(tasks, n, arrival_order, &next_arrival, time, &ready, in_queue);

        if (queue_empty(&ready)) {
            if (next_arrival >= n) {
                break;
            }
            int next_time = tasks[arrival_order[next_arrival]].arrival;
            printf("[time %d -> %d] CPU IDLE\n", time, next_time);
            time = next_time;
            enqueue_arrivals(tasks, n, arrival_order, &next_arrival, time, &ready, in_queue);
        }

        int idx = dequeue(&ready);
        in_queue[idx] = 0;
        if (tasks[idx].start < 0) {
            tasks[idx].start = time;
        }

        printf("[time %d] PID %d READY -> RUNNING\n", time, tasks[idx].pid);

        int run = tasks[idx].remaining < quantum ? tasks[idx].remaining : quantum;
        int start = time;
        int end = time + run;

        /* Grow the history as needed instead of imposing a fixed slice limit. */
        if (slice_count == slice_capacity) {
            size_t capacity = slice_capacity ? slice_capacity * 2 : 64;
            if (capacity < slice_capacity || capacity > (size_t)-1 / sizeof(*slices)) {
                fprintf(stderr, "Round Robin history is too large.\n");
                free(slices);
                exit(EXIT_FAILURE);
            }
            Slice *grown = realloc(slices, capacity * sizeof(*slices));
            if (grown == NULL) {
                fprintf(stderr, "Unable to allocate Round Robin history.\n");
                free(slices);
                exit(EXIT_FAILURE);
            }
            slices = grown;
            slice_capacity = capacity;
        }
        slices[slice_count].pid = tasks[idx].pid;
        slices[slice_count].start = start;
        slices[slice_count].end = end;
        slices[slice_count].ran = run;
        slice_count++;

        tasks[idx].remaining -= run;
        time = end;

        /* Processes that arrived during this time slice enter the ready queue
           before the preempted process is placed back at the end of the queue. */
        enqueue_arrivals(tasks, n, arrival_order, &next_arrival, time, &ready, in_queue);

        if (tasks[idx].remaining == 0) {
            tasks[idx].end = time;
            tasks[idx].finished = 1;
            completed++;
            printf("[time %d] PID %d RUNNING -> COMPLETED\n", time, tasks[idx].pid);
        } else {
            printf("[time %d] PID %d RUNNING -> READY (quantum expired)\n", time, tasks[idx].pid);
            enqueue(&ready, idx);
            in_queue[idx] = 1;
        }
    }

    double total_wait = 0.0;
    printf("\nRR Time Slices:\n");
    printf("%-8s %-12s %-10s %-14s\n", "PID", "Start Time", "End Time", "Running Time");
    for (size_t i = 0; i < slice_count; i++) {
        printf("%-8d %-12d %-10d %-14d\n",
               slices[i].pid, slices[i].start, slices[i].end, slices[i].ran);
    }
    free(slices);

    printf("\nRR Statistical Information:\n");
    printf("%-8s %-14s %-14s %-10s %-13s\n",
           "PID", "Arrival Time", "Running Time", "End Time", "Waiting Time");

    for (int i = 0; i < n; i++) {
        /* Turnaround time includes CPU service and every interval spent ready. */
        tasks[i].waiting = tasks[i].end - tasks[i].arrival - tasks[i].burst;
        total_wait += tasks[i].waiting;
        printf("%-8d %-14d %-14d %-10d %-13d\n",
               tasks[i].pid,
               tasks[i].arrival,
               tasks[i].burst,
               tasks[i].end,
               tasks[i].waiting);
    }

    printf("Average Waiting Time: %.2f\n", total_wait / n);
}

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s input_file [FCFS|RR|SJF] [time_quantum]\n", program);
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s input.1 FCFS\n", program);
    fprintf(stderr, "  %s input.1 SJF\n", program);
    fprintf(stderr, "  %s input.1 RR 5\n", program);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    Task tasks[MAX_TASKS];
    int n = read_tasks(argv[1], tasks);

    if (equals_ignore_case(argv[2], "FCFS")) {
        if (argc != 3) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        simulate_fcfs(tasks, n);
    } else if (equals_ignore_case(argv[2], "SJF")) {
        if (argc != 3) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        simulate_sjf(tasks, n);
    } else if (equals_ignore_case(argv[2], "RR")) {
        if (argc != 4) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        char *endptr = NULL;
        errno = 0;
        long q = strtol(argv[3], &endptr, 10);
        if (errno == ERANGE || *argv[3] == '\0' || *endptr != '\0' || q <= 0 || q > INT_MAX) {
            fprintf(stderr, "Time quantum must be a positive integer.\n");
            return EXIT_FAILURE;
        }
        simulate_rr(tasks, n, (int)q);
    } else {
        fprintf(stderr, "Unknown scheduling algorithm: %s\n", argv[2]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
