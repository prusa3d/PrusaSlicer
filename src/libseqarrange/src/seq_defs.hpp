/*================================================================*/
/*
 *  Author:  Pavel Surynek, 2023 - 2025
 *
 *  File:    seq_defs.h
 *
 *  Definitions of useful macros.
 */
/*================================================================*/

#ifndef __SEQ_DEFS_HPP__
#define __SEQ_DEFS_HPP__

/*----------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <set>


/*----------------------------------------------------------------*/


using namespace std;

#define SEQ_UNUSED(x)

//#define DEBUG
//#define PROFILE

typedef wchar_t wchar;

typedef std::basic_string<char> string;
typedef std::vector<string> strings_vector;
typedef std::set<string> strings_set;


/*----------------------------------------------------------------*/

extern const string INDENT;

/*----------------------------------------------------------------*/

#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#define DFR(x,y) (((x) < (y)) ? ((y) - (x)) : ((x) - (y)))
#define ABS(x) (((x) < 0) ? -(x) : (x))
#define SGN(x) (((x) < 0) ? -(-1) : ((x) > 0) ? 1 : 0)


/*----------------------------------------------------------------*/

#ifdef DEBUG
  #define ASSERT(condition)                                                             \
    {                                                                                   \
      if (!(condition))							                \
      {                                                                                 \
        printf("ASSERT: assertion failed (file: %s, line:%d).\n", __FILE__, __LINE__);  \
	fflush(NULL);                                                                   \
	exit(-1);						   	                \
      }                                                                                 \
    }
#else
  #define ASSERT(condition)
#endif /* DEBUG */

    
/*----------------------------------------------------------------*/

#endif /* __SEQ_DEFS_HPP__ */
