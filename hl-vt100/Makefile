##
## Makefile for vt100
##
## Made by julien palard
##

NAME = vt100
VERSION = 0
MINOR = 0
RELEASE = 0

LINKERNAME = lib$(NAME).so
SONAME = $(LINKERNAME).$(VERSION)
REALNAME = $(SONAME).$(MINOR).$(RELEASE)

SRC = src/lw_terminal_parser.c src/lw_terminal_vt100.c src/hl_vt100.c
SRC_TEST = src/test.c
OBJ = $(SRC:.c=.o)
OBJ_TEST = $(SRC_TEST:.c=.o)
CC = gcc
INCLUDE = src
DEFINE = _GNU_SOURCE
CFLAGS = -DNDEBUG -g3 -Wextra -Wstrict-prototypes -Wall -std=gnu17 -fPIC -I$(INCLUDE)
LIB = -lutil
RM = rm -f

$(NAME):	$(OBJ)
		$(CC) --shared $(OBJ) $(LIB) -o $(LINKERNAME)

test:	$(OBJ_TEST)
		$(CC) $(OBJ_TEST) -L . -l$(NAME) -o test

all:
		@make $(NAME)

.c.o:
		$(CC) -D $(DEFINE) -c $(CFLAGS) $< -o $(<:.c=.o)

clean:
		$(RM) $(LINKERNAME) test src/*~ *~ src/\#*\# src/*.o \#*\# *.o *core

re:		clean all

check-syntax:
		gcc -Isrc -Wall -Wextra -Wfallthrough -std=gnu17 -o /dev/null -S ${CHK_SOURCES}
