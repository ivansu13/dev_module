#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>  /* uprintf */
#include <sys/param.h>  /* defines used in kernel.h */
#include <sys/kernel.h> /* types used in module initialization */
#include <sys/conf.h>   /* cdevsw struct */
#include <sys/uio.h>    /* uio struct */
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/selinfo.h>
#include <sys/signalvar.h>
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sysctl.h>

#include <sys/devmsg.h>

static MALLOC_DEFINE(M_BUS, "bus", "Bus data structures");
static MALLOC_DEFINE(M_BUS_SC, "bus-sc", "Bus data structures, softc");


static int queue_length = MSG_DEFAULT_QUEUE_LEN;
static int msg_queue(struct msg_event *event);
struct timespec tv, tv2;

static d_open_t      msg_open;
static d_close_t     msg_close;
static d_read_t      msg_read;
static d_poll_t      msg_poll;

static struct cdevsw msg_cdevsw = {
	.d_version = D_VERSION,
	.d_open = msg_open,
	.d_close = msg_close,
	.d_read = msg_read,
	.d_poll = msg_poll,
	.d_name = "devmsg",
};

struct msg_event_info
{
	TAILQ_ENTRY(msg_event_info) event_info_link;
	struct msg_event *event_info;
};

TAILQ_HEAD(msgq, msg_event_info);

static struct dev_softc
{
	int	inuse;
	int	nonblock;
	int	queued;
	int	async;
	struct mtx mtx;
	struct cv cv;
	struct msgq msgq;
	struct selinfo sel;
} msg_softc;
static struct cdev *msg_dev;

static void
msg_init(void)
{
	msg_dev = make_dev_credf(MAKEDEV_ETERNAL, &msg_cdevsw, 0, NULL,
	    UID_ROOT, GID_WHEEL, 0600, "devmsg");
	mtx_init(&msg_softc.mtx, "msg mtx", "msgd", MTX_DEF);
	cv_init(&msg_softc.cv, "msg cv");
	TAILQ_INIT(&msg_softc.msgq);
}

int 
send_rawstring(int level, char *rawstring)
{
	int error = 0;	
	struct msg_event event;

	event.level = level;
	evetn.type = RAWSTRING;
	snprintf(event.param.raw_string.msg_string,
		 strlen(rawstring) + 1, "%s", rawstring);

	error = send_msg(event);	
	return (error);
}

int 
send_msg(struct msg_event event)
{	
	int error = 0;
	struct msg_event *me;

	me = malloc(sizeof(event), M_BUS, M_NOWAIT);
	if(me == NULL) {
		printf("msg: unable to allocate enough memory\n");
		return (ENOMEM);
	}
	memcpy(me, &event, sizeof(event));
	error = msg_queue(me);	
	return (error);
}

static int
msg_loader(module_t mod, int module_event, void *arg)
{
	int error = 0;
	switch (module_event) {
	case MOD_LOAD:
		msg_init();
		printf("msg device loaded.\n");
		break;
	case MOD_UNLOAD: 
		destroy_dev(msg_dev);
		printf("msg device unloaded.\n");
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static int
msg_open(struct cdev *msg_dev, int oflags, int devtype, struct thread *td)
{
	if(!mtx_initialized(&msg_softc.mtx))
		return (ENOLCK);
	mtx_lock(&msg_softc.mtx);
	
	if (msg_softc.inuse) {
		mtx_unlock(&msg_softc.mtx);
		return (EBUSY);
	}
	msg_softc.inuse = 1;
	mtx_unlock(&msg_softc.mtx);

	return (0);
}

static int
msg_close(struct cdev *msg_dev, int fflag, int devtype, struct thread *td)
{
	if(!mtx_initialized(&msg_softc.mtx))
		return (ENOLCK);
	mtx_lock(&msg_softc.mtx);

	msg_softc.inuse = 0;
	msg_softc.nonblock = 0;
	msg_softc.async = 0;

	cv_broadcast(&msg_softc.cv);
	mtx_unlock(&msg_softc.mtx);
	return (0);
}

static int
msg_read(struct cdev *msg_dev, struct uio *uio, int ioflag)
{
	struct msg_event_info *n1;
	int rv;

	if (!mtx_initialized(&msg_softc.mtx))
		return(ENOLCK); /* No locks available */
	mtx_lock(&msg_softc.mtx);

	while (TAILQ_EMPTY(&msg_softc.msgq)) {
		if (msg_softc.nonblock) {
			mtx_unlock(&msg_softc.mtx);
			return (EAGAIN);
		}
		rv = cv_wait_sig(&msg_softc.cv, &msg_softc.mtx);
		if (rv) {
			mtx_unlock(&msg_softc.mtx);
			return (rv);
		}
	}
	n1 = TAILQ_FIRST(&msg_softc.msgq);
	TAILQ_REMOVE(&msg_softc.msgq, n1, event_info_link);
	msg_softc.queued--;
	
	mtx_unlock(&msg_softc.mtx);
	
	rv = uiomove(n1->event_info, uio->uio_resid, uio);
	free(n1->event_info, M_BUS);
	free(n1, M_BUS);
	return (rv);
}

static int
msg_queue(struct msg_event *event)
{
	struct msg_event_info *n1 = NULL, *n2 = NULL;
	int rv = 0;

	if (!mtx_initialized(&msg_softc.mtx)) {
		free(event, M_BUS);
		return(ENOLCK); /* No locks available */
	}
	mtx_lock(&msg_softc.mtx);

	n1 = malloc(sizeof(*n1), M_BUS, M_NOWAIT);
	if (n1 == NULL)	{
		free(event, M_BUS);
		return(ENOMEM); /* Cannot allocate memory */
	}

	n1->event_info = event;
	
	/* if que is full, drop old data */
	while(msg_softc.queued > queue_length - 1) {
		kern_clock_gettime(curthread, CLOCK_REALTIME, &tv2);
		if(tv2.tv_sec - tv.tv_sec >= 30) {
			printf("msg queue is full, messages are dropped.\n");
			memcpy(&tv, &tv2, sizeof(tv2));
		}
		n2 = TAILQ_FIRST(&msg_softc.msgq);
		TAILQ_REMOVE(&msg_softc.msgq, n2, event_info_link);
		free(n2->event_info, M_BUS);
		free(n2, M_BUS);
		msg_softc.queued--;
	}
	TAILQ_INSERT_TAIL(&msg_softc.msgq, n1, event_info_link);
	msg_softc.queued++;
	
	cv_broadcast(&msg_softc.cv);
	mtx_unlock(&msg_softc.mtx);
	selwakeup(&msg_softc.sel);
	return(rv);
}

/* poll is for daemon select */
static	int
msg_poll(struct cdev *msg_dev, int events, struct thread *td)
{
	int revents = 0;

	if (!mtx_initialized(&msg_softc.mtx))
		return(ENOLCK);
	mtx_lock(&msg_softc.mtx);

	if (events & (POLLIN | POLLRDNORM)) {
		if (!TAILQ_EMPTY(&msg_softc.msgq))
			revents = events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &msg_softc.sel);
	}

	mtx_unlock(&msg_softc.mtx);

	return (revents);
}

DEV_MODULE(msg, msg_loader, NULL);
