#include <stdio.h>

enum
{
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
	LOG_CRITICAL,
};

#define YELLOW(s) "\x1b[33m" s "\x1b[0m"
#define RED(s) "\x1b[31m" s "\x1b[0m"
//#define GREEN(s) "\x1b[32m" s "\x1b[0m"
//#define BLUE(s) "\x1b[34m" s "\x1b[0m"

#define LOG_LEVEL LOG_ERROR

#define LOG_STRING_EXPAND(token) #token
#define LOG_STRING(token) LOG_STRING_EXPAND(token)

// WARNING: format must be string literal
#if (LOG_LEVEL == LOG_DEBUG)
# define LOG_(level, format, ...) do \
	{ \
		if ((level) < LOG_LEVEL) break; \
		switch (level) \
		{ \
		case LOG_DEBUG: \
			fprintf(stderr, __FILE__ ":" LOG_STRING(__LINE__) " " format "\n", __VA_ARGS__); \
			break; \
		case LOG_WARNING: \
			fprintf(stderr, __FILE__ ":" LOG_STRING(__LINE__) " " YELLOW(format) "\n", __VA_ARGS__); \
			break; \
		case LOG_ERROR: \
		case LOG_CRITICAL: \
			fprintf(stderr, __FILE__ ":" LOG_STRING(__LINE__) " " RED(format) "\n", __VA_ARGS__); \
			break; \
		} \
	} while (0)
#else
# define LOG_(level, format, ...) do \
	{ \
		if ((level) < LOG_LEVEL) break; \
		switch (level) \
		{ \
		case LOG_DEBUG: \
			fprintf(stderr, format "\n", __VA_ARGS__); \
			break; \
		case LOG_WARNING: \
			fprintf(stderr, YELLOW(format) "\n", __VA_ARGS__); \
			break; \
		case LOG_ERROR: \
		case LOG_CRITICAL: \
			fprintf(stderr, RED(format) "\n", __VA_ARGS__); \
			break; \
		} \
	} while (0)
#endif

#define LOG(level, ...) LOG_((level), __VA_ARGS__, 0)
