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

// System resources are not sufficient to handle the request.
#define ERROR_MEMORY				-1

// Invalid input data.
#define ERROR_INPUT					-2

// Request requires access rights that are not available.
#define ERROR_ACCESS				-3

// Entity that is required for the operation is missing.
#define ERROR_MISSING				-4

// Unable to create a necessary entity because it exists.
#define ERROR_EXIST					-5

// Filement filesystem internal error.
#define ERROR_EVFS					-6

// Temporary condition caused error.
#define ERROR_AGAIN					-7

// Unsupported feature is required to satisfy the request.
#define ERROR_UNSUPPORTED			-8

// Read error.
#define ERROR_READ					-9

// Write error.
#define ERROR_WRITE					-10

// Action was cancelled.
#define ERROR_CANCEL				-11

// An asynchronous operation is now in progress.
#define ERROR_PROGRESS				-12

// Unable to resolve domain.
#define ERROR_RESOLVE				-13

// Network operation failed.
#define ERROR_NETWORK				-14

// Unknown error.
#define ERROR						-32767
