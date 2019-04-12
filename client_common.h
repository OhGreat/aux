#pragma once

#define SERVER_TCP_PORT 60000
#define CL_UP_RECV_PORT 60001
#define SERVER_TEXTURE_HANDLER_PORT 60002
#define CLIENT_WUP_RECEIVER_PORT 60003
#define CLIENT_TEXTURE_HANDLER_PORT 60004

#define HEADER_SIZE 4

void* wup_receiver(void* arg);
Image* unknown_veh_handler(int socket_desc, int id, World* world);
void quit_handler(int sig);
void quit_handler_for_main(void);
void* client_updater_for_server(void* arg);

typedef struct {
  ListHead vehicles;
  int my_id;
  World* world;
  struct sockaddr_in* server_addr;
  int tcp_socket;
  Image* texture;
} wup_receiver_args;

typedef struct {
    Vehicle* veh;
    struct sockaddr_in server_addr;
} cl_up_args;
