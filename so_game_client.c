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
#include <semaphore.h>
#include <assert.h>
#include <fcntl.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"
#include "error_helper.h"
#include "client_common.h"
#include "common.c"


int window, halting_flag = 0;
WorldViewer viewer;
World world;
Vehicle* vehicle;

char* global_server_addr;
int tcp_socket;
sem_t wup_sem;
sem_t cl_sem;

Image* map_elevation;
Image* map_texture;
Image* my_texture;

cl_up_args* cl_args;
wup_receiver_args* thread_args;

int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

  printf("loading texture image from %s ... ", argv[2]);
  my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("rows: %d, cols: %d, channels: %d, data: %ld, row_data: %ld\n", my_texture->rows, my_texture->cols, my_texture->channels, sizeof(my_texture->data), sizeof(my_texture->row_data));
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  char buffer[1024*1024*2];

  int main_socket_desc, bytes_to_send, bytes_sent, bytes_to_read, bytes_read, ret;
  struct sockaddr_in server_addr = {0};
  global_server_addr = argv[1];

  //creating semaphores to handle structures destrction and threads closing*****
  //sem_cleanup();
  ret = sem_init(&wup_sem, 0, 1);
  if (ret == -1 && errno == EEXIST) {
      sem_destroy(&wup_sem);
      ret = sem_init(&wup_sem, 0, 1);
  }
  if (ret == -1) {
      printf("[FATAL ERROR] could not open &wup_sem, the reason is: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }
  ret = sem_init(&cl_sem, 0, 1);
  if (ret == -1 && errno == EEXIST) {
      sem_destroy(&cl_sem);
      ret = sem_init(&cl_sem, 0, 1);
  }
  if (ret == -1) {
      printf("[FATAL ERROR] could not open cl_sem, the reason is: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }

  //main socket for client connection*****************************************
  main_socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(main_socket_desc, "Could not create socket\n");
  int value = 5;
  setsockopt(main_socket_desc,SOL_SOCKET,SO_REUSEADDR, &value, sizeof(int));

  server_addr.sin_addr.s_addr = inet_addr(argv[1]);
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(SERVER_TCP_PORT);

  ret = connect(main_socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
  ERROR_HELPER(ret, "Could not enstamblish connection to server\n");
  if (DEBUG) fprintf(stderr, "Connected to server succesfully\n");

  tcp_socket = main_socket_desc;
  signal(SIGINT, quit_handler);
  signal(SIGSEGV, seg_fault_handler);

  //requesting id to server
  IdPacket *id_packet = malloc(sizeof(IdPacket));
  id_packet->id = -1;
  id_packet->header.type = 0x1;
  id_packet->header.size = sizeof(id_packet);
  bytes_to_send = Packet_serialize( buffer, (PacketHeader*) id_packet);
  bytes_sent = send(main_socket_desc, &bytes_to_send, sizeof(int), 0);
  bytes_sent = send(main_socket_desc, buffer, bytes_to_send, 0);
  ERROR_HELPER(bytes_sent, "Error sending id request\n");
  if (DEBUG) printf("id request sent succesfully\n");
  Packet_free( (PacketHeader*) id_packet);

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
  id_packet = (IdPacket *) Packet_deserialize(buffer, bytes_read);
  int my_id = id_packet->id;
  Packet_free( (PacketHeader*) id_packet);
  if (DEBUG) printf("received id: '%d' from server\n", my_id);

  //sending my vehicle texture to server
  ImagePacket* texture_packet = malloc(sizeof(ImagePacket));
  texture_packet->id = my_id;
  texture_packet->image = my_texture;
  texture_packet->header.type = 0x4;
  texture_packet->header.size = sizeof(texture_packet);
  bytes_to_send = Packet_serialize(buffer, (PacketHeader*) texture_packet);
  ret = send(main_socket_desc, &bytes_to_send, HEADER_SIZE, 0);
  bytes_sent = send(main_socket_desc, buffer, bytes_to_send, 0);
  if (DEBUG) printf("vehicle texture sent succesfully to server, written: %d bytes\n", bytes_to_send);
  free(texture_packet);

  //receiving map texture from server
  bytes_to_read = message_size_getter(main_socket_desc, HEADER_SIZE);
  bytes_read = 0;
  while (bytes_read < bytes_to_read)
  {
      ret = recv(main_socket_desc, buffer+bytes_read, bytes_to_read - bytes_read, MSG_WAITALL);
      if (ret == -1 && errno == EINTR) continue;
     ERROR_HELPER(ret, "Cannot receive map texture from server\n");
     bytes_read += ret;
  }
  ImagePacket* map_texture_packet = (ImagePacket*) Packet_deserialize(buffer, bytes_read);
  if (map_texture_packet->id != 0 || map_texture_packet->header.type != 0x4)
      ERROR_HELPER(-1, "Cannot deserialize map texture from server\n");
  map_texture = map_texture_packet->image;
  free(map_texture_packet);
  if (DEBUG) printf("map texture received succesfully, read: %d bytes\n", bytes_read);

  //receiving map elevation from server
  bytes_to_read = message_size_getter(main_socket_desc, HEADER_SIZE);
  bytes_read = 0;
  while (bytes_read < bytes_to_read)
  {
      ret = recv(main_socket_desc, buffer+bytes_read, bytes_to_read - bytes_read, MSG_WAITALL);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Cannot receive map elevation from server\n");
      bytes_read += ret;
  }
  ImagePacket* map_elevation_packet = (ImagePacket*) Packet_deserialize(buffer, bytes_read);
  if (map_elevation_packet->id != 0 || map_elevation_packet->header.type != 0x4)
      ERROR_HELPER(-1,"Wrong id or packet type received from server\n");
  map_elevation = map_elevation_packet->image;
  free(map_elevation_packet);
  if (DEBUG) printf("elevation map received succesfully, read: %d bytes\n", bytes_read);
  //Packet_free((PacketHeader*)map_elevation_packet);

  //creating world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle = (Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, my_id, my_texture);
  World_addVehicle(&world, vehicle);


  //thread to send cl_up to server
  pthread_t cl_up_thread;
  cl_args = malloc(sizeof(cl_up_args));
  cl_args->veh = vehicle;
  cl_args->server_addr = server_addr;
  ret = pthread_create(&cl_up_thread, NULL, client_updater_for_server, cl_args);
  ERROR_HELPER(ret, "Could not create cl_up sender thread\n");
  ret = pthread_detach(cl_up_thread);
  ERROR_HELPER(ret, "Unable to detach cl_up sender thread\n");
  if (DEBUG) printf("Created thread to send cl_up to server\n");

  //thread to handle wup
  pthread_t wup_receiver_thread;
  thread_args = malloc(sizeof(wup_receiver_args));
  thread_args->vehicles = world.vehicles;
  thread_args->my_id = my_id;
  thread_args->world = &world;
  thread_args->server_addr = &server_addr;
  thread_args->tcp_socket = main_socket_desc;
  thread_args->texture = my_texture;
  ret = pthread_create(&wup_receiver_thread, NULL, wup_receiver, thread_args);
  ERROR_HELPER(ret, "Could not create wup receiver thread\n");
  ret = pthread_detach(wup_receiver_thread);
  ERROR_HELPER(ret, "Unable to detach wup receiver thread\n");
  if (DEBUG) printf("Created thread to receive wup from server\n");

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  //cleanup
  //quit_handler(1);
  World_destroy(&world);
  //exit(0);
  return 0;
}


//handles wup received from server and calls unknown_veh_handler when needed
void* wup_receiver (void* arg)
{
  //usleep(200000); non funziona..
  int ret, socket_desc, bytes_to_read;
  struct sockaddr_in client_addr;
  char buffer[1024*1024];
  wup_receiver_args* args = (wup_receiver_args*) arg;
  int i, j, update_n_veh, world_n_veh;
  WorldUpdatePacket* wup;
  Vehicle* current_veh;
  ClientUpdate cl_up;
  ListItem* list_item;

  socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER( socket_desc, "Error opening wup receiver socket\n");
  int value = 5;
  setsockopt(socket_desc,SOL_SOCKET,SO_REUSEADDR, &value, sizeof(int));

  client_addr.sin_family = AF_INET;
  client_addr.sin_port = htons(CLIENT_WUP_RECEIVER_PORT);
  client_addr.sin_addr.s_addr = INADDR_ANY;

  ret = bind(socket_desc, (struct sockaddr*) &client_addr, sizeof(client_addr));
  ERROR_HELPER( ret, "Error binding wup receiver port to socket\n");

  //signal(SIGINT, quit_handler);
  //signal(SIGSEGV, quit_handler);

  //sem_t* sem = sem_open(WUP_SEM, 0);
  while (!halting_flag)
  {

      if (DEBUG) printf("WUP || waiting to receive next wup\n");
      ret = recv(socket_desc, &bytes_to_read, HEADER_SIZE, MSG_WAITALL);
      ERROR_HELPER(ret, "Error receiving wup size\n");

      if (bytes_to_read == 0) {
        halting_flag = 1;
        if (DEBUG) printf("Connection lost on server side. halting flag: %d wup receiver is closing\n", halting_flag);
        ret = close(socket_desc);
        ERROR_HELPER(ret, "Error closing wup socket desc\n");
        quit_handler_for_main();
        return 0;
      }
      if (DEBUG) printf("WUP || size of wup received: %d\n", bytes_to_read);
      ret = recv(socket_desc, buffer, bytes_to_read, MSG_WAITALL);
      printf("WUP || wup bytes read = %d\n", ret);
      wup = (WorldUpdatePacket*) Packet_deserialize( buffer, bytes_to_read);
      update_n_veh = wup->num_vehicles;
      if (DEBUG) printf("n of clients in update = %d\n", update_n_veh);

      ret = sem_wait(&wup_sem);
      ERROR_HELPER(ret, "Cannot wait wup semaphore\n");
      for (i=0; i<update_n_veh; i++)
      {
        current_veh = World_getVehicle(args->world, wup->updates[i].id);
        if (current_veh == 0)
        {
          if (DEBUG) printf("Starting acquisition of player: %d texture\n", wup->updates[i].id);
          unknown_veh_handler(args->tcp_socket, wup->updates[i].id, args->world);
        }
      }

      world_n_veh = args->world->vehicles.size;
      list_item = args->vehicles.first;
      for (i=0; i < world_n_veh; i++)
      {
        current_veh = (Vehicle*) list_item;
        for (j=0; j<update_n_veh; j++)
        {
          cl_up = wup->updates[j];
          if (current_veh->id == cl_up.id)
          {
            current_veh->x = cl_up.x;
            current_veh->y = cl_up.y;
            current_veh->theta = cl_up.theta;
            if (DEBUG) printf("Updated veh n: %d . . . . . . . . . . .\n", current_veh->id);
            break;
          }
        }
        if (j == update_n_veh)
        {
          World_detachVehicle(args->world, current_veh);
          if (DEBUG) printf("veh n: %d has disconnected.\n", current_veh->id);
          break;
        }
        list_item = list_item->next;
      }
      Packet_free((PacketHeader*)wup);
      ret = sem_post(&wup_sem);
      ERROR_HELPER(ret, "Could not post wup sem\n");
      //Packet_free((PacketHeader*) wup);
      if (DEBUG) printf("WUP || wup read succesfully\n");
  }
  ret = sem_destroy(&wup_sem);
  ret = close(socket_desc);
  ERROR_HELPER(ret, "Error closing wup socket desc\n");
  if (DEBUG) printf("halting flag: %d wup receiver is closing\n", halting_flag);
  return 0;
}


//handles texture requests and adds vehicle to world
void unknown_veh_handler(int socket_desc, int id, World* world)
{
    int ret, bytes_to_send;
    unsigned long int bytes_to_read;
    char buffer[1024*1024];

    ImagePacket* texture = malloc(sizeof(ImagePacket));
    texture->id = id;
    texture->image = NULL;
    texture->header.type = 0x2;
    texture->header.size = sizeof(texture);
    bytes_to_send = Packet_serialize(buffer, (PacketHeader*) texture);



    ret = send(socket_desc, buffer, bytes_to_send, 0);
    ERROR_HELPER(ret, "Problem with ret in texture receiver 1 \n");
    if (DEBUG) printf("texture request of veh n. %d sent to server\n", id);

    ret = recv(socket_desc, &bytes_to_read, HEADER_SIZE, MSG_WAITALL);
    ERROR_HELPER(ret, "Problem with ret in texture receiver 2 \n");
    ret = recv(socket_desc, buffer, bytes_to_read, MSG_WAITALL);
    ERROR_HELPER(ret, "Problem with ret in texture receiver 3\n");
    Packet_free((PacketHeader*) texture);
    texture = (ImagePacket*) Packet_deserialize(buffer, bytes_to_read);

    if (texture->id == id && texture->image != NULL) {

        //Vehicle* veh = malloc(sizeof(Vehicle));
        //Vehicle_init(veh, world, id, texture->image);
        //World_addVehicle(world, veh);
        if (DEBUG) printf("texture of veh n. %d received succesfully: %ld bytes\n", id,bytes_to_read);
        Vehicle* veh = malloc(sizeof(Vehicle));
        Vehicle_init(veh, world, id, texture->image);
        World_addVehicle(world, veh);
        free(texture);
        if (DEBUG) printf("player: %d added succesfully to game\n", id);
    }
    else {
      Packet_free((PacketHeader*) texture);
      if (DEBUG) printf("texture of veh n: %d is NULL... aborted\n", id);
    }
}


void* client_updater_for_server(void* arg)
{
    //usleep(10000);
    cl_up_args* args = (cl_up_args*) arg;
    int ret=0, bytes_to_send, bytes_sent, socket_desc;
    struct sockaddr_in server_addr;
    char buffer[1024];
    VehicleUpdatePacket* veh_up  = malloc(sizeof(VehicleUpdatePacket));
    ret = sem_wait(&cl_sem);
    veh_up->id = args->veh->id;
    ret = sem_post(&cl_sem);

    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER( ret, "Could not create socket to send client updates\n");

    server_addr.sin_addr.s_addr = args->server_addr.sin_addr.s_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CL_UP_RECV_PORT);

    while (!halting_flag)
    {
        ret = sem_wait(&cl_sem);
        veh_up->rotational_force = args->veh->rotational_force_update;
        veh_up->translational_force = args->veh->translational_force_update;

        veh_up->header.type = 0x7;
        veh_up->header.size = sizeof(veh_up);
        bytes_to_send = Packet_serialize(buffer, (PacketHeader*) veh_up);

        //bytes_sent = sendto(socket_desc, &bytes_to_send, HEADER_SIZE, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
        bytes_sent = sendto(socket_desc, buffer, bytes_to_send, 0, (struct sockaddr*) &server_addr, sizeof(server_addr));
        ret = sem_post(&cl_sem);

        if (DEBUG) printf("sent client update [%d bytes] packet to server\n", bytes_sent);

        usleep(30000);
    }
    if (DEBUG) printf("halting flag: %d cl_up sender thread is closing\n", halting_flag);
    Packet_free((PacketHeader*) veh_up);
    ret = close(socket_desc);
    ERROR_HELPER(ret, "Error closing cl_up socket desc\n");
    return 0;
}


void quit_handler(int sig)
{
    halting_flag = 1;
    usleep(500000);
    int ret, bytes_to_send;
    char* buffer = "quit";
    bytes_to_send = sizeof(buffer);


    ret = sem_wait(&wup_sem);
    ERROR_HELPER(ret, "Cannot wait wup semaphore\n");
    ret = sem_wait(&cl_sem);
    ERROR_HELPER(ret, "Cannot wait wup semaphore\n");

    ret = send(tcp_socket, buffer, bytes_to_send, 0);
    ERROR_HELPER(ret, "Could not send quit msg to server!\n");

    if (DEBUG) printf("quit message: (%d bytes) sent to server, exiting...\n", bytes_to_send);
    ret = close(tcp_socket);
    ERROR_HELPER(ret, "quit handler failed closing tcp socket\n");



    World_destroy(&world);
    Image_free( map_elevation);
    Image_free( map_texture);
    Image_free( my_texture);


    //ret = sem_post(&wup_sem);
    //ERROR_HELPER(ret, "Could not post wup sem\n");
    ret = sem_destroy(&wup_sem);
    ERROR_HELPER(ret, "Cannot close wup sem\n");
    //ret = sem_post(&cl_sem);
    //ERROR_HELPER(ret, "Could not post wup sem\n");
    if (DEBUG) printf("Closing game, bye\n");
    ret = sem_destroy(&cl_sem);
    ERROR_HELPER(ret, "Cannot close wup sem\n");
    exit(0);
}

void quit_handler_for_main()
{
    halting_flag = 1;
    usleep(500000);
    int ret;

    //sem_t* sem = sem_open(WUP_SEM, 0);
    ret = sem_wait(&wup_sem);
    ERROR_HELPER(ret, "Cannot wait wup semaphore\n");
    ret = sem_wait(&cl_sem);
    ERROR_HELPER(ret, "Cannot wait wup semaphore\n");

    ret = close(tcp_socket);
    ERROR_HELPER(ret, "quit handler failed closing tcp socket\n");

    World_destroy(&world);
    if (DEBUG) printf("Closing game, bye\n");

    Image_free( map_elevation);
    Image_free( map_texture);
    Image_free( my_texture);


    //ret = sem_post(&wup_sem);
    //ERROR_HELPER(ret, "Could not post wup sem\n");
    ret = sem_destroy(&wup_sem);
    ERROR_HELPER(ret, "Cannot close wup sem\n");
    //ret = sem_post(&cl_sem);
    //ERROR_HELPER(ret, "Could not post wup sem\n");
    ret = sem_destroy(&cl_sem);
    ERROR_HELPER(ret, "Cannot close wup sem\n");

    exit(0);
}

void seg_fault_handler(int sig)
{
  exit(0);
}

//sem cleanup func
void sem_cleanup(void) {
    sem_close(&cl_sem);
    //sem_unlink(CL_SEM);
    sem_close(&wup_sem);
    //sem_unlink(WUP_SEM);
}
