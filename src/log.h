/*
 * Conquest of Levidon
 * Copyright (C) 2016  Martin Kunev <martinkunev@gmail.com>
 *
 * This file is part of Conquest of Levidon.
 *
 * Conquest of Levidon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 3 of the License.
 *
 * Conquest of Levidon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Conquest of Levidon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

enum
{
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
	LOG_CRITICAL,
};

#define LOG_YELLOW "\x1b[33m"
#define LOG_RED "\x1b[31m"
#define LOG_RESET "\x1b[0m" /* TODO rename this */
//#define GREEN(s) "\x1b[32m" s "\x1b[0m"
//#define BLUE(s) "\x1b[34m" s "\x1b[0m"

#define LOG_LEVEL LOG_ERROR

#define LOG_STRING_EXPAND(token) #token
#define LOG_STRING(token) LOG_STRING_EXPAND(token)

// WARNING: format must be string literal
#define LOG(prefix, suffix, format, ...) fprintf(stderr, prefix format suffix, __VA_ARGS__);

#if (LOG_DEBUG >= LOG_LEVEL)

# define LOG_DEBUG(...) LOG(__FILE__ ":" LOG_STRING(__LINE__) " ", "\n", __VA_ARGS__, 0)
# define LOG_WARNING(...) LOG(__FILE__ ":" LOG_STRING(__LINE__) " " LOG_YELLOW, LOG_RESET "\n", __VA_ARGS__, 0)
# define LOG_ERROR(...) LOG(__FILE__ ":" LOG_STRING(__LINE__) " " LOG_RED, LOG_RESET "\n", __VA_ARGS__, 0)
# define LOG_CRITICAL(...) LOG(__FILE__ ":" LOG_STRING(__LINE__) " " LOG_RED, LOG_RESET "\n", __VA_ARGS__, 0)

#else

# define LOG_DEBUG(...) void

# if (LOG_WARNING >= LOG_LEVEL)
#  define LOG_WARNING(...) LOG(LOG_YELLOW, LOG_RESET "\n", __VA_ARGS__, 0)
# else
#  define LOG_WARNING(...) void
# endif

# if (LOG_ERROR >= LOG_LEVEL)
#  define LOG_ERROR(...) LOG(LOG_RED, LOG_RESET "\n", __VA_ARGS__, 0)
# else
#  define LOG_ERROR(...) void
# endif

# if (LOG_CRITICAL >= LOG_LEVEL)
#  define LOG_CRITICAL(...) LOG(LOG_RED, LOG_RESET "\n", __VA_ARGS__, 0)
# else
#  define LOG_CRITICAL(...) void
# endif

#endif
