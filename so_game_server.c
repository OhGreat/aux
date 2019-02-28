// #include <GL/glut.h> // not needed here

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
#include <arpa/inet.h>
#include <signal.h>

#include "server_common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "error_helper.h"
#include "server_connection_handler.c"
#include "so_game_protocol.h"




sem_t* wup_sem;
int halting_flag = 0;
int quit_socket;


int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
    exit(-1);
  }
  char* elevation_filename=argv[1];
  char* texture_filename=argv[2];
  char* vehicle_texture_filename="./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }


  printf("loading texture image from %s ... ", texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading vehicle (default) texture from %s ... ", vehicle_texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  //creating world
  World* world = malloc(sizeof(World));
  World_init(world, surface_elevation, surface_texture, 0.5, 0.5, 0.5);


  int ret;
  sem_cleanup();
  //creating map struct to share with clients
  Map* map_info = malloc(sizeof(Map));
  map_info->map_texture = surface_texture;
  map_info->map_elevation = surface_elevation;


  //creating worldupdatepacket and client_list to share with clients
  ListHead* client_list = malloc(sizeof(ListHead));
  List_init(client_list);

  WorldUpdatePacket* wup = malloc(sizeof(WorldUpdatePacket));
  wup->header.type = 0x6;
  wup->header.size = sizeof(WorldUpdatePacket);
  wup->num_vehicles = 0;
  //creating semaphore to handle world update packet
  wup_sem = sem_open(WUP_SEM, O_CREAT | O_EXCL, 0600, 1);
  if (wup_sem == SEM_FAILED && errno == EEXIST) {
      sem_unlink(WUP_SEM);
      wup_sem = sem_open(WUP_SEM, O_CREAT | O_EXCL, 0600, 1);
  }
  if (wup_sem == SEM_FAILED) {
      printf("[FATAL ERROR] could not open wup_sem, the reason is: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
  }

  signal(SIGINT, quit_handler);

  //creating socket and thread to send wup to clients **********************************
  int wup_sender_desc;
  wup_sender_desc = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(wup_sender_desc, "Could not create socket to send wup \n");
  int broadcastPermission = 1;
  if (setsockopt(wup_sender_desc, SOL_SOCKET, SO_BROADCAST, (void*) &broadcastPermission, sizeof(broadcastPermission)) < 0)
    ERROR_HELPER(-1, "Failed setting broadcast permission to wup sender\n");
    quit_socket = wup_sender_desc;


  pthread_t wup_sender_thread;
  wup_sender_args* wup_thread_args = malloc(sizeof(wup_sender_args));
  wup_thread_args->client_list = client_list;
  wup_thread_args->wup = wup;
  wup_thread_args->wup_sender_desc = wup_sender_desc;
  ret = pthread_create(&wup_sender_thread, NULL, wup_sender, (void *) wup_thread_args);
  PTHREAD_ERROR_HELPER(ret, "Could not create wup sender thread\n");
  ret = pthread_detach(wup_sender_thread);
  PTHREAD_ERROR_HELPER(ret, "Could not detach wup sender thread");
  if (DEBUG) printf("Thread spawned to periodically send wup to clients\n" );
  //**************************************************************************

  //creating client update receiver socket************************************
  int cl_up_recv_sock;
  struct sockaddr_in server_cl_up_recv_addr;
  struct sockaddr_in* cl_up_client_addr = malloc(sizeof(struct sockaddr_in));

  server_cl_up_recv_addr.sin_family = AF_INET;
  server_cl_up_recv_addr.sin_port = htons(CL_UP_RECV_PORT);
  server_cl_up_recv_addr.sin_addr.s_addr = INADDR_ANY;

  cl_up_recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(cl_up_recv_sock, "Error creating vehicle update receiver socket\n");

  ret = bind(cl_up_recv_sock, (struct sockaddr*) &server_cl_up_recv_addr, sizeof(server_cl_up_recv_addr));
  ERROR_HELPER(ret, "Error binding address to socket in server vehicle update\n");
  //**************************************************************************

  //thread to handle client updates*******************************************
  cl_up_args* cl_args = malloc(sizeof(cl_up_args));
  cl_args->wup = wup;
  cl_args->client_list = client_list;
  cl_args->world = world;
  cl_args->cl_up_socket = cl_up_recv_sock;
  pthread_t cl_thread;
  ret = pthread_create(&cl_thread, NULL, cl_up_handler, (void*) cl_args);
  PTHREAD_ERROR_HELPER(ret, "Could not create cl_up handler thread\n");
  ret = pthread_detach(cl_thread);
  PTHREAD_ERROR_HELPER(ret, "Could not detach cl_up handler thread");
  if (DEBUG) printf("Spawned thread to handle cl_up from clients\n");
/*
  //creating texture handler socket and thread********************************
  int texture_handler_socket;
  struct sockaddr_in texture_addr;
  texture_addr.sin_family = AF_INET;
  texture_addr.sin_port = htons(SERVER_TEXTURE_HANDLER_PORT);
  texture_addr.sin_addr.s_addr = INADDR_ANY;

  texture_handler_socket = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(texture_handler_socket, "Error creating texture handler socket\n");

  ret = bind(texture_handler_socket, (struct sockaddr*) &texture_addr, sizeof(texture_addr));
  ERROR_HELPER(ret, "Error binding address to texture handler socket\n");
*/
  // creating tcp_socket and accepting new connections to be handled by a thread
  int tcp_socket_desc, tcp_client_desc;
  struct sockaddr_in tcp_server_addr = {0};
  int sockaddr_len = sizeof(struct sockaddr_in);
  struct sockaddr_in* tcp_client_addr = malloc(sizeof(struct sockaddr_in));

  tcp_socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(tcp_socket_desc,"Cannot initialize socket");

  tcp_server_addr.sin_addr.s_addr   = INADDR_ANY;
  tcp_server_addr.sin_family        = AF_INET;
  tcp_server_addr.sin_port          = htons(SERVER_TCP_PORT);

  /* enabling SO_REUSEADDR to quickly restart server after crash */
  int reuseaddr_opt = 1;
  ret = setsockopt(tcp_socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
  ERROR_HELPER(ret, "Cannot set SO_REUSEADDR option");

  ret = bind(tcp_socket_desc, (struct sockaddr*) &tcp_server_addr, sockaddr_len);
  ERROR_HELPER(ret, "Cannot bind address to socket");

  ret = listen(tcp_socket_desc, MAX_CLIENTS);
  ERROR_HELPER(ret, "Cannot listen on socket");

  if (DEBUG) printf("Waiting for incoming connections..\n");

  //creating thread to handle incoming client connections
  while (halting_flag == 0) {

      tcp_client_desc = accept(tcp_socket_desc, (struct sockaddr* ) tcp_client_addr, (socklen_t *) &sockaddr_len);
      ERROR_HELPER(tcp_client_desc, "Cannot open socket for incoming connection");

      if (DEBUG) fprintf(stderr, "Accepted incoming connection succesfully...\n");

      //creating udp client addr to pass to client thread
      cl_up_client_addr->sin_family = AF_INET;
      cl_up_client_addr->sin_addr.s_addr = tcp_server_addr.sin_addr.s_addr;

      pthread_t client_handler_thread;
      connection_handler_args *thread_args = malloc(sizeof(connection_handler_args));
      thread_args->tcp_socket_desc = tcp_client_desc;
      thread_args->client_addr = tcp_client_addr;
      thread_args->cl_up_recv_sock = cl_up_recv_sock;
      thread_args->server_cl_up_recv_addr = &server_cl_up_recv_addr;
      //thread_args->texture_handler_socket = texture_handler_socket;
      thread_args->cl_up_client_addr = cl_up_client_addr;
      thread_args->world = world;
      thread_args->map_info = map_info;
      thread_args->wup = wup;
      thread_args->client_list = client_list;

      ret = pthread_create(&client_handler_thread, NULL, server_connection_handler, (void *)thread_args);
      PTHREAD_ERROR_HELPER(ret, "Could not create a new thread\n");

      ret = pthread_detach(client_handler_thread);
      PTHREAD_ERROR_HELPER(ret, "Could not detach thread");

      if (DEBUG) printf("New thread created to handle client connection\n");

      tcp_client_addr = calloc(1, sizeof(struct sockaddr_in));
      cl_up_client_addr = calloc(1, sizeof(struct sockaddr_in));
  }

  sem_cleanup();

  return 0;
}

//function to send wup to clients***********************************************
void* wup_sender(void* arg) {
    int ret = 0;
    int bytes_to_send, socket_desc;
    struct sockaddr_in client_addr = {0};
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_WUP_RECEIVER_PORT);
    client_addr.sin_addr.s_addr = inet_addr(CLIENT_BROADCAST_ADDR);

    char buffer[1024*1024*5];
    wup_sender_args* args = (wup_sender_args*) arg;
    socket_desc = args->wup_sender_desc;

    ListHead* client_list = args->client_list;
    int n_clients;

/*
    wup_sender_desc = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER(wup_sender_thread, "Could not create socket to send wup \n");
    int broadcastPermission = 1;
    if (setsockopt(wup_sender_thread, SOL_SOCKET, SO_BROADCAST, (void*) &broadcastPermission, sizeof(broadcastPermission)) < 0)
      ERROR_HELPER(-1, "Failed setting broadcast permission to wup sender\n");
*/
    while (halting_flag==0) {

        sem_t* wup_sem = sem_open(WUP_SEM, 0);
        ERROR_HELPER(ret, "Cannot open wup semaphore to send wup\n");
        ret = sem_wait(wup_sem);
        ERROR_HELPER(ret, "Cannot wait wup sem\n");

        n_clients = client_list->size;
        printf("n connected clients:%d\n",n_clients);
        bytes_to_send = Packet_serialize(buffer, (PacketHeader*) args->wup);


        ret = sendto(socket_desc, &bytes_to_send, HEADER_SIZE, 0, (struct sockaddr*) &client_addr, sizeof(client_addr));
        ret = sendto(socket_desc, buffer, bytes_to_send, 0, (struct sockaddr*) &client_addr, sizeof(client_addr));

        if (DEBUG) printf("wup sent to all connected clients\n");

          ret = sem_post(wup_sem);
          ERROR_HELPER(ret, "Cannot post wup sem\n");
          ret = sem_close(wup_sem);
          ERROR_HELPER(ret, "Cannot close wup sem\n");

          ret = usleep(1000);
          if (ret == -1 && errno == EINTR) printf("error sleeping thread with usleep\n");
          if (DEBUG) printf("successfully sent wup to all clients\n");
    }
    return 0;
}



void* cl_up_handler (void* arg) {
  int ret;
  char buffer[1024*1024*5];
  cl_up_args* args = (cl_up_args*) arg;
  Vehicle* veh;
  WorldUpdatePacket* wup = args->wup;
  ClientUpdate* cl_up ;

  sem_t* sem = sem_open(WUP_SEM, 0);


  //per sapere la grandezza del packet...
  VehicleUpdatePacket* vup = malloc( sizeof(VehicleUpdatePacket));
  vup->header.size = sizeof(VehicleUpdatePacket);
  vup->header.type = 0x7;
  int bytes_to_read = Packet_serialize(buffer, (PacketHeader*) vup);
  Packet_free((PacketHeader*) vup);


  //sem_t* sem;
  while(halting_flag == 0) {
      ret = recv(args->cl_up_socket, buffer, bytes_to_read, MSG_WAITALL);
      ERROR_HELPER(ret, "Error reciving cl_up\n");

      vup = (VehicleUpdatePacket*) Packet_deserialize( buffer, bytes_to_read);
      printf("Initializing procss of cl_up packet of veh: %d\n", vup->id);
      //find client and update its vehicles
      int i = 0;
      cl_up = &(wup->updates[i]);
      while(i<wup->num_vehicles && cl_up->id != vup->id){
        cl_up = &(wup->updates[i]);
        i++;
      }
      if (cl_up->id == vup->id) {

          veh = World_getVehicle(args->world, vup->id);
          veh->translational_force_update = vup->translational_force;
          veh->rotational_force_update = vup->rotational_force;
          Vehicle_update(veh, args->world->dt);

            printf("    waiting to acquire sem resource (cl_up)\n");
            ret = sem_wait(sem);
            ERROR_HELPER(ret, "Cannot wait wup semaphore\n");
            printf("    acquired wup sem \n");
            cl_up->x = veh->x;
            cl_up->y = veh->y;
            cl_up->theta = veh->theta;
            ret = sem_post(sem);
            ERROR_HELPER(ret, "Could not post wup sem\n");
            if (DEBUG) printf("Updated vehicle: %d on x:%f, y: %f\n", cl_up->id, cl_up->x, cl_up->y);
          }

      else {
        printf("how did we end up here?\n");
      }
    }
    ret = close(args->cl_up_socket);
    ERROR_HELPER(ret,"Error closing cl_up_socket!\n");
    if (DEBUG) printf("closed cl_up socket\n");
    return 0;
  }


void quit_handler(int sig) {
  halting_flag = 1;
  usleep(50000);
  int ret=0, bytes_sent = 0;
  int val= 0;

  struct sockaddr_in client_addr = {0};
  client_addr.sin_family = AF_INET;
  client_addr.sin_port = htons(CLIENT_WUP_RECEIVER_PORT);
  client_addr.sin_addr.s_addr = inet_addr(CLIENT_BROADCAST_ADDR);

  if (DEBUG) printf("Server is closing, disconnecting all clients...\n");
  sem_t* wup_sem = sem_open(WUP_SEM, 0);
  ERROR_HELPER(ret, "Cannot open wup semaphore to send wup\n");
  ret = sem_wait(wup_sem);
  ERROR_HELPER(ret, "Cannot wait wup sem\n");

  bytes_sent = sendto(quit_socket, &val, sizeof(val), 0, (struct sockaddr*) &client_addr, sizeof(client_addr));
  ERROR_HELPER(bytes_sent, "Could not send quit msg to clients!\n");

  ret = sem_post(wup_sem);
  ERROR_HELPER(ret, "Cannot post wup sem\n");
  ret = sem_close(wup_sem);
  ERROR_HELPER(ret, "Cannot close wup sem\n");

  if (DEBUG) printf("Succesfully sent quit message to all clients.\n");
  ret = close(quit_socket);
  ERROR_HELPER(ret, "Closed wup_sender_socket succesfully\nGoodbye.\n");
  exit(0);

}


//sem cleanup func
void sem_cleanup(void) {
    sem_close(wup_sem);
    sem_unlink(WUP_SEM);
}
