#include <sys/time.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/glext.h>

#include "format.h"
#include "map.h"
#include "pathfinding.h"
#include "interface.h"
#include "display_common.h"
#include "image.h"
#include "input_map.h"
#include "display_battle.h"

#define S(s) (s), sizeof(s) - 1

#define RESOURCE_GOLD 660
#define RESOURCE_FOOD 680
#define RESOURCE_WOOD 700
#define RESOURCE_IRON 720
#define RESOURCE_STONE 740

//#define HEALTH_BAR 64
#define TROOPS_BAR_MARGIN 2

#define TROOPS_BAR_WIDTH 5
#define TROOPS_BAR_HEIGHT 48

// TODO compatibility with OpenGL 2.1 (necessary in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

#define ARROW_LENGTH 60

static GLuint map_framebuffer;

static GLuint map_renderbuffer;

static uint8_t *format_sint(uint8_t *buffer, int64_t number)
{
	if (number > 0) *buffer++ = '+';
	return format_int(buffer, number, 10);
}

void if_storage_init(const struct game *game, int width, int height)
{
	size_t i;

	unsigned regions_count = game->regions_count;
	// assert(game->regions_count < 65536);

	glGenRenderbuffers(1, &map_renderbuffer);
	glGenFramebuffers(1, &map_framebuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, map_renderbuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, map_renderbuffer);

	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, 0, 1);

	// TODO why is this necessary
	//glBindRenderbuffer(GL_RENDERBUFFER, 0);
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);
	//glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glClear(GL_COLOR_BUFFER_BIT);

	for(i = 0; i < regions_count; ++i)
	{
		glColor3ub(255, i / 256, i % 256);
		fill_polygon(game->regions[i].location, 0, 0);
	}

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int if_storage_get(unsigned x, unsigned y)
{
	GLubyte pixel[3];

	glBindFramebuffer(GL_READ_FRAMEBUFFER, map_framebuffer);
	glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	if (!pixel[0]) return -1;
	return pixel[1] * 256 + pixel[2];
}

void if_storage_term(void)
{
	glDeleteRenderbuffers(1, &map_renderbuffer);
	glDeleteFramebuffers(1, &map_framebuffer);
}

static void show_progress(unsigned current, unsigned total, unsigned x, unsigned y, unsigned width, unsigned height)
{
	// Progress is visualized as a sector with length proportional to the remaining time.

	if (current)
	{
		double progress = (double)current / total;
		double angle = progress * 2 * M_PI;

		glColor4ubv(display_colors[Progress]);

		glBegin(GL_POLYGON);

		glVertex2f(x + width / 2, y + height / 2);
		switch ((unsigned)(progress * 8))
		{
		case 0:
		case 7:
			glVertex2f(x + width, y + (1 - sin(angle)) * height / 2);
			break;

		case 1:
		case 2:
			glVertex2f(x + (1 + cos(angle)) * width / 2, y);
			break;

		case 3:
		case 4:
			glVertex2f(x, y + (1 - sin(angle)) * height / 2);
			break;

		case 5:
		case 6:
			glVertex2f(x + (1 + cos(angle)) * width / 2, y + height);
			break;
		}
		switch ((unsigned)(progress * 8))
		{
		case 0:
			glVertex2f(x + width, y);
		case 1:
		case 2:
			glVertex2f(x, y);
		case 3:
		case 4:
			glVertex2f(x, y + height);
		case 5:
		case 6:
			glVertex2f(x + width, y + height);
		}
		glVertex2f(x + width, y + height / 2);

		glEnd();
	}
	else fill_rectangle(x, y, width, height, Progress);
}

static void show_resource(const struct image *restrict image, int treasury, int income, int expense, unsigned y)
{
	unsigned x;
	char buffer[32], *end; // TODO make sure this is enough

	image_draw(image, PANEL_X, y);
	end = format_uint(buffer, treasury, 10);

	x = PANEL_X + image->width;
	x += draw_string(buffer, end - buffer, x, y, &font12, Black);
	if (income)
	{
		end = format_sint(buffer, income);
		x += draw_string(buffer, end - buffer, x, y, &font12, Ally);
	}
	if (expense)
	{
		end = format_sint(buffer, -expense);
		x += draw_string(buffer, end - buffer, x, y, &font12, Enemy);
	}
}

static void tooltip_cost(const char *restrict name, size_t name_length, const struct resources *restrict cost, unsigned time)
{
	char buffer[16];
	size_t length;

	unsigned offset = 120;

	draw_string(name, name_length, TOOLTIP_X, TOOLTIP_Y, &font12, White);

	if (cost->gold)
	{
		image_draw(&image_gold, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->gold, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->food)
	{
		image_draw(&image_food, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->food, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->wood)
	{
		image_draw(&image_wood, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->wood, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->stone)
	{
		image_draw(&image_stone, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->stone, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->iron)
	{
		image_draw(&image_iron, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->iron, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}

	if (time)
	{
		image_draw(&image_time, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, time, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
}

static void if_map_troops(unsigned x, unsigned y, unsigned count_self, unsigned count_allies, unsigned count_enemies)
{
	unsigned count;

	// Calculate the height of the troops bar.
	count_self = (count_self + 5) / 10;
	count_allies = (count_allies + 5) / 10;
	count_enemies = (count_enemies + 5) / 10;
	count = count_self + count_allies + count_enemies;

	if (count)
	{
		// TODO indicate this overflow somehow
		if (count > image_map_village.height) count = image_map_village.height;

		if (count_self)
			fill_rectangle(x, y - count_self, TROOPS_BAR_WIDTH, count_self, Self);
		if (count_allies)
			fill_rectangle(x, y - count_self - count_allies, TROOPS_BAR_WIDTH, count_allies, Ally);
		if (count_enemies)
			fill_rectangle(x, y - count, TROOPS_BAR_WIDTH, count_enemies, Enemy);
		draw_rectangle(x - 1, y - count - 1, TROOPS_BAR_WIDTH + 2, count + 2, Black);
	}
}

static void display_troop_destination(struct point p0, struct point p1)
{
	// Take the points p0 and p1 as endpoints of the diagonal of the square and find the endpoints of the other diagonal.
	// http://stackoverflow.com/questions/27829304/calculate-bisector-segment-coordinates

	int x = p1.x - p0.x;
	int y = p1.y - p0.y;

	double xm = p0.x + x / 2.0;
	double ym = p0.y + y / 2.0;

	double length = sqrt(y * y + x * x);
	double dx = ARROW_LENGTH * y / (2 * length);
	double dy = ARROW_LENGTH * x / (2 * length);

	struct point from = {xm + dx, ym - dy};
	struct point to = {xm - dx, ym + dy};

	display_arrow(from, to, MAP_X, MAP_Y, Self); // TODO change color
}

static void if_map_region(const struct region *region, const struct state_map *state, const struct game *game)
{
	int siege = (region->owner != region->garrison.owner);

	const struct troop *troop;
	size_t i;

	show_flag(PANEL_X, PANEL_Y, region->owner);

	if (allies(game, region->owner, state->player) || allies(game, region->garrison.owner, state->player))
	{
		enum object object;
		size_t offset;
		size_t x;

		unsigned char self_count = 0, other_count = 0;

		// Display building information.
		// Construct button is displayed if the following conditions are satisfied:
		// * current player owns the region
		// * the building is not built
		// * building requirements are satisfied
		// * there is no siege
		for(i = 0; i < buildings_count; ++i)
		{
			struct point position = if_position(Building, i);
			if (region_built(region, i))
			{
				draw_rectangle(position.x - 1, position.y - 1, image_buildings[i].width + 2, image_buildings[i].height + 2, Black);
				image_draw(image_buildings + i, position.x, position.y);
			}
			else if ((state->player == region->owner) && region_building_available(region, buildings[i]) && !siege)
			{
				draw_rectangle(position.x - 1, position.y - 1, image_buildings[i].width + 2, image_buildings[i].height + 2, Black);
				image_draw(image_buildings_gray + i, position.x, position.y);
			}
		}

		// Display troops in the region.
		for(troop = region->troops; troop; troop = troop->_next)
		{
			enum color color_text;
			int moving = 0;

			if (troop->owner == state->player)
			{
				if (troop->move == LOCATION_GARRISON) continue;

				if (!self_count) fill_rectangle(PANEL_X, object_group[TroopSelf].top - 2, PANEL_WIDTH, 2 + object_group[TroopSelf].height + 12 + 2, Self);
				x = self_count++;
				object = TroopSelf;
				offset = state->self_offset;
				color_text = Black;

				moving = (troop->move != region);

				// Display troop destination if necessary.
				if (moving && (!state->troop || (troop == state->troop)))
				{
					struct point p0, p1;
					if (polygons_border(region->location, troop->move->location, &p0, &p1)) // TODO this is slow; don't do it every time
					{
						display_troop_destination(p0, p1);
					}
					else
					{
						// TODO do something better
						write(2, S("Neighboring regions have no common border\n"));
					}
				}
			}
			else if (allies(game, troop->owner, state->player))
			{
				if (troop->location == LOCATION_GARRISON) continue;

				if (!other_count) fill_rectangle(PANEL_X, object_group[TroopOther].top - 2, PANEL_WIDTH, 2 + object_group[TroopOther].height + 12 + 2, Ally);
				x = other_count++;
				object = TroopOther;
				offset = state->other_offset;
				color_text = White;
			}
			else
			{
				if (troop->location == LOCATION_GARRISON) continue;

				if (!other_count) fill_rectangle(PANEL_X, object_group[TroopOther].top - 2, PANEL_WIDTH, 2 + object_group[TroopOther].height + 12 + 2, Enemy);
				x = other_count++;
				object = TroopOther;
				offset = state->other_offset;
				color_text = White;
			}

			// Draw troops that are visible on the screen.
			if ((x >= offset) && (x < offset + TROOPS_VISIBLE))
			{
				struct point position = if_position(object, x - offset);
				display_troop(troop->unit->index, position.x, position.y, Player + troop->owner, color_text, troop->count);
				if (moving) image_draw(&image_movement, position.x, position.y);
				if (troop == state->troop) draw_rectangle(position.x - 1, position.y - 1, object_group[object].width + 2, object_group[object].height + 2, White);
			}
		}

		// Display scroll buttons.
		if (state->self_offset) image_draw(&image_scroll_left, PANEL_X, object_group[TroopSelf].top);
		if ((self_count - state->self_offset) > TROOPS_VISIBLE) image_draw(&image_scroll_right, object_group[TroopSelf].span_x + object_group[TroopSelf].padding, object_group[TroopSelf].top);
		if (state->other_offset) image_draw(&image_scroll_left, PANEL_X, object_group[TroopOther].top);
		if ((other_count - state->other_offset) > TROOPS_VISIBLE) image_draw(&image_scroll_right, object_group[TroopOther].span_x + object_group[TroopOther].padding, object_group[TroopOther].top);

		// Display garrison and garrison troops.
		{
			const struct garrison_info *restrict garrison = garrison_info(region);
			if (garrison)
			{
				image_draw(&image_garrison[garrison->index], GARRISON_X, GARRISON_Y);
				show_flag(GARRISON_X, GARRISON_Y - GARRISON_MARGIN, region->garrison.owner);

				if (allies(game, region->garrison.owner, state->player))
				{
					i = 0;
					for(troop = region->troops; troop; troop = troop->_next)
					{
						if (troop->owner == state->player)
						{
							if (troop->move != LOCATION_GARRISON) continue;
						}
						else if (troop->location != LOCATION_GARRISON) continue;

						struct point position = if_position(TroopGarrison, i);
						display_troop(troop->unit->index, position.x, position.y, Player + troop->owner, Black, troop->count);
						i += 1;
					}
				}
				else
				{
					// Display an estimation of the number of troops in the garrison.

					char buffer[32], *end; // TODO make sure this is enough

					unsigned count = 0;
					for(troop = region->troops; troop; troop = troop->_next)
						if (troop->location == LOCATION_GARRISON)
							count += troop->count;
					count = ((count + 5) / 10) * 10;

					end = format_bytes(buffer, S("about "));
					end = format_uint(end, count, 10);
					end = format_bytes(end, S(" troops"));

					draw_string(buffer, end - buffer, object_group[TroopGarrison].left, object_group[TroopGarrison].top, &font12, Black);
				}

				// If the garrison is under siege, display the remaining provisions.
				if (siege)
				{
					unsigned provisions = garrison->provisions - region->garrison.siege;
					for(i = 0; i < provisions; ++i)
						image_draw(&image_food, object_group[TroopGarrison].right + 9, object_group[TroopGarrison].bottom - (i + 1) * image_food.height);
				}

				// If the current player is doing an assault, display indicator.
				if (region->garrison.assault && (state->player == region->owner))
					image_draw(&image_assault, object_group[TroopGarrison].left, object_group[TroopGarrison].top); // TODO choose a better indicator
			}
		}
	}

	if ((state->player == region->owner) && !siege)
	{
		if (region->construct >= 0)
		{
			struct point position = if_position(Building, region->construct);
			show_progress(region->build_progress, buildings[region->construct].time, position.x, position.y, object_group[Building].width, object_group[Building].height);
			image_draw(&image_construction, position.x, position.y);
		}

		draw_string(S("train:"), PANEL_X + 2, object_group[Dismiss].top + (object_group[Dismiss].height - font12.height) / 2, &font12, Black);

		// Display train queue.
		size_t index;
		for(index = 0; index < TRAIN_QUEUE; ++index)
		{
			struct point position = if_position(Dismiss, index);
			if (region->train[index])
			{
				display_troop(region->train[index]->index, position.x, position.y, Player, 0, 0);
				show_progress((index ? 0 : region->train_progress), region->train[0]->time, position.x, position.y, object_group[Dismiss].width, object_group[Dismiss].height);
			}
			else fill_rectangle(position.x, position.y, object_group[Dismiss].width, object_group[Dismiss].height, Black);
		}

		// Display units available for training.
		for(index = 0; index < UNITS_COUNT; ++index)
		{
			if (!region_unit_available(region, UNITS[index])) continue;

			struct point position = if_position(Train, index);
			display_troop(index, position.x, position.y, Player, 0, 0);
		}
	}

	// Display tooltip for the hovered object.
	switch (state->hover_object)
	{
	case HOVER_UNIT:
		{
			const struct unit *unit = UNITS + state->hover.unit;
			tooltip_cost(unit->name, unit->name_length, &unit->cost, unit->time);
		}
		break;
	case HOVER_BUILDING:
		if (region_built(region, state->hover.building))
		{
			const struct building *building = buildings + state->hover.building;
			const struct resources none = {0};
			tooltip_cost(building->name, building->name_length, &none, 0);
		}
		else
		{
			const struct building *building = buildings + state->hover.building;
			tooltip_cost(building->name, building->name_length, &building->cost, building->time);
		}
		break;
	}
}

void if_map(const void *argument, const struct game *game)
{
	const struct state_map *state = argument;

	size_t i, j;

	// Display current player's color.
	// TODO use darker color in the center
	draw_rectangle(PANEL_X - 4, PANEL_Y - 4, PANEL_WIDTH + 8, PANEL_HEIGHT + 8, Player + state->player);
	draw_rectangle(PANEL_X - 3, PANEL_Y - 3, PANEL_WIDTH + 6, PANEL_HEIGHT + 6, Player + state->player);
	draw_rectangle(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, Player + state->player);

	// Display panel background pattern.
	display_image(&image_panel, PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT);

	// TODO display year and month
	// {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}
	// game->turn / 12;
	// game->turn % 12;

	// TODO remove this color display box
	/*for(i = 0; i < PLAYERS_LIMIT; ++i)
		fill_rectangle(PANEL_X + (i % 4) * 32, PANEL_Y + 300 + (i / 4) * 32, 32, 32, Player + i);*/

	// Map

	struct resources income = {0}, expenses = {0};
	const struct troop *troop;

	// TODO place income logic at a single place (now it's here and in main.c)

	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		// Fill each region with the color of its owner (or the color indicating unexplored).
		if (state->regions_visible[i]) glColor4ubv(display_colors[Player + region->owner]);
		else glColor4ubv(display_colors[Unexplored]);
		fill_polygon(region->location, MAP_X, MAP_Y);

		// Remember income and expenses.
		if (region->owner == state->player) region_income(region, &income);
		for(troop = region->troops; troop; troop = troop->_next)
		{
			if (troop->owner != state->player) continue;
			if (troop->location == LOCATION_GARRISON) continue;

			if (region->owner != region->garrison.owner) // Troops expenses are covered by another region. Double expenses.
			{
				struct resources expense;
				resource_multiply(&expense, &troop->unit->expense, 2);
				resource_add(&expenses, &expense);
			}
			else resource_add(&expenses, &troop->unit->expense);
		}
	}

	// Draw region borders.
	for(i = 0; i < game->regions_count; ++i)
	{
		glColor4ubv(display_colors[Black]);
		glBegin(GL_LINE_STRIP);
		for(j = 0; j < game->regions[i].location->vertices_count; ++j)
			glVertex2f(MAP_X + game->regions[i].location->points[j].x, MAP_Y + game->regions[i].location->points[j].y);
		glEnd();
	}

	for(i = 0; i < game->regions_count; ++i)
	{
		if (!state->regions_visible[i]) continue;

		const struct region *region = game->regions + i;

		unsigned count_self, count_allies, count_enemies;

		// Display garrison if built.
		const struct garrison_info *restrict garrison = garrison_info(region);
		if (garrison)
		{
			const struct image *restrict image = &image_map_garrison[garrison->index];
			unsigned location_x = MAP_X + region->location_garrison.x - image->width / 2;
			unsigned location_y = MAP_Y + region->location_garrison.y - image->height / 2;
			display_image(image, location_x, location_y, image->width, image->height);
			show_flag_small(MAP_X + region->location_garrison.x, location_y - image_flag_small.height + 10, region->garrison.owner);

			if (allies(game, region->owner, state->player) || allies(game, region->garrison.owner, state->player))
			{
				count_self = 0;
				count_allies = 0;
				count_enemies = 0;

				for(troop = region->troops; troop; troop = troop->_next)
				{
					if (troop->owner == state->player)
					{
						if (troop->move == LOCATION_GARRISON)
							count_self += troop->count;
					}
					else if (troop->location == LOCATION_GARRISON)
					{
						if (allies(game, troop->owner, state->player)) count_allies += troop->count;
						else count_enemies += troop->count;
					}
				}

				if_map_troops(location_x + image->width + TROOPS_BAR_MARGIN, location_y + image->height, count_self, count_allies, count_enemies);
			}
		}

		// Display village image.
		// TODO fix this; it requires bigger regions and grass as background
		// unsigned x, y;
		/*x = MAP_X + region->center.x - image_map_village.width;
		y = MAP_Y + region->center.y - image_map_village.height;
		display_image(&image_map_village, x, y, image_map_village.width, image_map_village.height);*/
		// TODO padding between village image and troops bar

		// Display a bar showing the number of troops in the region.
		// Don't include garrison troops.
		count_self = 0;
		count_allies = 0;
		count_enemies = 0;
		for(troop = region->troops; troop; troop = troop->_next)
		{
			if (troop->owner == state->player)
			{
				if (troop->move != LOCATION_GARRISON)
					count_self += troop->count;
			}
			else if (troop->location != LOCATION_GARRISON)
			{
				if (allies(game, troop->owner, state->player)) count_allies += troop->count;
				else count_enemies += troop->count;
			}
		}
		if_map_troops(MAP_X + region->center.x, MAP_Y + region->center.y, count_self, count_allies, count_enemies);
	}

	if (state->region >= 0)
	{
		const struct region *region = game->regions + state->region;

		// Show the name of the selected region.
		draw_string(region->name, region->name_length, PANEL_X + image_flag.width + MARGIN, PANEL_Y + (image_flag.height - font12.height) / 2, &font12, Black);

		if (state->regions_visible[state->region]) if_map_region(region, state, game);
	}

	// treasury
	struct resources *treasury = &game->players[state->player].treasury;
	show_resource(&image_gold, treasury->gold, income.gold, expenses.gold, RESOURCE_GOLD);
	show_resource(&image_food, treasury->food, income.food, expenses.food, RESOURCE_FOOD);
	show_resource(&image_wood, treasury->wood, income.wood, expenses.wood, RESOURCE_WOOD);
	show_resource(&image_stone, treasury->stone, income.stone, expenses.stone, RESOURCE_STONE);
	show_resource(&image_iron, treasury->iron, income.iron, expenses.iron, RESOURCE_IRON);

	show_button(S("Ready"), BUTTON_READY_X, BUTTON_READY_Y);
	show_button(S("Menu"), BUTTON_MENU_X, BUTTON_MENU_Y);
}