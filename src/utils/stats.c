//
// Created by nrper on 7/24/2026.
//
/**
 *@file stats.c
 *@brief The end-of-run report. Sums the per-thread counters recorded through the STAT_* macros and
 * prints them in human-readable units. Nothing here runs while the simulation does.
 *@author Noah Persily
*/
#include "utils/stats.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "variables/globals.h"

#define REPORT_WIDTH 88
#define BAR_WIDTH 20

// Box-drawing and block glyphs. These are UTF-8 in the source, which is why MSVC is built with
// /utf-8 (see CMakeLists.txt) -- without it the execution charset is the system codepage and these
// literals get mangled at compile time. printStats also puts the console into UTF-8 before writing
// any of them.
#define GLYPH_RULE  "─"
#define GLYPH_HEAVY "═"
#define GLYPH_BLOCK "█"

// Left-aligned partial blocks, so a bar has eighth-of-a-column resolution instead of rounding down
// to nothing for small values.
static const char *const EIGHTHS[8] = {
    "", "▏", "▎", "▍", "▌", "▋", "▊", "▉"
};


/**
 * @brief Hands out one of a small ring of scratch buffers so several fmt* results can be live in a
 *        single printf. Not thread safe by design -- printStats runs on one thread after every other
 *        thread has exited.
 */
static char *scratch(VOID) {
    static char buffers[8][48];
    static ULONG64 next = 0;

    char *buffer = buffers[next];
    next = (next + 1) % 8;

    return buffer;
}


/**
 * @brief Formats a raw count with a magnitude suffix, so 1310720 reads as "1.31 M".
 */
static const char *fmtCount(ULONG64 n) {
    char *out = scratch();

    if (n < 10000) {
        snprintf(out, 48, "%llu", n);
    } else if (n < 1000000) {
        snprintf(out, 48, "%.1f K", (double) n / 1000.0);
    } else if (n < 1000000000) {
        snprintf(out, 48, "%.2f M", (double) n / 1000000.0);
    } else {
        snprintf(out, 48, "%.2f B", (double) n / 1000000000.0);
    }

    return out;
}


/**
 * @brief Formats a byte count in 1024-based units, matching the KB/MB/GB macros the config uses.
 */
static const char *fmtBytes(ULONG64 bytes) {
    char *out = scratch();

    if (bytes < KB(1)) {
        snprintf(out, 48, "%llu B", bytes);
    } else if (bytes < MB(1)) {
        snprintf(out, 48, "%.1f KB", (double) bytes / (double) KB(1));
    } else if (bytes < GB(1)) {
        snprintf(out, 48, "%.1f MB", (double) bytes / (double) MB(1));
    } else {
        snprintf(out, 48, "%.2f GB", (double) bytes / (double) GB(1));
    }

    return out;
}


/**
 * @brief Formats a microsecond duration at whatever scale keeps it readable.
 */
static const char *fmtTime(ULONG64 microseconds) {
    char *out = scratch();

    if (microseconds < 1000) {
        snprintf(out, 48, "%llu us", microseconds);
    } else if (microseconds < 1000000) {
        snprintf(out, 48, "%.2f ms", (double) microseconds / 1000.0);
    } else if (microseconds < 60000000) {
        snprintf(out, 48, "%.2f s", (double) microseconds / 1000000.0);
    } else {
        snprintf(out, 48, "%.2f min", (double) microseconds / 60000000.0);
    }

    return out;
}


static double pct(ULONG64 part, ULONG64 whole) {
    if (whole == 0) {
        return 0.0;
    }

    return (double) part / (double) whole * 100.0;
}


/**
 * @brief Integer division that yields 0 instead of trapping when nothing was sampled.
 */
static ULONG64 safeDiv(ULONG64 numerator, ULONG64 denominator) {
    if (denominator == 0) {
        return 0;
    }

    return numerator / denominator;
}


static VOID repeatGlyph(const char *glyph, ULONG64 count) {
    for (ULONG64 i = 0; i < count; i++) {
        printf("%s", glyph);
    }
}


/**
 * @brief Prints a proportional bar for value against the largest value in its group.
 */
static VOID printBar(ULONG64 value, ULONG64 max) {
    ULONG64 eighths;
    ULONG64 full;

    if (max == 0) {
        return;
    }

    // work in eighths of a column so a value too small to fill one column still shows something
    eighths = value * BAR_WIDTH * 8 / max;
    full = eighths / 8;

    repeatGlyph(GLYPH_BLOCK, full);
    printf("%s", EIGHTHS[eighths % 8]);
}


/**
 * @brief A section heading: the title, then a rule out to the report width.
 */
static VOID rule(const char *title) {
    // columns used so far: two lead glyphs plus the spaces either side of the title. printf's return
    // value is no help here -- it counts UTF-8 bytes, and each glyph is three of them.
    ULONG64 used = strlen(title) + 4;

    printf("\n" GLYPH_RULE GLYPH_RULE " %s ", title);

    if (used < REPORT_WIDTH) {
        repeatGlyph(GLYPH_RULE, REPORT_WIDTH - used);
    }

    printf("\n");
}


static VOID row(const char *label, const char *format, ...) {
    va_list args;

    printf("  %-28s ", label);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}


/**
 * @brief A labelled count with its share of the group total and a bar. The workhorse of the report.
 */
static VOID rowBar(const char *label, ULONG64 value, ULONG64 total, ULONG64 max) {
    printf("  %-20s %10s  %6.2f%%  ", label, fmtCount(value), pct(value, total));
    printBar(value, max);
    printf("\n");
}


#if STATS

/**
 * @brief Names the range a log2 bucket covers. In tick mode the boundaries are converted to time,
 *        which is the only reason the wait histogram is readable at all.
 */
static VOID bucketLabel(char *out, ULONG64 size, ULONG64 bucket, boolean asTime, ULONG64 ticksPerUs) {
    ULONG64 low;
    ULONG64 high;

    if (bucket == 0) {
        snprintf(out, size, "%s", asTime ? "no wait" : "0");
        return;
    }

    low = 1ULL << (bucket - 1);
    high = (1ULL << bucket) - 1;

    if (asTime) {
        // fmtTime hands back rotating buffers; both are still live here because the ring holds 8
        snprintf(out, size, "%s - %s", fmtTime(low / ticksPerUs), fmtTime(high / ticksPerUs));
        return;
    }

    if (low == high) {
        snprintf(out, size, "%llu", low);
    } else {
        snprintf(out, size, "%llu - %llu", low, high);
    }
}


/**
 * @brief Prints every non-empty bucket of a log2 histogram as a labelled bar.
 */
static VOID histogram(const ULONG64 *hist, boolean asTime, ULONG64 ticksPerUs) {
    ULONG64 total;
    ULONG64 max;
    char label[48];

    total = 0;
    max = 0;

    for (ULONG64 i = 0; i < STATS_BUCKETS; i++) {
        total += hist[i];
        if (hist[i] > max) {
            max = hist[i];
        }
    }

    if (total == 0) {
        row("(no samples)", "");
        return;
    }

    for (ULONG64 i = 0; i < STATS_BUCKETS; i++) {
        if (hist[i] == 0) {
            continue;
        }

        bucketLabel(label, sizeof(label), i, asTime, ticksPerUs);
        rowBar(label, hist[i], total, max);
    }
}


/**
 * @brief Adds one thread's counters into the running total. Walks both structs as flat ULONG64
 *        arrays, which is why every member of statistics must be a ULONG64.
 */
static VOID addStats(statistics *total, const statistics *source) {
    ULONG64 *destination = (ULONG64 *) total;
    const ULONG64 *addend = (const ULONG64 *) source;

    for (ULONG64 i = 0; i < sizeof(statistics) / sizeof(ULONG64); i++) {
        destination[i] += addend[i];
    }
}


/**
 * @brief Sums the counters of every thread that records any.
 */
static VOID collectStats(statistics *total) {
    PTHREAD_INFO groups[5];
    ULONG64 counts[5];

    groups[0] = vm.threadInfo.user;
    counts[0] = vm.config.number_of_user_threads;
    groups[1] = vm.threadInfo.trimmer;
    counts[1] = vm.config.number_of_trimming_threads;
    groups[2] = vm.threadInfo.writer;
    counts[2] = vm.config.number_of_writing_threads;
    groups[3] = vm.threadInfo.aging;
    counts[3] = vm.config.number_of_aging_threads;
    groups[4] = vm.threadInfo.scheduler;
    counts[4] = vm.config.number_of_scheduler_threads;

    memset(total, 0, sizeof(statistics));

    for (int group = 0; group < 5; group++) {
        if (groups[group] == NULL) {
            continue;
        }

        for (ULONG64 i = 0; i < counts[group]; i++) {
            addStats(total, &groups[group][i].stats);
        }
    }
}

#endif


/**
 * @brief Prints the end-of-run report. Always available: with STATS off it falls back to the summary
 *        that the vm.misc counters can answer on their own.
 * @param elapsedMs Wall clock duration of the user-thread phase, from full_virtual_memory_test.
 */
VOID printStats(ULONG64 elapsedMs) {
    LARGE_INTEGER frequency;
    ULONG64 ticksPerUs;
    ULONG64 physicalPages;
    ULONG64 totalHardFaults;
    ULONG64 totalFaults;

    // the glyphs below are UTF-8; without this the console renders them as codepage garbage
    SetConsoleOutputCP(CP_UTF8);

    QueryPerformanceFrequency(&frequency);
    ticksPerUs = (ULONG64) frequency.QuadPart / 1000000;
    if (ticksPerUs == 0) {
        ticksPerUs = 1;
    }

    physicalPages = vm.config.number_of_physical_pages;

    totalHardFaults = vm.misc.pagesFromFree + vm.misc.pagesFromLocalCache + vm.misc.pagesFromStandBy;
    totalFaults = vm.misc.numRescues + totalHardFaults;

    printf("\n");
    repeatGlyph(GLYPH_HEAVY, REPORT_WIDTH);
    printf("\n  VIRTUAL MEMORY  RUN REPORT\n");
    repeatGlyph(GLYPH_HEAVY, REPORT_WIDTH);
    printf("\n");

    //
    // Configuration and wall clock
    //
    rule("RUN");
    row("elapsed", "%s", fmtTime(elapsedMs * 1000));
    row("physical", "%s  (%s pages)", fmtBytes(physicalPages * PAGE_SIZE), fmtCount(physicalPages));
    row("virtual", "%s", fmtBytes(vm.config.virtual_address_size));
    row("disk", "%s  (%s pages)", fmtBytes(vm.config.disk_size_in_bytes),
        fmtCount(vm.config.disk_size_in_pages));
    row("page table layers", "%llu  (%s tables)", vm.config.number_of_page_table_layers,
        fmtCount(vm.config.cumulative_number_of_page_tables));
    row("user threads", "%llu", vm.config.number_of_user_threads);
    row("free lists", "%llu", vm.config.number_of_free_lists);
    row("fault throughput", "%s faults/sec", fmtCount(safeDiv(totalFaults * 1000, elapsedMs)));

    //
    // Where faults were satisfied from
    //
    rule("FAULTS");
    row("total", "%s", fmtCount(totalFaults));
    row("hard", "%s  (%.2f%%)", fmtCount(totalHardFaults), pct(totalHardFaults, totalFaults));
    row("rescues", "%s  (%.2f%%)", fmtCount(vm.misc.numRescues), pct(vm.misc.numRescues, totalFaults));
    printf("\n");
    {
        ULONG64 max = vm.misc.pagesFromFree;
        if (vm.misc.pagesFromLocalCache > max) {
            max = vm.misc.pagesFromLocalCache;
        }
        if (vm.misc.pagesFromStandBy > max) {
            max = vm.misc.pagesFromStandBy;
        }

        rowBar("from free list", vm.misc.pagesFromFree, totalHardFaults, max);
        rowBar("from local cache", vm.misc.pagesFromLocalCache, totalHardFaults, max);
        rowBar("from standby", vm.misc.pagesFromStandBy, totalHardFaults, max);
    }

    //
    // Final list state
    //
    rule("LISTS AT EXIT");
    {
        ULONG64 max = vm.lists.standby.length;
        if (vm.lists.modified.length > max) {
            max = vm.lists.modified.length;
        }
        if (vm.lists.free.length > max) {
            max = vm.lists.free.length;
        }
        if (vm.pfn.numActivePages > max) {
            max = vm.pfn.numActivePages;
        }

        rowBar("active", vm.pfn.numActivePages, physicalPages, max);
        rowBar("standby", vm.lists.standby.length, physicalPages, max);
        rowBar("modified", vm.lists.modified.length, physicalPages, max);
        rowBar("free", vm.lists.free.length, physicalPages, max);
    }

#if !STATS
    rule("DETAIL");
    row("page waits", "%s", fmtCount(vm.misc.pageWaits));
    row("total time waiting", "%s", fmtTime(safeDiv(vm.misc.totalTimeWaiting, ticksPerUs)));
    row("trim cycles", "%s", fmtCount(vm.misc.numTrims));
    row("write cycles", "%s", fmtCount(vm.misc.numWrites));
    printf("\n  (built with STATS 0 -- set it to 1 in variables/structures.h for the full report)\n");
#else
    {
        statistics stats;
        ULONG64 rescueTotal;
        ULONG64 freedTotal;
        ULONG64 trimmedTotal;
        ULONG64 agedTotal;

        collectStats(&stats);

        //
        // Page waits: the headline starvation signal
        //
        rule("PAGE WAITS");
        row("waits", "%s", fmtCount(stats.pageWaits));
        row("share of faults", "%.2f%%", pct(stats.pageWaits, stats.faults));
        row("faults per wait", "%s", fmtCount(safeDiv(stats.faults, stats.pageWaits)));
        row("total waiting", "%s", fmtTime(safeDiv(stats.waitTicks, ticksPerUs)));
        row("average wait", "%s", fmtTime(safeDiv(safeDiv(stats.waitTicks, stats.pageWaits), ticksPerUs)));
        row("longest wait", "%s", fmtTime(safeDiv(stats.maxWaitTicks, ticksPerUs)));
        printf("\n");
        histogram(stats.waitHist, TRUE, ticksPerUs);

        //
        // What a fault actually cost
        //
        rescueTotal = stats.rescues[RESCUE_IN_FLIGHT] + stats.rescues[RESCUE_STANDBY]
                      + stats.rescues[RESCUE_MODIFIED];

        rule("FAULT DETAIL");
        row("entries", "%s", fmtCount(stats.faults));
        row("redo rate", "%.2f%%  (%s redos)", pct(stats.redoFaults, stats.faults + stats.redoFaults),
            fmtCount(stats.redoFaults));
        printf("\n");
        {
            ULONG64 max = stats.firstTouch > stats.diskReads ? stats.firstTouch : stats.diskReads;
            rowBar("first touch (zero)", stats.firstTouch, stats.firstTouch + stats.diskReads, max);
            rowBar("disk read", stats.diskReads, stats.firstTouch + stats.diskReads, max);
        }
        printf("\n  rescue kind\n");
        {
            ULONG64 max = 0;
            for (int i = 0; i < RESCUE_KINDS; i++) {
                if (stats.rescues[i] > max) {
                    max = stats.rescues[i];
                }
            }
            rowBar("mid-write", stats.rescues[RESCUE_IN_FLIGHT], rescueTotal, max);
            rowBar("off standby", stats.rescues[RESCUE_STANDBY], rescueTotal, max);
            rowBar("off modified", stats.rescues[RESCUE_MODIFIED], rescueTotal, max);
        }

        //
        // The cost of going multilevel
        //
        rule("TREE WALK  (page tables materialized per fault)");
        {
            ULONG64 total = 0;
            ULONG64 max = 0;
            ULONG64 weighted = 0;
            char label[32];

            for (ULONG64 i = 0; i <= STATS_MAX_LAYERS; i++) {
                total += stats.walkPTsMaterialized[i];
                weighted += i * stats.walkPTsMaterialized[i];
                if (stats.walkPTsMaterialized[i] > max) {
                    max = stats.walkPTsMaterialized[i];
                }
            }

            for (ULONG64 i = 0; i <= STATS_MAX_LAYERS; i++) {
                if (stats.walkPTsMaterialized[i] == 0) {
                    continue;
                }
                snprintf(label, sizeof(label), "%llu table%s", i, i == 1 ? "" : "s");
                rowBar(label, stats.walkPTsMaterialized[i], total, max);
            }

            printf("\n");
            row("tables per 1000 faults", "%llu", safeDiv(weighted * 1000, total));
        }

        //
        // What a free cost. The mirror of FAULT DETAIL: entries is every freeVA the budget forced,
        // most of which find a pte that is already free, exactly as most fault entries find a pte
        // another thread already handled.
        //
        freedTotal = stats.freed[FREE_ACTIVE] + stats.freed[FREE_TRANSITION] + stats.freed[FREE_DISK];

        rule("FREE DETAIL");
        row("entries", "%s", fmtCount(stats.frees));
        row("pages freed", "%s  (%.2f%%)", fmtCount(freedTotal), pct(freedTotal, stats.frees));
        row("already free", "%s  (%.2f%%)", fmtCount(stats.frees - freedTotal),
            pct(stats.frees - freedTotal, stats.frees));
        row("quota trips", "%s", fmtCount(stats.quotaTrips));
        row("pages per trip", "%llu  (target %d)", safeDiv(stats.freeBatchSum, stats.freeBatchCount),
            PAGES_TO_FREE_AT_QUOTA);
        row("revisits", "%s  (%.2f%% of faults)", fmtCount(stats.revisits),
            pct(stats.revisits, stats.faults));
        printf("\n  freed kind\n");
        {
            ULONG64 max = 0;
            for (int i = 0; i < FREE_KINDS; i++) {
                if (stats.freed[i] > max) {
                    max = stats.freed[i];
                }
            }
            rowBar("active", stats.freed[FREE_ACTIVE], freedTotal, max);
            rowBar("transition", stats.freed[FREE_TRANSITION], freedTotal, max);
            rowBar("on disk", stats.freed[FREE_DISK], freedTotal, max);
        }
        printf("\n  pages freed per quota trip\n");
        histogram(stats.freeBatchHist, FALSE, ticksPerUs);

        //
        // The same walk cost as TREE WALK above, paid in reverse. Anything above zero here is a page
        // table read back from disk purely so a free could see what was under it.
        //
        rule("FREE TREE WALK  (page tables faulted back in per free)");
        {
            ULONG64 total = 0;
            ULONG64 max = 0;
            ULONG64 weighted = 0;
            char label[32];

            for (ULONG64 i = 0; i <= STATS_MAX_LAYERS; i++) {
                total += stats.freeWalkPTsMaterialized[i];
                weighted += i * stats.freeWalkPTsMaterialized[i];
                if (stats.freeWalkPTsMaterialized[i] > max) {
                    max = stats.freeWalkPTsMaterialized[i];
                }
            }

            for (ULONG64 i = 0; i <= STATS_MAX_LAYERS; i++) {
                if (stats.freeWalkPTsMaterialized[i] == 0) {
                    continue;
                }
                snprintf(label, sizeof(label), "%llu table%s", i, i == 1 ? "" : "s");
                rowBar(label, stats.freeWalkPTsMaterialized[i], total, max);
            }

            printf("\n");
            row("tables per 1000 frees", "%llu", safeDiv(weighted * 1000, total));
        }

        //
        // Scheduler: the quotas it published, and how much slack it had when it picked them.
        // schedHeadroomCount is the number of wakeups that got as far as a real decision, so it is
        // the denominator for anything measured only on that path.
        //
        rule("SCHEDULER");
        row("wakeups", "%s  (%s calibrating, %s no history)", fmtCount(stats.schedWakeups),
            fmtCount(stats.schedCalibrating), fmtCount(stats.schedNoHistory));
        row("decisions", "%s  (period %d ms)", fmtCount(stats.schedHeadroomCount), SCHEDULER_PERIOD_MS);
        row("consumption", "%llu pages/wakeup", safeDiv(stats.schedConsumedSum, stats.schedConsumedCount));
        row("headroom until dry", "%s", fmtTime(safeDiv(stats.schedHeadroomSum, stats.schedHeadroomCount) * 1000));
        row("no slack to age", "%s  (%.2f%% of decisions)", fmtCount(stats.schedBehind),
            pct(stats.schedBehind, stats.schedHeadroomCount));
        printf("\n  observed rates (pages/sec)\n");
        row("ager", "%s", fmtCount(safeDiv(stats.schedAgeRateSum, stats.schedHeadroomCount)));
        row("trimmer", "%s", fmtCount(safeDiv(stats.schedTrimRateSum, stats.schedHeadroomCount)));
        row("writer", "%s", fmtCount(safeDiv(stats.schedWriteRateSum, stats.schedHeadroomCount)));
        printf("\n  quotas published (pages/wakeup)\n");
        row("age", "%llu", safeDiv(stats.schedAgeSum, stats.schedAgeCount));
        row("trim / write", "%llu", safeDiv(stats.schedTrimSum, stats.schedTrimCount));

        printf("\n  age quota\n");
        histogram(stats.schedAgeHist, FALSE, ticksPerUs);
        printf("\n  trim quota\n");
        histogram(stats.schedTrimHist, FALSE, ticksPerUs);
        printf("\n  headroom (ms until free + standby are exhausted)\n");
        histogram(stats.schedHeadroomHist, FALSE, ticksPerUs);

        //
        // How many aging rounds the scheduler thought it needed to reach max age. Piling up at
        // NUMBER_OF_AGES means the age lists never had enough old pages to satisfy demand.
        //
        {
            ULONG64 total = 0;
            ULONG64 max = 0;
            char label[32];

            for (int rounds = 0; rounds <= NUMBER_OF_AGES; rounds++) {
                total += stats.schedRoundsToAge[rounds];
                if (stats.schedRoundsToAge[rounds] > max) {
                    max = stats.schedRoundsToAge[rounds];
                }
            }

            printf("\n  aging rounds needed\n");
            for (int rounds = 0; rounds <= NUMBER_OF_AGES; rounds++) {
                if (stats.schedRoundsToAge[rounds] == 0) {
                    continue;
                }
                snprintf(label, sizeof(label), "%d round%s", rounds, rounds == 1 ? "" : "s");
                rowBar(label, stats.schedRoundsToAge[rounds], total, max);
            }
        }

        //
        // Ager: which way the comb went, per layer
        //
        rule("AGER");
        row("wakeups", "%s", fmtCount(stats.agerWakeups));
        row("ptes aged", "%s", fmtCount(stats.agerPTEsAged));
        row("aged per wakeup", "%s", fmtCount(safeDiv(stats.agerPTEsAged, stats.agerWakeups)));
        printf("\n  layer      visits      leaf%%   pruned%%  descend%%      aged%%\n");
        for (ULONG64 layer = 0; layer < STATS_MAX_LAYERS; layer++) {
            ULONG64 visits = 0;

            for (int branch = 0; branch < AGER_BRANCHES; branch++) {
                visits += stats.agerBranch[layer][branch];
            }

            if (visits == 0) {
                continue;
            }

            printf("  %5llu  %10s  %8.2f  %8.2f  %8.2f  %9.2f\n", layer, fmtCount(visits),
                   pct(stats.agerBranch[layer][AGER_LEAF_REGION], visits),
                   pct(stats.agerBranch[layer][AGER_PRUNED], visits),
                   pct(stats.agerBranch[layer][AGER_DESCENDED], visits),
                   pct(stats.agerBranch[layer][AGER_AGED_INPLACE], visits));
        }
        {
            ULONG64 pruned = 0;
            ULONG64 visits = 0;

            for (ULONG64 layer = 0; layer < STATS_MAX_LAYERS; layer++) {
                pruned += stats.agerBranch[layer][AGER_PRUNED];
                for (int branch = 0; branch < AGER_BRANCHES; branch++) {
                    visits += stats.agerBranch[layer][branch];
                }
            }

            printf("\n");
            row("subtrees pruned", "%s  (%.2f%% of visits)", fmtCount(pruned), pct(pruned, visits));
            row("ptes skipped by pruning", "~%s", fmtCount(pruned * vm.config.pte_entries_per_pagetable));
        }

        //
        // Trimmer: batch shape, and the age of what it took
        //
        trimmedTotal = 0;
        for (int age = 0; age < NUMBER_OF_AGES; age++) {
            trimmedTotal += stats.trimmedAge[age];
        }

        rule("TRIMMER");
        row("regions visited", "%s", fmtCount(stats.trimBatchCount));
        row("pages trimmed", "%s", fmtCount(stats.trimBatchSum));
        row("average batch", "%llu pages/region", safeDiv(stats.trimBatchSum, stats.trimBatchCount));
        row("empty regions", "%s  (%.2f%%)", fmtCount(stats.trimEmptyRegions),
            pct(stats.trimEmptyRegions, stats.trimBatchCount));
        row("skipped (was accessed)", "%s  (%.2f%% of ptes seen)", fmtCount(stats.trimSkippedAccessed),
            pct(stats.trimSkippedAccessed, stats.trimSkippedAccessed + stats.trimBatchSum));
        row("quota met", "%s of %s cycles", fmtCount(stats.quotaMet),
            fmtCount(stats.quotaMet + stats.quotaShort));
        row("recalled from local", "%s over %s wakeups", fmtCount(stats.recalled),
            fmtCount(stats.recallWakeups));

        printf("\n  batch size (pages per region)\n");
        histogram(stats.trimBatchHist, FALSE, ticksPerUs);

        //
        // The age a pte had when we took its page away. This is the quality score for the ager: a
        // distribution bunched at MAX_AGE means we are trimming genuinely cold pages.
        //
        {
            double weighted = 0.0;
            ULONG64 max = 0;
            char label[32];

            for (int age = 0; age < NUMBER_OF_AGES; age++) {
                weighted += (double) age * (double) stats.trimmedAge[age];
                if (stats.trimmedAge[age] > max) {
                    max = stats.trimmedAge[age];
                }
            }

            printf("\n  age of trimmed ptes   (average %.2f of %d)\n",
                   trimmedTotal == 0 ? 0.0 : weighted / (double) trimmedTotal, MAX_AGE);

            for (int age = 0; age < NUMBER_OF_AGES; age++) {
                snprintf(label, sizeof(label), "age %d", age);
                rowBar(label, stats.trimmedAge[age], trimmedTotal, max);
            }
        }

        //
        // Regions the trimmer picked, by the age list they came off
        //
        rule("AGE LISTS");
        {
            ULONG64 max = 0;
            char label[32];

            for (int age = 0; age < NUMBER_OF_AGES; age++) {
                if (stats.victimFromAge[age] > max) {
                    max = stats.victimFromAge[age];
                }
            }

            printf("  victim regions by source age list\n");
            for (int age = MAX_AGE; age >= 0; age--) {
                snprintf(label, sizeof(label), "age %d", age);
                rowBar(label, stats.victimFromAge[age], stats.trimBatchCount, max);
            }
            printf("\n");
            row("nothing to trim", "%s times", fmtCount(stats.victimNotFound));
        }

        //
        // Writer
        //
        rule("WRITER");
        row("write batches", "%s", fmtCount(stats.writeBatchCount));
        row("pages written", "%s", fmtCount(stats.writeBatchSum));
        row("average batch", "%llu pages  (cap %d)", safeDiv(stats.writeBatchSum, stats.writeBatchCount),
            BATCH_SIZE);
        row("disk full", "%s times", fmtCount(stats.diskFull));
        row("modified list empty", "%s times", fmtCount(stats.modifiedEmpty));
        printf("\n  batch size\n");
        histogram(stats.writeBatchHist, FALSE, ticksPerUs);

        //
        // Contention
        //
        rule("CONTENTION");
        row("list ops", "%s", fmtCount(stats.listShared + stats.listFallback));
        row("srw exclusive fallback", "%s  (%.2f%%)", fmtCount(stats.listFallback),
            pct(stats.listFallback, stats.listShared + stats.listFallback));
        row("free list searches", "%s", fmtCount(stats.freeListSearches));
        row("probes per search", "%.2f",
            stats.freeListSearches == 0
                ? 0.0
                : (double) stats.freeListProbes / (double) stats.freeListSearches);
        row("standby prunes", "%s", fmtCount(stats.pruneWakeups));
        row("prune yield", "%s of %s requested  (%.2f%%)", fmtCount(stats.pruneObtained),
            fmtCount(stats.pruneRequested), pct(stats.pruneObtained, stats.pruneRequested));

        //
        // Live age distribution, straight off the counters the ager already maintains
        //
        rule("LIVE AGE DISTRIBUTION");
        agedTotal = 0;
        for (int age = 0; age < NUMBER_OF_AGES; age++) {
            LONG64 count = vm.pte.globalNumOfAge[age].count;
            if (count > 0) {
                agedTotal += (ULONG64) count;
            }
        }
        {
            ULONG64 max = 0;
            char label[32];

            for (int age = 0; age < NUMBER_OF_AGES; age++) {
                LONG64 count = vm.pte.globalNumOfAge[age].count;
                if (count > 0 && (ULONG64) count > max) {
                    max = (ULONG64) count;
                }
            }

            for (int age = 0; age < NUMBER_OF_AGES; age++) {
                LONG64 count = vm.pte.globalNumOfAge[age].count;
                snprintf(label, sizeof(label), "age %d", age);
                rowBar(label, count > 0 ? (ULONG64) count : 0, agedTotal, max);
            }
        }
    }
#endif

    printf("\n");
    repeatGlyph(GLYPH_HEAVY, REPORT_WIDTH);
    printf("\n\n");
}
