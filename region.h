// TODO required is refered to twice
#define region_order_available(region, required) (((region)->built & (required)) == (required))
