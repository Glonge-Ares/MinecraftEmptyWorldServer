#include "Nonblock.h"
#include <fcntl.h>

int setnonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


int setkeepalive(int fd)
{
	
	
	
}
