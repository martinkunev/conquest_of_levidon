enum {ANNEALING_STEPS = 128, ANNEALING_TRIES = 16};

extern const double desire_buildings[];

double unit_importance(const struct unit *restrict unit);
double unit_cost(const struct unit *restrict unit);
double unit_importance_assault(const struct unit *restrict unit, const struct garrison_info *restrict garrison);

int state_wanted(double rate, double rate_new, double temperature);
