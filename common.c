
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "error_helper.h"

int message_size_getter(int socket_desc, int header_size) {
    int bytes_read, bytes_to_read, ret;
    char buffer[header_size];

    bytes_read = 0;
    while (bytes_read < header_size)
    {
        ret = recv(socket_desc, buffer+bytes_read, header_size - bytes_read, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Cannot receive id from server\n");
        bytes_read += ret;
    }
    memcpy(&bytes_to_read, buffer, header_size);
    printf("to read: %d bytes\n", bytes_to_read);
    return bytes_to_read;
}
