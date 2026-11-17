/* Build-time configuration; values come from CMake cache variables, these are the fallbacks. */
#ifndef CQ_CONFIG_H
#define CQ_CONFIG_H
#ifndef CQ_LADDER_N
#define CQ_LADDER_N 53 /* docs/ANGLE_LADDER.md §2 */
#endif
#ifndef CQ_K2
#define CQ_K2 4096
#endif
#ifndef CQ_MAX_QADDR_BITS
#define CQ_MAX_QADDR_BITS 16 /* provisional */
#endif
#endif
