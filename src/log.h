#include <stdio.h>

enum
{
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
	LOG_CRITICAL,
};

#define LOG_LEVEL LOG_ERROR

// TODO maybe show log word at the start (CRITICAL, ERROR, WARNING)

#define LOG_STRING_EXPAND(token) #token
#define LOG_STRING(token) LOG_STRING_EXPAND(token)

// WARNING: format must be string literal
#if (LOG_LEVEL == LOG_DEBUG)
# define LOG(level, format, ...) do \
	{ \
		if ((level) >= LOG_LEVEL) fprintf(stderr, __FILE__ ":" LOG_STRING(__LINE__) " " format, __VA_ARGS__); \
	} while (0)
#else
# define LOG(level, format, ...) do \
	{ \
		if ((level) >= LOG_LEVEL) fprintf(stderr, format, __VA_ARGS__); \
	} while (0)
#endif
