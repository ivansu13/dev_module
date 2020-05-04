#ifndef _SYS_MSG_H_
#define _SYS_MSG_H_

#define MSG_MAX_LEN 1024
#define BUFFERSIZE 255
#define MSG_DEFAULT_QUEUE_LEN 1000

enum {
	INFO,
	WARNING,
	ERROR
};

enum {
	RAWSTRING,
    STRUCT
};

struct msg_event {
	int type;
	int level;
	union {
		struct _raw_string {
			char msg_string[MSG_MAX_LEN];
		}raw_string;
	}param;
};

int send_msg(struct msg_event msg);
int rawstring(int rawsting_level, char *rawstring);

#endif
