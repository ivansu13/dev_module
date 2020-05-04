PROG = daemon
SRCS = daemon.c
LDADD=	-lutil -lc
MAN=

.c.o:
	$(CC)  -c $< -o $@	
clean:
	rm -f *.o daemon .depend* daemon.debug daemon.full

.include <bsd.prog.mk>
