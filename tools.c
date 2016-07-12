/*
 * rpihddevice - VDR HD output device for Raspberry Pi
 * Copyright (C) 2014, 2015, 2016 Thomas Reufer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <limits.h>
#include <vdr/tools.h>
#include "tools.h"
#include <algorithm>

/*
 * ffmpeg's implementation for rational numbers:
 * https://github.com/FFmpeg/FFmpeg/blob/master/libavutil/rational.c
 */

cRational::cRational(double d) :
	num(0), den(0)
{
	int exp;
	frexp(d, &exp);

	den = 1LL << (29 - std::max(exp - 1, 0));
	num = floor(d * den + 0.5);

	Reduce(INT_MAX);
}

bool cRational::Reduce(int max)
{
	cRational a0 = cRational(0, 1), a1 = cRational(1, 0);
	int sign = (num < 0) ^ (den < 0);
	if (int div = Gcd(abs(num), abs(den)))
	{
		num = abs(num) / div;
		den = abs(den) / div;
	}
	if (num <= max && den <= max)
	{
		a1 = cRational(num, den);
		den = 0;
	}
	while (den)
	{
		int x = num / den;
		int nextDen = num - den * x;
		cRational a2 = cRational(x * a1.num + a0.num, x * a1.den + a0.den);
		if (a2.num > max || a2.den > max)
		{
			if (a1.num)
				x = (max - a0.num) / a1.num;
			if (a1.den)
				x = std::min(x, (max - a0.den) / a1.den);
			if (den * (2 * x * a1.den + a0.den) > num * a1.den)
				a1 = cRational(x * a1.num + a0.num, x * a1.den + a0.den);
			break;
		}
		a0 = a1;
		a1 = a2;
		num = den;
		den = nextDen;
	}
	num = sign ? -a1.num : a1.num;
	den = a1.den;
	return den == 0;
}

/*
 * Stein's binary GCD algorithm:
 * https://en.wikipedia.org/wiki/Binary_GCD_algorithm
 */

int cRational::Gcd(int u, int v)
{
    if (u == v || v == 0)
        return u;

    if (u == 0)
        return v;

    // look for factors of 2
    if (~u & 1) // u is even
    {
        if (v & 1) // v is odd
            return Gcd(u >> 1, v);
        else // both u and v are even
            return Gcd(u >> 1, v >> 1) << 1;
    }

    if (~v & 1) // u is odd, v is even
        return Gcd(u, v >> 1);

    // reduce larger argument
    if (u > v)
        return Gcd((u - v) >> 1, v);

    return Gcd((v - u) >> 1, u);
}
