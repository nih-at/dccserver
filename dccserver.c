/* $NiH: dccserver.c,v 1.9 2002/10/14 22:30:47 wiz Exp $ */
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

#define NO_OF_CHILDREN 100
typedef struct child_s {
    int sock;
    pid_t pid;
} child_t;

child_t children[NO_OF_CHILDREN];

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
    struct stat sb;
    long offset;

    if (stat(filename, &sb) == 0) {
	/* file exists */
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
	    /* XXX: rename */
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	/* append (resume) */
	if ((sb.st_size > 0) && (sb.st_size < filesize)) {
	    offset = sb.st_size;
	    warnx("file exists, resuming after %ld bytes", offset);
	}
	else {
	    /* XXX: rename */
	    tell_client(fp, 151, NULL);
	    return -1;
	}
	if ((out=open(filename, O_WRONLY|O_APPEND, 0644)) == -1) {
	    warn("can't open file `%s' for appending",  filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
    }
    else {
	offset = 0;
	if ((out=open(filename, O_WRONLY|O_CREAT|O_EXCL, 0644)) == -1) {
	    warn("can't open file `%s' for writing",  filename);
	    tell_client(fp, 151, NULL);
	    return -1;
	}
    }

    tell_client(fp, 121, "%ld", offset);

    rem = filesize - offset;
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
	warnx("`%s' complete (%ld/%ld bytes got, %ld new)", filename,
	      filesize, filesize, filesize-offset);
	return 0;
    }
    else {
	warnx("client closed connection for `%s' (%ld/%ld bytes, "
	      "%ld new)", filename, filesize-rem, filesize,
	      filesize-offset-rem);
    }    

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

    /* XXX: wrong */
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
	sigchld = 1;
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
    char *p;

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
		ret = ST_END;
		break;
	    }

	    warnx("getting file `%s' (%ld bytes) from %s", filename,
		  filesize, partner);

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
	printf("<%s> ", partner);
	p = line;
	while (*p) {
	    switch (*p) {
	    case 0x03:
		/* skip control-C */
		p++;
		/*
		 * syntax: ^CN[,M] -- 0<=N<=99, 0<=M<=99, N fg, M bg,
		 *         or ^C to turn color off
		 */
		/* fg */
		if ('0' <= *p && *p <= '9') {
		    if ('0' <= p[1] && p[1] <= '9') {
			p++;
		    }
		    p++;
		}
		if (*p == ',') {
		    p++;
		    /* bg */
		    if ('0' <= *p && *p <= '9') {
			if ('0' <= p[1] && p[1] <= '9') {
			    p++;
			}
			p++;
		    }
		}
		break;

	    case 0x02:
		/* bold */
	    case 0x0f:
		/* plain text */
	    case 0x12:
		/* reverse */
	    case 0x15:
		/* underlined */
	    case 0x1f:
		break;
		
	    default:
		putchar(*p);
		break;
	    }
	    p++;
	}
	putchar('\n');
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
communicate_with_client(int sock)
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
    int i;

    switch(child=fork()) {
    case 0:
	communicate_with_client(sock);
	/* UNREACHABLE */
	_exit(1);

    case -1:
	warn("fork error");
	break;
	
    default:
	break;
    }

    for (i=0; i<NO_OF_CHILDREN; i++) {
	if (children[i].pid == -1) {
	    children[i].pid = child;
	    children[i].sock = sock;
	    warnx("child %d started (pid %d)", i, child);
	    return;
	}
    }

    warnx("too many children");
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

void
handle_input(void)
{
    char buf[8192];
    int child;
    char *end;

    if (fgets(buf, sizeof(buf), stdin) == NULL)
	return;

    child = strtol(buf, &end, 10);
    if ((child >= 0 && child < NO_OF_CHILDREN) && end[0] == ':'
	&& end[1] == ' ') {
	if (children[child].pid == -1) {
	    warnx("child %d is dead", child);
	    return;
	}

	write(children[child].sock, end+2, strlen(end+2));
    }

    return;
}
int
main(int argc, char *argv[])
{
    char *endptr;
    int sock, sock_opt;
    int c;
    int pollret;
    long port;
    struct sockaddr_in laddr;
    struct pollfd pollset[2];

    snprintf(nickname, sizeof(nickname), "dccserver");
    port = 59;
    prg = argv[0];
    for (c=0; c<NO_OF_CHILDREN; c++)
	children[c].pid = -1;

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
	    int i;
	    int status;
	    pid_t deadpid;

	    while (((deadpid=waitpid(-1, &status, WNOHANG)) != -1)
		   && deadpid != 0) {
		sigchld = 0;
		for (i=0; i<NO_OF_CHILDREN; i++) {
		    if (children[i].pid == deadpid) {
			children[i].pid = -1;
			close(children[i].sock);
			warnx("child %d died", i);
			break;
		    }
		}
		if (i == NO_OF_CHILDREN)
		    warnx("child %ld found dead, but unknown", (long)deadpid);
	    }
	}

	pollset[0].fd = 0;
	pollset[0].events = POLLIN|POLLPRI;
	pollset[0].revents = 0;
	pollset[1].fd = sock;
	pollset[1].events = POLLIN|POLLPRI;
	pollset[1].revents = 0;

	if ((pollret=poll(pollset, 2, 500)) == -1) {
	    if (errno != EINTR)
		warn("poll error");
	    continue;
	}

	if (pollret == 0)
	    continue;

	if (pollset[0].revents != 0) {
	    /* some data from stdin */

	    handle_input();
	}
	if (pollset[1].revents != 0) {
	    /* some data from network */
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
    }

    return 0;
}
