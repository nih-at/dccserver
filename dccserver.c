/* $NiH: dccserver.c,v 1.5 2002/10/13 23:42:28 wiz Exp $ */
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGRAM "dccserver"
#define VERSION "0.1"

#define BACKLOG 10

typedef enum state_e { ST_NONE, ST_CHAT, ST_FSERVE, ST_SEND, ST_GET,
		       ST_END } state_t;

static const char *prg;
static char nickname[1024];
static char filename[1024];
static long filesize;
static char partner[100];

volatile int sigchld = 0;
volatile int sigint = 0;

#ifndef MIN
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif

void
say(char *line, FILE *fp)
{
    fwrite(line, strlen(line), 1, fp);
    return;
}

void
tell_client(FILE *fp, int retcode, char *fmt, ...)
{
    va_list ap;

    fprintf(fp, "%03d %s", retcode, nickname);
    if (fmt != NULL) {
	fprintf(fp, " ");
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
    }
    fprintf(fp, "\n");

    return;
}

int
get_file(FILE *fp)
{
    char buf[8192];
    int len;
    long rem;
    int out;

    /* XXX: more checks */
    if ((out=open(filename, O_WRONLY|O_CREAT|O_EXCL, 0644)) == -1) {
	warn("can't open file `%s' for writing",  filename);
	return -1;
    }

    rem = filesize;
    while((len=fread(buf, 1, MIN(rem, sizeof(buf)), fp)) > 0) {
	if (write(out, buf, len) < len) {
	    warn("write error on `%s'", filename);
	    close(out);
	    return -1;
	}
	rem -= len;

	if (rem <= 0)
	    break;
    }

    if (close(out) == -1)
	warn("close error for `%s'", filename);

    if (rem <= 0) {
	warnx("`%s' complete (%ld bytes got)", filename, filesize);
	return 0;
    }

    if (ferror(fp))
	warn("error getting file `%s'", filename);

    return -1;
}
    
int
parse_get_line(char *line)
{
    char *p, *q, *endptr;

    if ((p=strtok(line+4, " ")) == NULL)
	return -1;

    strlcpy(partner, line+4, sizeof(partner));
    if ((q=strtok(NULL, " ")) == NULL)
	return -1;

    filesize = strtol(q, &endptr, 10);
    if (*q == '\0' || *endptr != '\0' || (filesize <=0))
	return -1;

    if ((q=strtok(NULL, " ")) == NULL)
	return -1;

    strlcpy(filename, q, sizeof(filename));
    if ((strlen(filename) == 0) || (strchr(filename, '/') != NULL))
	return -1;

    return 0;
}    

void
sig_handle(int signal)
{
    switch(signal) {
    case SIGCHLD:
	sigchld++;
	break;
    case SIGINT:
	sigint++;
	break;
    default:
	break;
    }

    return;
}

state_t
converse_with_client(FILE *fp, state_t state, char *line)
{
    state_t ret;

    ret = state;
    switch(state) {
    case ST_NONE:
	if (strncmp("100 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
	    tell_client(fp, 101, NULL);
	    warnx("starting chat with %s", partner);
	    ret = ST_CHAT;
	}
	else if (strncmp("110 ", line, 4) == 0) {
	    strlcpy(partner, line+4, sizeof(partner));
#ifdef NOT_YET
	    tell_client(fp, 111, NULL);
	    warnx("starting fserve session with %s", partner);
	    ret = ST_FSERVE;
#endif
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
	}
	else if (strncmp("120 ", line, 4) == 0) {
	    /* Client sending file */
	    if (parse_get_line(line) < 0) {
		tell_client(fp, 151, NULL);
		break;
	    }

	    /* XXX: resume */
	    tell_client(fp, 121, "0");
	    get_file(fp);
	    ret = ST_END;
	}
	else if (strncmp("130 ", line, 4) == 0) {
#ifdef NOT_YET
	    tell_client(fp, 131, NULL);
	    ret = ST_GET;
#endif
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
	}
	else {
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
	    break;
	}
	break;

    case ST_CHAT:
	printf("<%s> %s\n", partner, line);
	break;

    case ST_FSERVE:
	if (strcasecmp(line, "quit") == 0 ||
	    strcasecmp(line, "exit") == 0) {
	    say("Goodbye!", fp);
	    ret = ST_END;
	}
	break;
    case ST_GET:
	break;   

    case ST_SEND:
    case ST_END:
    default:
	    tell_client(fp, 151, NULL);
	    ret = ST_END;
    }

    return ret;
}

void
do_child(int sock)
{
    FILE *fp;
    char buf[8192];
    char *p;
    state_t state;

    state = ST_NONE;

    if ((fp=fdopen(sock, "r+")) == NULL) {
	(void)close(sock);
	err(1, "[child] can't fdopen");
    }

    while ((state != ST_END) && (fgets(buf, sizeof(buf), fp) != NULL)) {
	if ((*buf == '\n') || (*buf == '\r')) {
	    /* empty line */
	    *buf = '\0';
	}
	else if ((p=strtok(buf, "\n\r")) == NULL) {
	    warn("client sent too long line");
	    tell_client(fp, 151, NULL);
	    state = ST_END;
	    continue;
	}
	state = converse_with_client(fp, state, buf);
    }

    if (state != ST_END)
	warnx("closing connection with %s", partner);

    fclose(fp);
    exit(0);
}


void
handle_connection(int sock)
{
    pid_t child;

    switch(child=fork()) {
    case 0:
	do_child(sock);
	/* UNREACHABLE */
	_exit(1);

    case -1:
	warn("fork error");
	break;
	
    default:
	break;
    }

    close(sock);
    return;
}

void
usage(void)
{

    fprintf(stderr, "%s: emulate mirc's /dccserver command\n\n"
	    "Usage:\n"
	    "%s [-n nickname] [-p port]\n"
	    "Where port is the port on which %s should listen,\n"
	    "and nickname the nick that should be used (defaults are 59 "
	    "and 'dccserver').", prg, prg, prg);
    exit(1);
}

int
main(int argc, char *argv[])
{
    char *endptr;
    int sock, sock_opt;
    int c;
    long port;
    struct sockaddr_in laddr;

    snprintf(nickname, sizeof(nickname), "dccserver");
    port = 59;
    prg = argv[0];

    signal(SIGCHLD, sig_handle);

    while ((c=getopt(argc, argv, "hn:p:v")) != -1) {
	switch(c) {
	case 'h':
	    usage();

	case 'v':
	    printf(PROGRAM " " VERSION);
	    exit(0);

	case 'n':
	    snprintf(nickname, sizeof(nickname), "%s", optarg);
	    break;

	case 'p':
	    port = strtol(optarg, &endptr, 10);
	    if (*optarg == '\0' || *endptr != '\0')
		usage();

	    break;

	default:
	    /* unreachable */
	    exit(1);
	}
    }

    if ((sock=socket(AF_INET, SOCK_STREAM, 0)) == -1)
	err(1, "can't open socket");

    if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1) {
	(void)close(sock);
	err(1, "can't set socket close-on-exec");
    }

    /* Tell system that local addresses can be reused. */
    sock_opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt,
		   sizeof(sock_opt)) == -1) {
	(void)close(sock);
	err(1, "setsockopt(SO_REUSEADDR) failed");
    }

    memset(&laddr, '\0', sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(port);
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
	(void)close(sock);
	err(1, "can't bind socket to port %ld", port);
    }

    if (listen(sock, BACKLOG) == -1)
	err(1, "can't listen on socket");

    while (1) {
	struct sockaddr_in raddr;
	socklen_t raddrlen;
	int new_sock;

	while (sigchld > 0) {
	    int status;

	    sigchld--;
	    wait(&status);
	}

	raddrlen = sizeof(raddr);
	if ((new_sock=accept(sock, (struct sockaddr *)&raddr,
			     &raddrlen))  == -1) {
	    if ((errno != EINTR) && (errno != ECONNABORTED))
		warn("accept failed");
	    continue;
	}

	(void)printf("Connection from %s/%d\n",
		     inet_ntoa(raddr.sin_addr), ntohs(raddr.sin_port));

	/* do something */
	handle_connection(new_sock);
    }

    return 0;
}
