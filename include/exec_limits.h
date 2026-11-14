#ifndef KILONS_EXEC_LIMITS_H
#define KILONS_EXEC_LIMITS_H
/* Includes the bytes of strings, their terminators and one AMD64 pointer
 * per entry. The fixed stack header and vector terminators are separate. */
#define EXEC_ARG_BYTES 4096U
#define EXEC_MAX_STRINGS (EXEC_ARG_BYTES / 9U)
#endif
