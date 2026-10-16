/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

/*
 * $Logfile: /Freespace2/code/Math/Fix.cpp $
 * $Revision: 2 $
 * $Date: 10/07/98 10:53a $
 * $Author: Dave $
 *
 * Code to deal with 16.16 fixed point numbers.
 *
 * $Log: /Freespace2/code/Math/Fix.cpp $
 * 
 * 2     10/07/98 10:53a Dave
 * Initial checkin.
 * 
 * 1     10/07/98 10:49a Dave
 * 
 * 2     2/17/97 5:18p John
 * Added a bunch of RCS headers to a bunch of old files that don't have
 * them.
 *
 * $NoKeywords: $
 */


#include <limits.h>

#include "PsTypes.h"
#include "fix.h"

// Claude: Win32 MulDiv, so fix math needs no windows.h: a*b/c in 64 bits, rounded to nearest (half away from
// zero), -1 when c is 0 or the result overflows. Matches MulDiv on 50 million cases, edges included.
static int fix_muldiv(int a, int b, int c)
{
	if (c == 0) {
		return -1;
	}
	if (c < 0) {
		a = -a;
		c = -c;
	}
	longlong p = (longlong)a * (longlong)b;
	longlong q;
	if ((a < 0) == (b < 0)) {
		q = (p + (c / 2)) / c;
	} else {
		q = (p - (c / 2)) / c;
	}
	if ((q > INT_MAX) || (q < -INT_MAX)) {
		return -1;
	}
	return (int)q;
}

fix fixmul(fix a, fix b)
{
	longlong tmp;
	tmp = (longlong)a * (longlong)b;
	return (fix)(tmp>>16);
}

fix fixdiv(fix a, fix b)
{
	return fix_muldiv(a,65536,b);
}

fix fixmuldiv(fix a, fix b,fix c)
{
	return fix_muldiv(a,b,c);
}
