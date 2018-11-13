#include <GL/glut.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"
#include "error_helper.h"
#include "client_common.h"
#include "common.c"
#include <assert.h>


int window;
WorldViewer viewer;
World world;
Vehicle* vehicle; // The vehicle

char* global_server_addr;

int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

  printf("loading texture image from %s ... ", argv[2]);
  Image* my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  char buffer[1024*1024*5];

  int main_socket_desc, bytes_to_send, bytes_sent, bytes_to_read, bytes_read, ret;
  struct sockaddr_in server_addr = {0};
  global_server_addr = argv[1];

  main_socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(main_socket_desc, "Could not create socket\n");

  server_addr.sin_addr.s_addr = inet_addr(argv[1]);
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(SERVER_TCP_PORT);

  ret = connect(main_socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
  ERROR_HELPER(ret, "Could not enstamblish connection to server\n");
  if (DEBUG) fprintf(stderr, "Connected to server succesfully\n");

  //requesting id to server
  IdPacket *id_packet = malloc(sizeof(IdPacket));
  id_packet->id = -1;
  id_packet->header.type = 0x1;
  id_packet->header.size = sizeof(id_packet);
  bytes_to_send = Packet_serialize( buffer, (PacketHeader*) id_packet);
  bytes_sent = send(main_socket_desc, &bytes_to_send, sizeof(int), 0);
  bytes_sent = send(main_socket_desc, buffer, bytes_to_send, 0);
  if (DEBUG) printf("id request sent succesfully\n");

  //receiving id from server
  bytes_to_read = message_size_getter(main_socket_desc, HEADER_SIZE);
  bytes_read = 0;
  while (bytes_read < bytes_to_read)
  {
      ret = recv(main_socket_desc, buffer+bytes_read, bytes_to_read - bytes_read, 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Cannot receive id from server\n");
      bytes_read += ret;
  }
  Packet_free( (PacketHeader*) id_packet);
  id_packet = (IdPacket *) Packet_deserialize(buffer, bytes_read);
  int my_id = id_packet->id;
  Packet_free( (PacketHeader*) id_packet);
  if (DEBUG) printf("received id: '%d' from server\n", my_id);

  //sending my vehicle texture to server
  ImagePacket* texture_packet = malloc(sizeof(ImagePacket));
  texture_packet->id = my_id;
  texture_packet->image = my_texture;
  texture_packet->header.type = 0x2;
  texture_packet->header.size = sizeof(texture_packet);
  bytes_to_send = Packet_serialize(buffer+HEADER_SIZE, (PacketHeader*) texture_packet);
  memcpy(buffer, &bytes_to_send, HEADER_SIZE);
  bytes_to_send += HEADER_SIZE;
  bytes_sent = 0;
  while( bytes_sent < bytes_to_send) {
      ret = send(main_socket_desc, buffer + bytes_sent, bytes_to_send - bytes_sent, 0);
    if (errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot send vehicle texture to server\n");
    bytes_sent += ret;
  }
  if (DEBUG) printf("vehicle texture sent succesfully to server, written: %d bytes\n", bytes_to_send);

  //receiving map texture from server
  bytes_to_read = message_size_getter(main_socket_desc, HEADER_SIZE);
  bytes_read = 0;
  while (bytes_read < bytes_to_read)
  {
      ret = recv(main_socket_desc, buffer+bytes_read, bytes_to_read - bytes_read, 0);
      if (ret == -1 && errno == EINTR) continue;
     ERROR_HELPER(ret, "Cannot receive id from server\n");
     bytes_read += ret;
  }
  ImagePacket* map_texture_packet = (ImagePacket*) Packet_deserialize(buffer, bytes_read);
  if (map_texture_packet->id != 0 || map_texture_packet->header.type != 0x4)
      ERROR_HELPER(-1, "Cannot receive map texture from server\n");
  Image* map_texture = map_texture_packet->image;
  free(map_texture_packet);
  if (DEBUG) printf("map texture received succesfully\n");

  //receiving map elevation from server
  bytes_to_read = message_size_getter(main_socket_desc, HEADER_SIZE);
  bytes_read = 0;
  while (bytes_read < bytes_to_read)
  {
      ret = recv(main_socket_desc, buffer+bytes_read, bytes_to_read - bytes_read, 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Cannot receive id from server\n");
      bytes_read += ret;
  }
  ImagePacket* map_elevation_packet = (ImagePacket*) Packet_deserialize(buffer, bytes_read);
  if (map_elevation_packet->id != 0 || map_elevation_packet->header.type != 0x4)
      ERROR_HELPER(-1,"Wrong id or packet type received from server\n");
  Image* map_elevation = map_elevation_packet->image;
  if (DEBUG) printf("elevation map received succesfully\n");

  //creating world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle = (Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, my_id, my_texture);
  World_addVehicle(&world, vehicle);

  //thread to handle wup
  pthread_t wup_receiver_thread;
  wup_receiver_args* thread_args = malloc(sizeof(wup_receiver_args));
  thread_args->vehicles = world.vehicles;
  thread_args->my_id = my_id;
  thread_args->world = &world;
  thread_args->server_addr = &server_addr;
  ret = pthread_create(&wup_receiver_thread, NULL, wup_receiver, thread_args);
  ERROR_HELPER(ret, "Could not create wup receiver thread\n");
  ret = pthread_detach(wup_receiver_thread);
  ERROR_HELPER(ret, "Unable to detach wup receiver thread\n");
  if (DEBUG) printf("Created thread to receive wup from server\n");

  //thread to send cl_up to server
  pthread_t cl_up_thread;
  cl_up_args* cl_args = malloc(sizeof(cl_up_args));
  cl_args->veh = vehicle;
  cl_args->server_addr = server_addr;
  ret = pthread_create(&cl_up_thread, NULL, client_updater_for_server, cl_args);
  ERROR_HELPER(ret, "Could not create cl_up sender thread\n");
  ret = pthread_detach(cl_up_thread);
  ERROR_HELPER(ret, "Unable to detach cl_up sender thread\n");
  if (DEBUG) printf("Created thread to send cl_up to server\n");

  //signal(SIGINT, quit_handler);

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);
  //cleanup
  World_destroy(&world);
  return 0;
}


//handles wup received from server and calls unknown_veh_handler when needed
void* wup_receiver (void* arg)
{
    int ret, socket_desc, bytes_read, bytes_to_read;
    struct sockaddr_in client_addr;
    char buffer[1024*1024*5];
    wup_receiver_args* args = (wup_receiver_args*) arg;
    int i, update_vehs, world_vehs;
    WorldUpdatePacket* wup;

    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER( socket_desc, "Error opening wup receiver socket\n");

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_WUP_RECEIVER_PORT);
    client_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(socket_desc, (struct sockaddr*) &client_addr, sizeof(client_addr));
    ERROR_HELPER( ret, "Error binding wup receiver port to socket\n");

    Vehicle* current_veh = (Vehicle*) args->vehicles.first;
    while (1)
    {
        bytes_read = 0;
        ret = recv(socket_desc, &bytes_to_read, HEADER_SIZE, MSG_WAITALL);
        ret = recv(socket_desc, buffer, bytes_to_read, MSG_WAITALL);
        printf("byte read = %d\n", ret);

        wup = (WorldUpdatePacket*) Packet_deserialize( buffer, bytes_read);
        assert(wup && "invalid wup");
        update_vehs = wup->num_vehicles;
        for (i=0; i<update_vehs; i++)
        {
            if (wup->updates[i].id != args->my_id &&
                (current_veh = World_getVehicle(args->world, wup->updates[i].id)) != 0)
            {
                current_veh->x = wup->updates[i].x;
                current_veh->y = wup->updates[i].y;
                current_veh->theta = wup->updates[i].theta;
            }
            else if (wup->updates[i].id != args->my_id)
            {
              unknown_veh_handler(args->server_addr, wup->updates[i].id, args->world, wup->updates[i]);
            }
        }
        Vehicle* veh = (Vehicle*) args->vehicles.first;
        int j;
        world_vehs = args->world->vehicles.size;
        for (j=0;j<world_vehs;j++) {
          for (i=0; i < update_vehs && veh->id != wup->updates[i].id; i++) {
          }
          if (veh->id != wup->updates[i].id ) World_detachVehicle(args->world, veh);
        }
        if (DEBUG) printf("wup read succesfully\n");
    }
}


//handles texture requests and adds vehicle to world
void unknown_veh_handler(struct sockaddr_in* addr, int id, World* world, ClientUpdate cl_up)
{
    int ret, socket_desc, bytes_to_read, bytes_to_send;
    char buffer[1024*1024*5];
    struct sockaddr_in server_addr, client_addr;
    ImagePacket* texture;

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_TEXTURE_HANDLER_PORT);
    client_addr.sin_addr.s_addr = INADDR_ANY;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_TEXTURE_HANDLER_PORT);
    server_addr.sin_addr.s_addr = addr->sin_addr.s_addr;

    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    ret = bind(socket_desc, (struct sockaddr*) &client_addr, sizeof(client_addr));
    ERROR_HELPER(ret, "Error binding texture handler port to socket");

    texture = malloc(sizeof(ImagePacket));
    texture->id = id;
    texture->image = NULL;
    texture->header.type = 0x2;
    texture->header.size = sizeof(ImagePacket);
    bytes_to_send = Packet_serialize(buffer, (PacketHeader*) texture);

    ret = sendto(socket_desc, &bytes_to_send, HEADER_SIZE, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
    ret = sendto(socket_desc, buffer, bytes_to_send, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));

    if (DEBUG) printf("texture request of veh n. %d sent to server\n", id);

    ret = recvfrom(socket_desc, &bytes_to_read, HEADER_SIZE, MSG_WAITALL, (struct sockaddr*) &server_addr, (socklen_t*) sizeof(&server_addr));
    ret = recvfrom(socket_desc, buffer, bytes_to_read, MSG_WAITALL, (struct sockaddr*) &server_addr, (socklen_t*) sizeof(&server_addr));


    Packet_free( (PacketHeader*) texture);
    texture = (ImagePacket*) Packet_deserialize(buffer, bytes_to_read);
    if (texture->id == id && texture->image != NULL) {

        Vehicle* veh = malloc(sizeof(Vehicle));
        Vehicle_init(veh, world, id, texture->image);
        World_addVehicle(world, veh);
        veh->x = cl_up.x;
        veh->y = cl_up.y;
        veh->theta = cl_up.theta;
    }
    else Packet_free((PacketHeader*) texture);
    if (DEBUG) printf("texture of veh n. %d received succesfully\n", id);
}

void* client_updater_for_server(void* arg)
{
    cl_up_args* args = (cl_up_args*) arg;
    int ret=0, bytes_to_send, bytes_sent, socket_desc;
    struct sockaddr_in server_addr;
    char buffer[1024];
    VehicleUpdatePacket* veh_up  = malloc(sizeof(VehicleUpdatePacket));
    veh_up->id = args->veh->id;

    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER( ret, "Could not create socket to send client updates\n");

    server_addr.sin_addr.s_addr = args->server_addr.sin_addr.s_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CL_UP_RECV_PORT);

    while (1)
    {
        veh_up->rotational_force = args->veh->rotational_force_update;
        veh_up->translational_force = args->veh->translational_force_update;
        veh_up->header.type = 0x7;
        veh_up->header.size = sizeof(veh_up);
        bytes_to_send = Packet_serialize(buffer, (PacketHeader*) veh_up);

        //bytes_sent = sendto(socket_desc, &bytes_to_send, HEADER_SIZE, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
        bytes_sent = sendto(socket_desc, buffer, bytes_to_send, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));

        if (DEBUG) printf("sent client update [%d bytes] packet to server\n", bytes_sent);
        usleep(50000);
    }
}

void quit_handler()
{
    int ret, socket_desc, bytes_sent, bytes_to_send;
    char buffer[] = {0};
    struct sockaddr_in server_addr;

    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER(socket_desc, "Could not create quit handler socket\n");

    server_addr.sin_addr.s_addr = inet_addr(global_server_addr);
    server_addr.sin_port = htons(CL_UP_RECV_PORT);
    server_addr.sin_family = AF_INET;

    bytes_sent = 0;
    bytes_to_send = sizeof(buffer);
    while (bytes_sent < bytes_to_send)
    {
      ret = sendto(socket_desc, buffer, bytes_to_send, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
      ERROR_HELPER(ret, "Could not send quit msg to server!\n");
    }
    if (DEBUG) printf("quit message sent to server, exiting...\n");
    exit(0);

}
