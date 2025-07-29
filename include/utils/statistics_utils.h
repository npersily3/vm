//
// Created by nrper on 7/21/2025.
//

#ifndef STATISTICS_UTILS_H
#define STATISTICS_UTILS_H

#include "variables/globals.h"

VOID recordAccess(ULONG64 va);
PULONG64 calculateDeltaAccessTimes(double* changeInTimestamps, PULONG64 actualTimes, ULONG64 currentIndex);
double lnApproximation(double x);
double calculateVolatility(double* timeIntervals, double average);
double calculateDrift(double* timeIntervals, double average);
double calculateMostRecentAccessTime(double mostRecentTime);
double mean(double* numbers, ULONG64 number_of_entries);
double variance(double* numbers, ULONG64 number_of_entries, double mean);
double getSlope(double* intervals, ULONG64 count);
double hazardRate (PTE_REGION* region) ;
double likelihoodOfAccess(PTE_REGION* region) ;




#endif //STATISTICS_UTILS_H
