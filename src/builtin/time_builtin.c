#include "time_builtin.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "../util/error.h"
#include "../util/strutil.h"

/* Calendar arithmetic is done here instead of through strftime()/strptime()/
 * gmtime(): those differ between platforms (Windows has no strptime, its
 * strftime aborts on unknown specifiers and lacks %e/%F/%T on older runtimes),
 * and %L (milliseconds) does not exist in them at all. Everything below is
 * the proleptic Gregorian calendar in UTC plus a fixed offset. */

#define MIN_TIMESTAMP (-62135596800LL)   /* 0001-01-01T00:00:00Z */
#define MAX_TIMESTAMP 253402300799LL     /* 9999-12-31T23:59:59Z */
#define DEFAULT_FORMAT "%Y-%m-%dT%H:%M:%SZ"

#define FORMAT_SPECS "YymdeHMSIpjaAbBuwLszZFT%"
#define PARSE_SPECS  "YymdeHMSIpaAbBLszZFT%"

static const char *const WEEKDAYS[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const MONTHS[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static long long floor_div(long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
static long long days_from_civil(long long y, int m, int d) {
    y -= (m <= 2);
    long long era = floor_div(y, 400);
    long long yoe = y - era * 400;
    long long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long long z, int *y, int *m, int *d) {
    z += 719468;
    long long era = floor_div(z, 146097);
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static int is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap_year(y)) ? 29 : days[m - 1];
}

/* ---- Time zones ----------------------------------------------------------- */

typedef struct {
    int local;            /* 1: use the machine's local zone */
    int offset;           /* fixed offset in seconds when !local */
    char label[16];       /* what %Z prints for UTC / fixed offsets */
} Zone;

/* Parses "Z", "+HH", "+HHMM" or "+HH:MM" at *pp. */
static int parse_zone_offset(const char **pp, int *offset) {
    const char *p = *pp;
    if (*p == 'Z' || *p == 'z') {
        *offset = 0;
        *pp = p + 1;
        return 1;
    }
    if (*p != '+' && *p != '-') return 0;
    int sign = (*p == '-') ? -1 : 1;
    p++;
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return 0;
    int hh = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    int mm = 0;
    if (*p == ':') {
        p++;
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return 0;
        mm = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    } else if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        mm = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    }
    if (hh > 23 || mm > 59) return 0;
    *offset = sign * (hh * 3600 + mm * 60);
    *pp = p;
    return 1;
}

static int parse_zone_arg(const char *text, Zone *z) {
    memset(z, 0, sizeof(*z));
    if (nac_ci_equal(text, "utc") || nac_ci_equal(text, "z")) {
        strcpy(z->label, "UTC");
        return 1;
    }
    if (nac_ci_equal(text, "local")) {
        z->local = 1;
        return 1;
    }
    const char *p = text;
    if (*p != '+' && *p != '-') return 0;
    if (!parse_zone_offset(&p, &z->offset) || *p != '\0') return 0;
    int abs_off = z->offset < 0 ? -z->offset : z->offset;
    snprintf(z->label, sizeof(z->label), "%c%02d:%02d",
             z->offset < 0 ? '-' : '+', abs_off / 3600, (abs_off % 3600) / 60);
    return 1;
}

/* Offset (seconds east of UTC) and abbreviation of the machine's local zone
 * at `secs`. Returns 0 if the C library cannot represent that instant. */
static int local_zone_at(long long secs, int *offset, char *abbr, size_t abbr_cap) {
    time_t t = (time_t)secs;
    if ((long long)t != secs) return 0;

    struct tm tmv;
#ifdef _WIN32
    if (localtime_s(&tmv, &t) != 0) return 0;
#else
    if (!localtime_r(&t, &tmv)) return 0;
#endif
    long long shown = days_from_civil((long long)tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday) * 86400LL
                      + tmv.tm_hour * 3600LL + tmv.tm_min * 60LL + tmv.tm_sec;
    *offset = (int)(shown - secs);
    if (strftime(abbr, abbr_cap, "%Z", &tmv) == 0) abbr[0] = '\0';
    return 1;
}

/* ---- Formatting ----------------------------------------------------------- */

typedef struct {
    int year, mon, mday, hour, min, sec, ms;
    int wday;             /* 0 = Sunday */
    int yday;             /* 1..366 */
    int offset;           /* seconds east of UTC */
    long long epoch;
    char zone[64];
} Broken;

static int break_time(long long secs, int ms, const Zone *zone, Broken *bt) {
    int offset = zone->offset;
    char label[64];
    snprintf(label, sizeof(label), "%s", zone->label);
    if (zone->local && !local_zone_at(secs, &offset, label, sizeof(label))) return 0;

    long long shown = secs + offset;
    long long days = floor_div(shown, 86400);
    long long rem = shown - days * 86400;

    civil_from_days(days, &bt->year, &bt->mon, &bt->mday);
    bt->hour = (int)(rem / 3600);
    bt->min = (int)((rem % 3600) / 60);
    bt->sec = (int)(rem % 60);
    bt->ms = ms;
    bt->wday = (int)(((days % 7) + 7 + 4) % 7);          /* 1970-01-01 was a Thursday */
    bt->yday = (int)(days - days_from_civil(bt->year, 1, 1)) + 1;
    bt->offset = offset;
    bt->epoch = secs;
    snprintf(bt->zone, sizeof(bt->zone), "%s", label);
    return 1;
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    int overflow;
} Out;

static void out_str(Out *o, const char *s) {
    size_t n = strlen(s);
    if (o->len + n >= o->cap) {
        o->overflow = 1;
        return;
    }
    memcpy(o->buf + o->len, s, n + 1);
    o->len += n;
}

static void out_num(Out *o, const char *fmt, long long v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), fmt, v);
    out_str(o, tmp);
}

/* Returns 0 and sets *bad to the offending character on an unsupported
 * specifier. */
static int format_broken(const Broken *bt, const char *fmt, Out *o, char *bad) {
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            char c[2] = { *f, '\0' };
            out_str(o, c);
            continue;
        }
        f++;
        if (*f == '\0' || !strchr(FORMAT_SPECS, *f)) {
            *bad = *f;
            return 0;
        }
        switch (*f) {
            case 'Y': out_num(o, "%04lld", bt->year); break;
            case 'y': out_num(o, "%02lld", bt->year % 100); break;
            case 'm': out_num(o, "%02lld", bt->mon); break;
            case 'd': out_num(o, "%02lld", bt->mday); break;
            case 'e': out_num(o, "%2lld", bt->mday); break;
            case 'H': out_num(o, "%02lld", bt->hour); break;
            case 'M': out_num(o, "%02lld", bt->min); break;
            case 'S': out_num(o, "%02lld", bt->sec); break;
            case 'I': out_num(o, "%02lld", bt->hour % 12 == 0 ? 12 : bt->hour % 12); break;
            case 'p': out_str(o, bt->hour < 12 ? "AM" : "PM"); break;
            case 'j': out_num(o, "%03lld", bt->yday); break;
            case 'a': {
                char abbr[4];
                memcpy(abbr, WEEKDAYS[bt->wday], 3);
                abbr[3] = '\0';
                out_str(o, abbr);
                break;
            }
            case 'A': out_str(o, WEEKDAYS[bt->wday]); break;
            case 'b': {
                char abbr[4];
                memcpy(abbr, MONTHS[bt->mon - 1], 3);
                abbr[3] = '\0';
                out_str(o, abbr);
                break;
            }
            case 'B': out_str(o, MONTHS[bt->mon - 1]); break;
            case 'u': out_num(o, "%lld", bt->wday == 0 ? 7 : bt->wday); break;
            case 'w': out_num(o, "%lld", bt->wday); break;
            case 'L': out_num(o, "%03lld", bt->ms); break;
            case 's': out_num(o, "%lld", bt->epoch); break;
            case 'z': {
                int off = bt->offset < 0 ? -bt->offset : bt->offset;
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "%c%02d%02d", bt->offset < 0 ? '-' : '+',
                         off / 3600, (off % 3600) / 60);
                out_str(o, tmp);
                break;
            }
            case 'Z': out_str(o, bt->zone); break;
            case 'F': {
                char tmp[24];
                snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d", bt->year, bt->mon, bt->mday);
                out_str(o, tmp);
                break;
            }
            case 'T': {
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "%02d:%02d:%02d", bt->hour, bt->min, bt->sec);
                out_str(o, tmp);
                break;
            }
            case '%': out_str(o, "%"); break;
        }
    }
    return 1;
}

/* Splits a numeric NaC value (seconds, optionally fractional) into whole
 * seconds and milliseconds. */
static int split_timestamp(Value v, long long *secs, int *ms) {
    if (v.type == TYPE_INT) {
        *secs = v.int_val;
        *ms = 0;
        return 1;
    }
    if (v.type != TYPE_FLOAT || !isfinite(v.float_val) || fabs(v.float_val) > 1e12) return 0;

    long long total_ms = llround(v.float_val * 1000.0);
    *secs = floor_div(total_ms, 1000);
    *ms = (int)(total_ms - *secs * 1000);
    return 1;
}

/* ---- Parsing -------------------------------------------------------------- */

typedef struct {
    int year, mon, mday, hour, min, sec, ms, offset;
    int pm;               /* -1 unset, 0 AM, 1 PM */
    int h12;              /* an %I was read */
    int have_epoch;
    long long epoch;
} Parsed;

static void parsed_init(Parsed *pt) {
    memset(pt, 0, sizeof(*pt));
    pt->year = 1970;
    pt->mon = 1;
    pt->mday = 1;
    pt->pm = -1;
}

static int read_digits(const char **pp, int min_digits, int max_digits, int *out) {
    const char *p = *pp;
    int value = 0;
    int n = 0;
    while (n < max_digits && isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        p++;
        n++;
    }
    if (n < min_digits) return 0;
    *out = value;
    *pp = p;
    return 1;
}

/* ".5" is 500 ms, ".123456" is 123 ms: the fraction of a second. */
static int read_fraction_ms(const char **pp, int *ms) {
    const char *p = *pp;
    if (!isdigit((unsigned char)*p)) return 0;
    int value = 0;
    int digits = 0;
    while (isdigit((unsigned char)*p)) {
        if (digits < 3) {
            value = value * 10 + (*p - '0');
            digits++;
        }
        p++;
    }
    while (digits < 3) {
        value *= 10;
        digits++;
    }
    *ms = value;
    *pp = p;
    return 1;
}

/* Matches a full or three-letter English name, case-insensitively. */
static int match_name(const char **pp, const char *const *names, int count, int *index) {
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < count; i++) {
            size_t len = pass == 0 ? strlen(names[i]) : 3;
            size_t k = 0;
            while (k < len && (*pp)[k] &&
                   tolower((unsigned char)(*pp)[k]) == tolower((unsigned char)names[i][k])) {
                k++;
            }
            if (k == len) {
                *pp += len;
                *index = i;
                return 1;
            }
        }
    }
    return 0;
}

static int scan_format(const char **pp, const char *fmt, Parsed *pt) {
    const char *p = *pp;
    for (const char *f = fmt; *f; f++) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*p)) p++;
            continue;
        }
        if (*f != '%') {
            if (*p != *f) return 0;
            p++;
            continue;
        }
        f++;
        int v = 0;
        switch (*f) {
            case 'Y': if (!read_digits(&p, 4, 4, &pt->year)) return 0; break;
            case 'y':
                if (!read_digits(&p, 2, 2, &v)) return 0;
                pt->year = v < 69 ? 2000 + v : 1900 + v;
                break;
            case 'm': if (!read_digits(&p, 1, 2, &pt->mon)) return 0; break;
            case 'd': if (!read_digits(&p, 1, 2, &pt->mday)) return 0; break;
            case 'e':
                while (*p == ' ') p++;
                if (!read_digits(&p, 1, 2, &pt->mday)) return 0;
                break;
            case 'H': if (!read_digits(&p, 1, 2, &pt->hour)) return 0; break;
            case 'M': if (!read_digits(&p, 1, 2, &pt->min)) return 0; break;
            case 'S': if (!read_digits(&p, 1, 2, &pt->sec)) return 0; break;
            case 'I':
                if (!read_digits(&p, 1, 2, &pt->hour)) return 0;
                pt->h12 = 1;
                break;
            case 'p':
                if (tolower((unsigned char)p[0]) == 'a' && tolower((unsigned char)p[1]) == 'm') {
                    pt->pm = 0;
                } else if (tolower((unsigned char)p[0]) == 'p' && tolower((unsigned char)p[1]) == 'm') {
                    pt->pm = 1;
                } else {
                    return 0;
                }
                p += 2;
                break;
            case 'b':
            case 'B':
                if (!match_name(&p, MONTHS, 12, &v)) return 0;
                pt->mon = v + 1;
                break;
            case 'a':
            case 'A':
                if (!match_name(&p, WEEKDAYS, 7, &v)) return 0;   /* read, then ignored */
                break;
            case 'z': if (!parse_zone_offset(&p, &pt->offset)) return 0; break;
            case 'Z': {
                char word[8];
                size_t n = 0;
                while (isalpha((unsigned char)*p) && n < sizeof(word) - 1) word[n++] = *p++;
                word[n] = '\0';
                if (!nac_ci_equal(word, "UTC") && !nac_ci_equal(word, "GMT") && !nac_ci_equal(word, "Z")) return 0;
                pt->offset = 0;
                break;
            }
            case 'L': if (!read_fraction_ms(&p, &pt->ms)) return 0; break;
            case 's': {
                int neg = (*p == '-');
                if (neg) p++;
                if (!isdigit((unsigned char)*p)) return 0;
                long long e = 0;
                int n = 0;
                while (isdigit((unsigned char)*p) && n < 13) {
                    e = e * 10 + (*p - '0');
                    p++;
                    n++;
                }
                pt->epoch = neg ? -e : e;
                pt->have_epoch = 1;
                break;
            }
            case 'F':
                if (!scan_format(&p, "%Y-%m-%d", pt)) return 0;
                break;
            case 'T':
                if (!scan_format(&p, "%H:%M:%S", pt)) return 0;
                break;
            case '%':
                if (*p != '%') return 0;
                p++;
                break;
            default:
                return 0;
        }
    }
    *pp = p;
    return 1;
}

/* ISO 8601 / RFC 3339: YYYY-MM-DD, optionally followed by T or a space and
 * HH:MM[:SS[.fraction]] and a zone (Z, +HH:MM, +HHMM, +HH). Without a zone
 * the time is taken as UTC. */
static int parse_iso8601(const char *text, Parsed *pt) {
    const char *p = text;
    while (isspace((unsigned char)*p)) p++;

    if (!read_digits(&p, 4, 4, &pt->year) || *p++ != '-') return 0;
    if (!read_digits(&p, 2, 2, &pt->mon) || *p++ != '-') return 0;
    if (!read_digits(&p, 2, 2, &pt->mday)) return 0;

    if (*p == 'T' || *p == 't' || *p == ' ') {
        p++;
        if (!read_digits(&p, 2, 2, &pt->hour) || *p++ != ':') return 0;
        if (!read_digits(&p, 2, 2, &pt->min)) return 0;
        if (*p == ':') {
            p++;
            if (!read_digits(&p, 2, 2, &pt->sec)) return 0;
            if (*p == '.' || *p == ',') {
                p++;
                if (!read_fraction_ms(&p, &pt->ms)) return 0;
            }
        }
        while (*p == ' ') p++;
        if (*p == 'Z' || *p == 'z' || *p == '+' || *p == '-') {
            if (!parse_zone_offset(&p, &pt->offset)) return 0;
        }
    }

    while (isspace((unsigned char)*p)) p++;
    return *p == '\0';
}

/* Validates the fields and converts to epoch seconds. */
static int parsed_to_epoch(Parsed *pt, long long *secs) {
    if (pt->have_epoch) {
        *secs = pt->epoch;
        return *secs >= MIN_TIMESTAMP && *secs <= MAX_TIMESTAMP;
    }
    if (pt->h12) {
        if (pt->hour < 1 || pt->hour > 12) return 0;
        pt->hour = pt->hour % 12 + (pt->pm == 1 ? 12 : 0);
    }
    if (pt->year < 1 || pt->mon < 1 || pt->mon > 12) return 0;
    if (pt->mday < 1 || pt->mday > days_in_month(pt->year, pt->mon)) return 0;
    if (pt->hour > 23 || pt->min > 59 || pt->sec > 59) return 0;

    *secs = days_from_civil(pt->year, pt->mon, pt->mday) * 86400LL
            + pt->hour * 3600LL + pt->min * 60LL + pt->sec - pt->offset;
    return *secs >= MIN_TIMESTAMP && *secs <= MAX_TIMESTAMP;
}

/* ---- Builtins ------------------------------------------------------------- */

static double now_epoch_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long ticks = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (double)((ticks - 116444736000000000ULL) / 10000ULL);   /* 100 ns since 1601 -> ms since 1970 */
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)(tv.tv_usec / 1000);
#endif
}

static int all_specs_supported(const char *fmt, const char *allowed, char *bad) {
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') continue;
        f++;
        if (*f == '\0' || !strchr(allowed, *f)) {
            *bad = *f;
            return 0;
        }
    }
    return 1;
}

static void report_bad_spec(const char *fn, char bad) {
    char msg[160];
    if (bad == '\0') {
        snprintf(msg, sizeof(msg), "%s(): format ends with a lone '%%'", fn);
    } else {
        snprintf(msg, sizeof(msg), "%s(): unsupported format specifier '%%%c'", fn, bad);
    }
    report_error(msg);
}

bool is_time_builtin(const char *name) {
    return strcmp(name, "nowMs") == 0 || strcmp(name, "formatTime") == 0 ||
           strcmp(name, "parseTime") == 0;
}

Value call_time_builtin(const char *name, Value *args, int arg_count) {
    if (strcmp(name, "nowMs") == 0) {
        if (arg_count != 0) {
            report_error("nowMs() requires 0 arguments");
            return make_float(0);
        }
        return make_float(now_epoch_ms());
    }

    if (strcmp(name, "formatTime") == 0) {
        if (arg_count < 1 || arg_count > 3) {
            report_error("formatTime() requires (timestamp [, format [, zone]])");
            return make_string("");
        }
        long long secs = 0;
        int ms = 0;
        if (!split_timestamp(args[0], &secs, &ms)) {
            report_error("formatTime(): timestamp must be a number of seconds since 1970");
            return make_string("");
        }
        if (secs < MIN_TIMESTAMP || secs > MAX_TIMESTAMP) {
            report_error("formatTime(): timestamp is out of range (years 0001 to 9999)");
            return make_string("");
        }

        const char *fmt = DEFAULT_FORMAT;
        if (arg_count >= 2) {
            if (args[1].type != TYPE_STRING) {
                report_error("formatTime(): format must be a string");
                return make_string("");
            }
            fmt = args[1].str_val;
        }

        Zone zone;
        parse_zone_arg("utc", &zone);
        if (arg_count == 3) {
            if (args[2].type != TYPE_STRING || !parse_zone_arg(args[2].str_val, &zone)) {
                report_error("formatTime(): zone must be \"utc\", \"local\" or an offset like \"+03:00\"");
                return make_string("");
            }
        }

        char bad = '\0';
        if (!all_specs_supported(fmt, FORMAT_SPECS, &bad)) {
            report_bad_spec("formatTime", bad);
            return make_string("");
        }

        Broken bt;
        if (!break_time(secs, ms, &zone, &bt)) {
            report_error("formatTime(): local time is not available for that timestamp");
            return make_string("");
        }

        char out[MAX_STRING_LEN];
        Out o = { out, sizeof(out), 0, 0 };
        out[0] = '\0';
        format_broken(&bt, fmt, &o, &bad);
        if (o.overflow) {
            report_error("formatTime(): result is too long");
            return make_string("");
        }
        return make_string(out);
    }

    if (strcmp(name, "parseTime") == 0) {
        if (arg_count < 1 || arg_count > 2 || args[0].type != TYPE_STRING ||
            (arg_count == 2 && args[1].type != TYPE_STRING)) {
            report_error("parseTime() requires (text [, format]) with string arguments");
            return make_null();
        }

        Parsed pt;
        parsed_init(&pt);
        int ok;
        if (arg_count == 2) {
            char bad = '\0';
            if (!all_specs_supported(args[1].str_val, PARSE_SPECS, &bad)) {
                report_bad_spec("parseTime", bad);
                return make_null();
            }
            const char *p = args[0].str_val;
            while (isspace((unsigned char)*p)) p++;
            ok = scan_format(&p, args[1].str_val, &pt);
            while (isspace((unsigned char)*p)) p++;
            ok = ok && *p == '\0';
        } else {
            ok = parse_iso8601(args[0].str_val, &pt);
        }

        long long secs = 0;
        if (!ok || !parsed_to_epoch(&pt, &secs)) return make_null();   /* not a time: null, not an error */

        if (pt.ms != 0) return make_float((double)secs + pt.ms / 1000.0);
        if (secs >= INT_MIN && secs <= INT_MAX) return make_int((int)secs);
        return make_float((double)secs);
    }

    report_error("Unknown time built-in function");
    return make_null();
}
