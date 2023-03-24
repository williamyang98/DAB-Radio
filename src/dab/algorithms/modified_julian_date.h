#pragma once

// Source: http://www.leapsecond.com/tools/gdate.c
// Convert Modified Julian Day (MJD) to year/month/day calendar date.
// - assumes Gregorian calendar
// - year is (4-digit calendar year), month is (1-12), day is (1-31)
// - adapted (tvb) from Fliegel/van Flandern ACM 11/#10 p 657 Oct 1968
static void mjd_to_ymd(long mjd, int &year, int &month, int &day)
{
    long J, C, Y, M;

    J = mjd + 2400001 + 68569;
    C = 4 * J / 146097;
    J = J - (146097 * C + 3) / 4;
    Y = 4000 * (J + 1) / 1461001;
    J = J - 1461 * Y / 4 + 31;
    M = 80 * J / 2447;
    day = (int) (J - 2447 * M / 80);
    J = M / 11;
    month = (int) (M + 2 - (12 * J));
    year = (int) (100 * (C - 49) + Y + J);
    return;
}