#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <err.h>
#include <syslog.h>
#include <stdarg.h>
#include <libutil.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#include "devmsg.h"
#define VERSION "0.1"
#define PATH_MSG "/dev/devmsg"
#define OPTSTRING "hvVsd"
#define LEVEL_LEN 10

static volatile sig_atomic_t daemon_start = 0;
static int no_daemon = 0;
static void eventdlog(int priority, const char* message, ...)
	__printflike(2, 3);

struct pidfh *pfh;

static struct
option longopts[] =
{
	{ "help",	no_argument, 	NULL,	'h'},
	{ "verson",	no_argument, 	NULL,	'v'},
	{ "verson",	no_argument, 	NULL,	'V'},
	{ "no daemon",	no_argument, 	NULL,	'd'},
	{ NULL,		0, 		NULL,	0}
};

static char
log_level[][LEVEL_LEN] =
{
	"INFO",
	"WARNING",
	"ERROR"
};

static void
sighandler(int signo)
{
	daemon_start = 1;
}

void
show_help(char *prog)
{
	fprintf(stderr, "\n"\
		"%s usage: %s [option]\n"\
		"options:\n"\
		"   -h | --help     show help\n"\
		"   -v | -V | --version show version\n"\
		"   -d | --daemon	start as no daemon\n", prog, prog);
	return;
}

void
show_version(char * prog)
{
	printf("%s version "VERSION"\n", prog);
	return;
}

void
open_pidfile()
{
	pid_t otherpid;

	pfh = pidfile_open("/var/run/eventd.pid", 0600, &otherpid);
	if(pfh == NULL)
		warn("eventd cannot open pid file");
}

void
remove_pidfile()
{
	pidfile_remove(pfh);
}

void
write_pidfile()
{
	pidfile_write(pfh);
}

void
close_pidfile()
{
	pidfile_close(pfh);
}

static void
event_loop(void)
{
	int fd;
	int rv;
	int read_len;
	int FD_ISSET_check = 0;
	int once = 0;
	char *buf;
	struct msg_event event;
	struct timeval tv;
	fd_set fds;
	
	fd = open(PATH_MSG, O_RDONLY | O_CLOEXEC);

	if(fd == -1) {
		err(1, "Can't open devmsg device %s", PATH_MSG);
		return;
	}

	while(!daemon_start)
	{
		if(!no_daemon && !once) {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			rv = select(fd + 1, &fds, &fds, &fds, &tv);

			if(rv >= 0) {
				remove_pidfile();
				open_pidfile();
				daemon(0,0);
				write_pidfile();
				once++;
			}
			else {
				eventdlog(LOG_INFO, "eventd select fail");
				break;
			}
		}
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if(FD_ISSET_check == 0) {
			tv.tv_sec = 10;
			tv.tv_usec = 0;
		}
		else {
			tv.tv_sec = 2;
			tv.tv_usec = 0;
		}
		rv = select(fd + 1, &fds, NULL, NULL, &tv);
		if(FD_ISSET(fd, &fds) && rv >= 0) {
			read_len = sizeof(event);
			buf = (char*)&event;
			while(read_len != 0 && (rv = read(fd, buf, read_len)) != 0) {
				if(rv == -1) {
					break;
				}
				read_len -= rv;
				buf += rv;
			}
			if(rv > 0) {
				FD_ISSET_check = 1;
				switch (event.type) {
					case RAWSTRING:
						eventdlog(LOG_INFO, 
						"[EVT_LOG] level='%s';"
						"message='%s'",
						log_level[event.level],
						event.param.raw_string.msg_string);
						break;
					default:
						eventdlog(LOG_INFO, "Type not support");
						break;
				}
			}
			else {
				FD_ISSET_check = 0;
				eventdlog(LOG_INFO, "eventd fail to read\n");
			}
		}
		else if(FD_ISSET(fd, &fds) && rv < 0) {
			close(fd);
			return; /* EINTER */
		}
		else {
			FD_ISSET_check = 0;
		}
	}
	close(fd);
}

static void
eventdlog(int priority, const char* fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	if (no_daemon)
		vfprintf(stdout, fmt, argp);
	else// if (priority <= LOG_WARNING)
		vsyslog(priority, fmt, argp);
	va_end(argp);
}

int
main(int argc, char *argv[])
{
	int opt = 0;
	int is_daemon = 0;
	struct sigaction sa;
	
	while ((opt = getopt_long(argc, argv, OPTSTRING, longopts, NULL)) >= 0)
	{
		switch (opt)
		{
			case 'h':
				show_help(argv[0]);
				exit(0);
			case 'v':
			case 'V':
				show_version(argv[0]);
				exit(0);
			case 'd':
				no_daemon = 1;
				break;
			default:
				show_version(argv[0]);
				exit(10);
		}
	}

/* Initialization */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGKILL, sighandler);
/* Initialization end */
	event_loop();

	return (0);	
}
