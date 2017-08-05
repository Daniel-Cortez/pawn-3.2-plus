/*============================================================================*\
 *  Copyright (c) 2017, Stanislav Gromov                                      *
 *                                                                            *
 *  Permission to use, copy, modify, and/or distribute this software for any  *
 *  purpose with or without fee is hereby granted, provided that the above    *
 *  copyright notice and this permission notice appear in all copies.         *
 *                                                                            *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES  *
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF          *
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR   *
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES    *
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN     *
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF   *
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.            *
\*============================================================================*/

/* Simple benchmark based on computation of the Fibonacci numbers.
 * Might be useful to compare the performance of the new interpreter core
 * with the old ones.
 */


#if !defined NUM_ITERATIONS
    #define NUM_ITERATIONS 1_000_000
#endif

#include <console>
#include <time>

main()
{
    static i, ticks;
    static f1, f2, j, t;
    new a[46];
    ticks = tickcount();
    for (i = 0; i < NUM_ITERATIONS; ++i)
    {
        f1 = 1, f2 = 1;
        a[0] = 1, a[1] = 1;
        for (j = 2; j < sizeof(a); ++j)
            t = f1, f1 = f2, a[j] = f2 = f2 + t;
    }
    ticks = tickcount() - ticks;
    for (j = 0; j < sizeof(a); ++j)
        printf("%2d: %d\n", j+1, a[j]);
    printf("Done %d times in %d ticks\n", NUM_ITERATIONS, ticks);
}
