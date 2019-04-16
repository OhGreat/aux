#pragma once

#include "so_game_protocol.h"


#define SERVER_ADDRESS "127.0.0.1"
#define CLIENT_BROADCAST_ADDR "172.25.1.255"
#define SERVER_TCP_PORT 60000
#define CL_UP_RECV_PORT 60001
#define SERVER_TEXTURE_HANDLER_PORT 60002
#define CLIENT_WUP_RECEIVER_PORT 60003
#define CLIENT_TEXTURE_HANDLER_PORT 60004
#define MAX_CLIENTS 10
#define CLIENT_LIST_SEM "/client_list_sem"
#define WUP_SEM "/wup_sem"
#define HEADER_SIZE 4

typedef struct {
    Image* map_texture;
    Image* map_elevation;
} Map;

typedef struct {
    int tcp_socket_desc;
    struct sockaddr_in* client_addr;
    int cl_up_recv_sock;
    struct sockaddr_in* server_cl_up_recv_addr;
    struct sockaddr_in* cl_up_client_addr;
    int texture_handler_socket;
    World* world;
    Map* map_info;
    WorldUpdatePacket* wup;
    ListHead* client_list;
} connection_handler_args;


typedef struct {
    ListItem list;
    int id;
    struct sockaddr_in* client_addr;
    Vehicle* veh;
    ClientUpdate* cl_up;
} Client_info;

typedef struct {
    World* world;
    WorldUpdatePacket* wup;
    int wup_sender_desc;
} wup_sender_args;

typedef struct {
    int socket_desc;
    World* world;
} texture_handler_args;

typedef struct {
    WorldUpdatePacket* wup;
    ListHead* client_list;
    World* world;
    int cl_up_socket;
} cl_up_args;

void quit_handler(int sig);
void sem_cleanup(void);
void* wup_sender(void* arg);
void* texture_request_handler(void* arg);
void wup_cl_remove(WorldUpdatePacket* wup, int client_id, World* world, Vehicle* veh);
void* cl_up_handler (void* arg);
