#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <semaphore.h>

#include "so_game_protocol.h"
#include "server_common.h"
#include "error_helper.h"


void* server_connection_handler(void* arg)
{
    int ret, client_id, tcp_socket_desc, bytes_to_send, bytes_sent, bytes_to_read, bytes_read;
    char buffer[1024*1024*5];
    connection_handler_args* args = (connection_handler_args*) arg;
    tcp_socket_desc = args->tcp_socket_desc;
    client_id = args->tcp_socket_desc;

    //sending id
    bytes_to_read = message_size_getter(tcp_socket_desc, HEADER_SIZE);
    bytes_read = 0;
    while (bytes_read < bytes_to_read)
    {
        ret = recv(tcp_socket_desc, buffer+bytes_read, bytes_to_read-bytes_read, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Could not read id packet from client");
        bytes_read += ret;
    }
    IdPacket* id_packet = (IdPacket*) Packet_deserialize(buffer, read_bytes);
    if (DEBUG && id_packet->id == -1) printf("id request received from socket: %d\n", tcp_socket_desc);

    id_packet->id = client_id;
    bytes_to_send = Packet_serialize(buffer+HEADER_SIZE, (PacketHeader*) id_packet);
    memcpy(buffer, &bytes_to_send, HEADER_SIZE);
    bytes_to_send += HEADER_SIZE;
    bytes_sent = 0;
    while (bytes_sent < bytes_to_send)
    {
        ret = send(tcp_socket_desc, buffer+bytes_sent, bytes_to_send-bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Could not send back id to socket\n");
    }
    Packet_free(id_packet);


    //receiving texture
    if (DEBUG) printf("waiting to receive client texture...\n");
    bytes_read = 0;
    bytes_to_read = message_size_getter(tcp_socket_desc, HEADER_SIZE);
    while (bytes_read < bytes_to_read)
    {
        ret = recv(tcp_socket_desc, buffer+bytes_read, bytes_to_read-bytes_read, 0);
        if (ret ==-1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Cannot recv texture from client\n");
        bytes_read += ret;
    }
    ImagePacket* texture_packet = (ImagePacket*) Packet_deserialize(buffer, read_bytes);
    if (texture_packet->id != client_id || texture_packet->header.type != 0x2)
        ERROR_HELPER(-1, "Id or packet type not corresponding to client, closing connection\n");
    if (DEBUG) printf("Client: %d texture received successfully\n", client_id);


    //sending map texture
    ImagePacket* map_texture = malloc(sizeof(ImagePacket));
    map_texture->id = 0;
    map_texture->image = args->map_info->map_texture;
    map_texture->header.type = 0x4;
    map_texture->header.size = sizeof(map_texture);
    bytes_to_send = Packet_serialize(buffer+HEADER_SIZE, (PacketHeader*) map_texture);
    memcpy(buffer, bytes_to_send, HEADER_SIZE);
    bytes_to_send += HEADER_SIZE;
    bytes_sent = 0;
    while (bytes_sent < bytes_to_send)
    {
        ret = send(tcp_socket_desc, buffer+bytes_sent, bytes_to_send-bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Could not send map texture to client: %d\n", client_id);
        bytes_sent += ret;
    }
    free(map_texture);
    if (DEBUG) printf("map texture sent to client: %d\n", client_id);

    //sending map elevation
    ImagePacket* map_elevation = malloc(sizeof(ImagePacket));
    map_elevation->id = 0;
    map_elevation->image = args->map_info->map_elevation;
    map_elevation->header.type = 0x4;
    map_elevation->header.size = sizeof(map_elevation);
    bytes_to_send = Packet_serialize(buffer+HEADER_SIZE, (PacketHeader*) map_elevation);
    memcpy(buffer, bytes_to_send, HEADER_SIZE);
    bytes_to_send += HEADER_SIZE;
    bytes_sent = 0;
    while (bytes_sent < bytes_to_send)
    {
        ret = send(tcp_socket_desc, buffer+bytes_sent, bytes_to_send-bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        ERROR_HELPER(ret, "Could not send map texture to client: %d\n", client_id);
        bytes_sent += ret;
    }
    free(map_elevation);
    if (DEBUG) printf("map elevation sent to client: %d\n", client_id);


    //adding client to client_list and wup
    Client_info* client = malloc(sizeof(Client_info));
    client->id = client_id;
    client->client_addr = args->client_addr;
    Vehicle* veh = (Vehicle*) malloc(sizeof(Vehicle));
    Vehicle_init(veh, args->world, client_id, texture_packet->image);

    if (DEBUG) printf("Logging client into game...\n");
    sem_t* sem = sem_open(WUP_SEM, 0);
    ret = sem_wait(sem);
    ERROR_HELPER(ret, "Cannot wait wup semaphore\n");
    List_insert(args->client_list, args->client_list->last, (ListItem*) client);
    World_addVehicle(args->world, veh);
    ret = sem_post(WUP_SEM);
    ERROR_HELPER(ret, "Could not post wup sem\n");
    ret = sem_close(sem);
    ERROR_HELPER(ret, "Could not close wup_sem\n");
    if (DEBUG) printf("Client: %d logged in successfully\n", client_id);


    //keep receiving vehicle updates from client
    if (DEBUG) printf("Now receiving vehicle updates from client: %d\n",client_id);
    while (1)
    {
        bytes_to_read = message_size_getter(args->cl_up_recv_sock, HEADER_SIZE);

        if (bytes_to_read == 0)
        {
            sem = sem_open(WUP_SEM, 0);
            ERROR_HELPER(ret, "Could not open wup sem to log out client\n");
            ret = sem_wait(sem);
            ERROR_HELPER(ret, "Could not wait wup sem to log out client\n");
            List_detach(args->client_list, (ListItem*) client);
            wup_cl_remove(wup, client_id);
            ret = sem_post(sem);
            ERROR_HELPER(ret, "Could not post wup sem to log out client\n");
            ret = sem_close(sem);
            ERROR_HELPER(ret, "Could not close wup sem to log out client\n");

            pthread_exit(NULL);
        }
        bytes_read = 0;
        VehicleUpdatePacket* vup = (VehicleUpdatePacket*) Packet_deserialize( buffer, bytes_to_read);
        if (vup->id == client_id) {
            sem = sem_open(WUP_SEM, 0);
            ret = sem_wait(sem);
            ERROR_HELPER(ret, "Could not wait wup sem to update client position\n");
            veh->translational_force_update = vup->translational_force;
            veh->rotational_force_update = vup->rotational_force;
            Vehicle_update(veh, args->world->dt);
            cl_up->x = veh->x;
            cl_up->y = veh->y;
            cl_up->theta = cl_up->theta;
            if (DEBUG) printf("Updated vehicle: %d on %d\n", client_id, args->world->dt);
        }
    }
}


void wup_cl_remove(WorldUpdatePacket* wup, int client_id)
{
    int i, j=0, n_veh= wup->num_vehicles;
    WorldUpdatePacket* new = malloc(sizeof(WorldUpdatePacket));
    new->num_vehicles = n_veh-1;
    new->updates = malloc(sizeof(ClientUpdate)*new->num_vehicles);
    for (i=0;i<n_veh;i++)
    {
        if (wup->updates[i].id != client_id)
        {
            new->updates[j] = wup->updates[i];
            j++;
        }
    }
    free(wup->updates);
    wup->updates = new->updates;
    wup->num_vehicles = n_veh-1;
}
