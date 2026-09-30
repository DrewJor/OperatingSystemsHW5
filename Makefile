CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic

proj2: proj2.c
	$(CC) $(CFLAGS) proj2.c -o proj2

.PHONY: test clean
test: proj2
	python3 tests/check_scheduler.py ./proj2

clean:
	rm -f proj2
