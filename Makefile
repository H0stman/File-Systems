GCC=g++

all: main.o shell.o fs.o disk.o
	$(GCC) -Wall -g -Wextra -Wpedantic -std=c++11 -o filesystem main.o shell.o disk.o fs.o

main.o: main.cpp shell.h disk.h
	$(GCC) -Wall -g -Wextra -Wpedantic -std=c++11 -c main.cpp

shell.o: shell.cpp shell.h fs.h disk.h
	$(GCC) -Wall -g -Wextra -Wpedantic -std=c++11 -c shell.cpp

fs.o: fs.cpp fs.h disk.h
	$(GCC) -Wall -g -Wextra -Wpedantic -std=c++11 -c fs.cpp

disk.o: disk.cpp disk.h
	$(GCC) -Wall -g -Wextra -Wpedantic -std=c++11 -c disk.cpp

clean:
	rm filesystem main.o shell.o fs.o disk.o
