#pragma once

#define SERVER_TCP_PORT 60000
#define CL_UP_RECV_PORT 60001
#define SERVER_TEXTURE_HANDLER_PORT 60002
#define CLIENT_WUP_RECEIVER_PORT 60003
#define CLIENT_TEXTURE_HANDLER_PORT 60004

#define HEADER_SIZE 4

void* wup_receiver(void* arg);
void unknown_veh_handler( char server_address[] int id, World* world);


typedef struct {
  ListHead vehicles;
  int my_id;
  World* world;
  struct sockaddr_in* server_addr;
} wup_receiver_args;

typedef struct {
    Vehicle* veh;
    struct sockaddr_in server_addr;
} client_updater_args;
