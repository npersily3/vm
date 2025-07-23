//
// Created by nrper on 7/21/2025.
//

#ifndef STATISTICS_UTILS_H
#define STATISTICS_UTILS_H

#include "variables/globals.h"

VOID recordAccess(ULONG64 va);
PULONG64 calculateDeltaAccessTimes(PULONG64 changeInTimestamps, PULONG64 actualTimes, ULONG64 currentIndex);
double lnApproximation(double x);
double calculateVolatilityandDrift(PTE_REGION* region);
double mean(PULONG64 numbers, ULONG64 number_of_entries);
double variance(PULONG64 numbers, ULONG64 number_of_entries, double mean);
double getSlope(PULONG64 intervals, ULONG64 count);
#endif //STATISTICS_UTILS_H
