#include <math.h>

#include "p.h"

#define PAWN_RADIUS 0.5

// Determines the relative position of a point and a line described by a vector.
// Returns 1 if the point is on the right side, -1 if the point is on the left side and 0 if the point is on the line.
static inline int point_side(struct position p, struct position v0, struct position v1)
{
	float value = (v1.y - v0.y) * p.x + (v0.x - v1.x) * p.y + v1.x * v0.y - v0.x * v1.y;
	if (value > 0) return 1;
	else if (value < 0) return -1;
	else return 0;
}

// The wall doesn't block paths right next to it ("touching" the wall is allowed).
int wall_blocks(struct position start, struct position end, float left, float right, float top, float bottom) // TODO rename
{
	// http://stackoverflow.com/a/293052/515212

	int side;
	int sides[3] = {0};

	// Determine relative position of the right-top corner of the wall.
	side = point_side((struct position){right, top}, start, end);
	sides[side + 1] = 1;

	// Determine relative position of the left-top corner of the wall.
	side = point_side((struct position){left, top}, start, end);
	sides[side + 1] = 1;

	// Determine relative position of the left-bottom corner of the wall.
	side = point_side((struct position){left, bottom}, start, end);
	sides[side + 1] = 1;

	// Determine relative position of the left-top corner of the wall.
	side = point_side((struct position){right, bottom}, start, end);
	sides[side + 1] = 1;

	if (sides[0] && sides[2]) // if there are corners on both sides of the path
	{
		if ((start.x >= right) && (end.x >= right)) return 0;
		if ((start.x <= left) && (end.x <= left)) return 0;
		if ((start.y >= top) && (end.y >= top)) return 0;
		if ((start.y <= bottom) && (end.y <= bottom)) return 0;

		return 1;
	}

	return 0;
}

// WARNING: The path must be a non-zero vector (end != start).
int pawn_blocks(struct position start, struct position end, struct position pawn) // TODO rename
{
	// http://stackoverflow.com/a/1084899/515212

	const double radius = PAWN_RADIUS * 2;

	// Change the coordinate system so that the path starts from (0, 0).
	end.x -= start.x;
	end.y -= start.y;
	pawn.x -= start.x;
	pawn.y -= start.y;

	double a = end.x * end.x + end.y * end.y;
	double b = -2 * (pawn.x * end.x + pawn.y * end.y);
	double c = pawn.x * pawn.x + pawn.y * pawn.y - radius * radius;
	double discriminant = b * b - 4 * a * c;

	if (discriminant < 0) return 0;

	discriminant = sqrt(discriminant);
	double solution0 = (- b - discriminant) / (2 * a);
	double solution1 = (- b + discriminant) / (2 * a);

	if ((solution0 >= 0) && (solution0 <= 1)) return 1;
	if ((solution1 >= 0) && (solution1 <= 1)) return 1;

	return 0;
}
