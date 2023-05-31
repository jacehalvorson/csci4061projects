CFLAGS = -Wall -Werror -g
CC = gcc $(CFLAGS)
SHELL = /bin/bash
CWD = $(shell pwd | sed 's/.*\///g')
AN = proj1

minitar: minitar_main.c file_list.o minitar.o
	$(CC) -o minitar minitar_main.c file_list.o minitar.o -lm

file_list.o: file_list.h file_list.c
	$(CC) -c file_list.c

minitar.o: minitar.h minitar.c
	$(CC) -c minitar.c

test: minitar
	@chmod u+x testy
	@chmod u+x minitar_tests.sh
	./testy test_minitar.org $(testnum)

clean:
	rm -f *.o minitar

clean-tests:
	rm -rf testing_dir test-results

zip: clean clean-tests
	rm -f proj1-code.zip
	cd .. && zip "$(CWD)/$(AN)-code.zip" -r "$(CWD)"
	@echo Zip created in $(AN)-code.zip
	@if (( $$(stat -c '%s' $(AN)-code.zip) > 10*(2**20) )); then echo "WARNING: $(AN)-code.zip seems REALLY big, check there are no abnormally large test files"; du -h $(AN)-code.zip; fi
	@if (( $$(unzip -t $(AN)-code.zip | wc -l) > 256 )); then echo "WARNING: $(AN)-code.zip has 256 or more files in it which may cause submission problems"; fi
