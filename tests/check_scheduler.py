"""Compare the C simulator with an independent millisecond-by-millisecond model."""
from collections import deque
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile

BINARY = str(Path(sys.argv[1] if len(sys.argv) > 1 else './proj2').resolve())
ROOT = Path(__file__).resolve().parents[1]


def reference(tasks, algorithm, quantum):
    remaining = [t[2] for t in tasks]
    starts, ends = {}, {}
    ready = deque()
    current = None
    used = time = 0
    while len(ends) < len(tasks):
        # Boundary arrivals precede requeueing an expired RR process.
        ready.extend(i for i, t in enumerate(tasks) if t[1] == time)
        if current is not None:
            if remaining[current] == 0:
                ends[current] = time
                current = None
            elif algorithm == 'RR' and used == quantum:
                ready.append(current)
                current = None
        if current is None and ready:
            if algorithm == 'SJF':
                current = min(ready, key=lambda i: (tasks[i][2], tasks[i][1], i))
                ready.remove(current)
            else:
                current = ready.popleft()
            starts.setdefault(current, time)
            used = 0
        if current is not None:
            remaining[current] -= 1
            used += 1
        time += 1
    return {t[0]: (starts[i], ends[i], ends[i] - t[1] - t[2])
            for i, t in enumerate(tasks)}


def run(path, algorithm, quantum=2):
    args = [BINARY, str(path), algorithm]
    if algorithm == 'RR':
        args.append(str(quantum))
    return subprocess.run(args, text=True, capture_output=True, timeout=10)


def check(path, tasks, algorithm, quantum=2, average=None):
    result = run(path, algorithm, quantum)
    assert result.returncode == 0, result.stderr
    expected = reference(tasks, algorithm, quantum)
    summary = result.stdout.split('Statistical Information:\n')[1]
    rows = [list(map(int, line.split())) for line in summary.splitlines()
            if re.fullmatch(r'\s*-?\d+(?:\s+-?\d+){4,5}\s*', line)]
    assert len(rows) == len(tasks), result.stdout
    for row in rows:
        pid = row[0]
        start, end, wait = expected[pid]
        if algorithm == 'RR':
            assert (row[3], row[4]) == (end, wait), (row, expected)
        else:
            assert (row[2], row[3], row[5]) == (start, end, wait), (row, expected)
    actual_avg = float(re.search(r'Average Waiting Time: ([\d.]+)', summary)[1])
    expected_avg = sum(t[2] for t in expected.values()) / len(tasks)
    assert abs(actual_avg - expected_avg) <= 0.00501
    if average is not None:
        assert actual_avg == average
    timestamps = [int(t) for t in re.findall(r'\[time (\d+)', result.stdout)]
    assert timestamps == sorted(timestamps), 'Progress is not chronological'
    return result.stdout


def main():
    output = ROOT / 'output' / 'runs'
    output.mkdir(parents=True, exist_ok=True)
    for number, quantum, averages in [(1, 5, [9, 7.75, 5.5]),
                                      (2, 3, [6.8, 4.6, 7.4]),
                                      (3, 2, [3, 1.25, 2.75])]:
        path = ROOT / f'test{number}.txt'
        lines = path.read_text().splitlines()
        tasks = [tuple(map(int, line.split())) for line in lines[1:] if line.strip()]
        assert int(lines[0]) == len(tasks)
        for algorithm, average in zip(['FCFS', 'SJF', 'RR'], averages):
            text = check(path, tasks, algorithm, quantum, average)
            (output / f'test{number}_{algorithm}.txt').write_text(text)
    randomizer = random.Random(20260930)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'tasks.txt'
        # Include idle gaps, unsorted input, simultaneous arrivals, and full queues.
        cases = [[(1, 0, 1), (2, 10, 2)], [(1, 0, 4), (2, 2, 1)],
                 [(i, 0, 3) for i in range(20)]]
        for _ in range(200):
            tasks = [(i + 10, randomizer.randrange(25), randomizer.randrange(1, 13))
                     for i in range(randomizer.randrange(1, 21))]
            randomizer.shuffle(tasks)
            cases.append(tasks)
        for tasks in cases:
            path.write_text(str(len(tasks)) + '\n' +
                            ''.join('%d %d %d\n' % t for t in tasks))
            for algorithm in ['FCFS', 'SJF', 'RR']:
                check(path, tasks, algorithm, 2)
        invalid = ['', 'cat > test1.txt\n4\n', '0\n', '21\n',
                   '1\n0 -1 2\n', '1\n0 0 0\n', '1\n0 0\n',
                   '2\n1 0 1\n1 1 1\n', '1\n1 0 2\nEOF\n',
                   '1\n1 0 2ms\n', '99999999999999999999999\n',
                   '1\n1 2147483647 1\n']
        for data in invalid:
            path.write_text(data)
            assert run(path, 'FCFS').returncode != 0, data
        path.write_text('1\n0 0 1\n')
        for q in ['0', '-1', 'abc', '2x', '999999999999999999999999']:
            assert run(path, 'RR', q).returncode != 0
        for args in [[], [str(path)], [str(path), 'OTHER'], [str(path), 'RR'],
                     [str(path), 'FCFS', '2'], [str(path) + '.missing', 'FCFS']]:
            assert subprocess.run([BINARY, *args], capture_output=True).returncode != 0
        # The original fixed RR history failed at 10,000 slices.
        path.write_text('1\n0 0 10001\n')
        check(path, [(0, 0, 10001)], 'RR', 1, 0)
    print('PASS: 9 assignment runs, 609 model comparisons, 23 invalid-input/CLI checks,')
    print('and a 10,001-slice Round Robin regression. Full sample logs: output/runs/')


if __name__ == '__main__':
    main()
