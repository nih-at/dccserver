#include <sys/socket.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

static char *prg;

void
usage(void)
{

    fprintf(stderr, "%s: emulate mirc's /dccserver command\n\n"
	    "Usage:\n"
	    "%s hostname port\n"
	    "Where hostname and port are the is the name of the interface "
	    "and the port\n"
	    "on which %s should listen.\n", prg, prg, prg);
    exit(1);
}

int
main(int argc, char *argv[])
{
    char *endptr;
    char *hostname;
    int s;
    long port;
    struct sockaddr addr;

    if (argc < 3)
	usage();

    prg = argv[0];
    hostname = argv[1];
    port = strtol(argv[2], &endptr, 10);
    if (*argv[2] == '\0' || *endptr != '\0')
	usage();

    if ((s=socket(PF_INET, SOCK_STREAM, 0)) == -1)
	err(1, "can't open socket");

    addr.sin_family = AF_INET;
    addr.sin_port = htons(59);
    
    if (listen(s, 10) == -1)
	err(1, "can't listen on socket");

    return 0;
}
