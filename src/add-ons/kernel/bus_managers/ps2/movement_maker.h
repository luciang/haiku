#ifndef MOVEMENT_MAKER_H
#define MOVEMENT_MAKER_H

#include <OS.h>


#define HISTORY_SIZE 1

float floorf(float x);
float ceilf(float x);
float sqrtf(float x);
int32 make_small(float value);

typedef struct {
	int32			xDelta;
	int32			yDelta;

	int8			acceleration;
	int8			speed;

	float			scrolling_x;
	float			scrolling_y;
	int32			scrolling_xStep;
	int32			scrolling_yStep;
	int32			scroll_acceleration;

	uint8			n_points;
	float			historyX[HISTORY_SIZE];
	float			historyY[HISTORY_SIZE];
} movement_maker;



void get_raw_movement(movement_maker *move, uint32 posX, uint32 posY);
void compute_acceleration(movement_maker *move, int8 accel_factor);
void get_movement(movement_maker *move, uint32 posX, uint32 posY);
void get_scrolling(movement_maker *move, uint32 posX, uint32 posY);
void start_new_movment(movement_maker *move);


#endif
