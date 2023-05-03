#MAKEFILE

CC			=  gcc
AR         	=  ar
CFLAGS	    += -std=c99 -Wall -Werror -pedantic -g
ARFLAGS     =  rvs
INCLUDES	= -I ./includes
LDFLAGS 	= -L ./-lfarm
#OPTFLAGS	= -O3 
LIBS        = -lpthread

TARGETS		= farm	\
	generafile
 
.PHONY:	all test clean cleanall 
.SUFFIXES:	.c .h

#regola generale per creare un file oggetto dal file.c
%.o:	%.c
	$(CC) $(CFLAGS) $(INCLUDES) $< -c -o $@ 

all:	$(TARGETS)

farm:	farm.o 	calculateResult.o myfarmlib.a
	$(CC)	$(CFLAGS)	$(INCLUDES) -o $@ $^ $(LDFLAGS) $(LIBS)

myfarmlib.a:	threadPoolWorker.o ./includes/threadpoolworker.h
	$(AR)	$(ARFLAGS)	$@ $<

farm.o:	farm.c

calculateResult.o: calculateResult.c

threadPoolWorker.o:	threadPoolWorker.c

generafile:	generafile.c

test:	farm generafile
	@echo "eseguo test..."
	@chmod +x ./test.sh		
	./test.sh
	@echo "test eseguito!"

clean: 
	-rm -f $(TARGETS)

cleanall:	clean
	-rm -f *~ farm.sck *.o *.a file* expected.txt
