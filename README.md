# Assignment 05: CPU scheduling simulator

Build from this directory:

```sh
cc -std=c11 -Wall -Wextra -pedantic proj2.c -o proj2
```

Run the assignment sample:

```sh
./proj2 test1.txt FCFS
./proj2 test1.txt SJF
./proj2 test1.txt RR 5
```

The first line of an input file must be its task count (1 through 20).
Every following task has three integers: PID, arrival time, and burst time.
Use plain text, with no shell commands, column headings, or `EOF` markers:

```text
4
0 0 12
1 2 4
2 3 1
3 4 2
```

Arrival times must be nonnegative, bursts must be positive, and PIDs must
be unique. Input rows need not be sorted. All times are milliseconds.
The simulator checks that the latest arrival plus total burst time fits
in a signed integer, so its clock cannot overflow.

FCFS and SJF run each selected process to completion. SJF chooses only
among processes that have arrived. Ties use arrival time, then input order.
RR requires a positive integer quantum; processes arriving exactly when
a quantum expires enter the queue before the interrupted process returns.
Progress is printed at each state change, with idle intervals explicitly shown.

| Input | FCFS | SJF | RR | Quantum |
|---|---:|---:|---:|---:|
| test1.txt | 9.00 | 7.75 | 5.50 | 5 |
| test2.txt | 6.80 | 4.60 | 7.40 | 3 |
| test3.txt | 3.00 | 1.25 | 2.75 | 2 |

Run all verification checks with `make test` (requires Python 3), or:

```sh
python3 tests/check_scheduler.py ./proj2
```

The report is `output/pdf/Lab05_Report.pdf`. Screenshots are in
`output/screenshots/`, and complete output from all nine example runs is in
`output/runs/`. Submit the report, `proj2.c`, and the three test input files.
The executable can be rebuilt on the grading machine.
