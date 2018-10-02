#pragma once
#include "world.h"
#include "vehicle.h"


typedef enum ViewType {Inside, Outside, Global} ViewType;

typedef struct WorldViewer{
  World* world;
  float zoom;
  float camera_z;
  int window_width, window_height;
  Vehicle* self;
  ViewType view_type;
} WorldViewer;


// call this to start the visualization of the stuff.
// This will block the program, and terminate when pressing esc on the viewport
void WorldViewer_runGlobal(World* world,
			   Vehicle* self,
			   int* argc, char** argv);


//world visualization functions
void WorldViewer_run(WorldViewer* viewer,
		     World* world,
		     Vehicle* self,
		     int* argc, char** argv);

void WorldViewer_init(WorldViewer* viewer,
		      World* w,
		      Vehicle* self);

void WorldViewer_draw(WorldViewer* viewer);

void WorldViewer_destroy(WorldViewer* viewer);

void WorldViewer_reshapeViewport(WorldViewer* viewer, int width, int height);


//element visualization functions
void Surface_destructor(Surface* s);

void Vehicle_destructor(Vehicle* v);

void drawBox(float l, float w, float h);

int Image_toTexture(Image* src);

void Surface_applyTexture(Surface* s, Image* img);

void Surface_draw(Surface* s);

void Vehicle_applyTexture(Vehicle* v);

void Vehicle_draw(Vehicle* v);

//more functions
void keyPressed(unsigned char key, int x, int y);
void specialInput(int key, int x, int y);
void display(void);
void reshape(int width, int height);
void idle(void);



