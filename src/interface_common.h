#define BUTTON_WIDTH 120
#define BUTTON_HEIGHT 20

#define BUTTON_ENTER_X 900
#define BUTTON_ENTER_Y 696

#define BUTTON_CANCEL_X 900
#define BUTTON_CANCEL_Y 720

#define BUTTON_EXIT_X 900
#define BUTTON_EXIT_Y 744

#define BUTTON_READY_X 6
#define BUTTON_READY_Y 620

#define BUTTON_MENU_X 130
#define BUTTON_MENU_Y 620

void show_flag(unsigned x, unsigned y, unsigned player);
void show_flag_small(unsigned x, unsigned y, unsigned player);

void show_button(const unsigned char *label, size_t label_size, unsigned x, unsigned y);
