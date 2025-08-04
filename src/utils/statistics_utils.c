// //
// // Created by nrper on 7/21/2025.
// //
//
// #include "../../include/utils/statistics_utils.h"
//
// #include <math.h>
// #include <stdio.h>
//
// #include "utils/pte_utils.h"
// #include "variables/structures.h"
//
//
// VOID recordAccess(ULONG64 arbitrary_va) {
//
//     PTE_REGION* region;
//     pte* currentPTE;
//     ULONG64 index;
//     stochastic_data* data;
//
//     currentPTE = va_to_pte(arbitrary_va);
//     region = getPTERegion(currentPTE);
//     data = &region->statistics;
//
//
//     region->accessed = TRUE;
//
//     index = InterlockedIncrement64((volatile LONG64 *) &data->currentStamp) - 1;
//     index %= NUMBER_OF_TIME_STAMPS;
//
//
//     LARGE_INTEGER timestamp;
//     QueryPerformanceCounter(&timestamp);
//
//     data->timeStamps[index] = timestamp.QuadPart;
// }
// double likelihoodOfAccess(PTE_REGION* region) {
//     double hazard;
//
//     hazard = hazardRate(region);
//     printf("hazardRate: %f\n", hazard);
//
//
//     return 1- exp(-hazard * LENGTH_OF_PREDICTION);
//
// }
//
// double hazardRate (PTE_REGION* region) {
//     stochastic_data data;
//
//     double volatility;
//     double drift;
//     double average;
//     double time_since_last_access;
//     double time_intervals[NUMBER_OF_TIME_STAMPS - 1];
//     double logHazardRate;
//
//     data = region->statistics;
//
//
//     calculateDeltaAccessTimes(time_intervals, data.timeStamps, data.currentStamp);
//     average = mean(time_intervals, NUMBER_OF_TIME_STAMPS - 1);
//
//
//     volatility = calculateVolatility(time_intervals, average);
//     drift = calculateDrift(time_intervals, average);
//     time_since_last_access = calculateMostRecentAccessTime(data.timeStamps[data.currentStamp % NUMBER_OF_TIME_STAMPS]);
//
//
//
//     logHazardRate = LOG_BASELINE_HAZARD +
//         DRIFT_COEFFICENT * drift +
//         VOLATILITY_COEFFICENT * volatility +
//             RECENT_ACCESS_COEFFICIENT * (double) time_since_last_access;
//     DebugBreak();
//     return exp(logHazardRate);
//
// }
//
// double lnApproximation(double x) {
//     return (x-1) - .5 * (x-1)*(x-1);
// }
//
// double calculateVolatility(double* timeIntervals, double average) {
//     double localVariance;
//     double volatility;
//
//     localVariance = variance(timeIntervals, NUMBER_OF_TIME_STAMPS - 1, average);
//     volatility = sqrt(localVariance);
//
//     return volatility;
// }
// double calculateDrift(double* timeIntervals, double average) {
//
//     double slope;
//     double drift;
//
//     slope = getSlope(timeIntervals, NUMBER_OF_TIME_STAMPS - 1);
//     drift = -(slope / average);
//
//     return drift;
// }
// double calculateMostRecentAccessTime(double mostRecentTime) {
//     LARGE_INTEGER timestamp;
//     QueryPerformanceCounter(&timestamp);
//
//     LARGE_INTEGER frequency;
//     QueryPerformanceFrequency(&frequency);
//
//     double output;
//
//     output = (((double) timestamp.QuadPart) / frequency.QuadPart) - mostRecentTime / (double) frequency.QuadPart;
//     DebugBreak();
//     return output;
// }
//
// double getSlope(double* intervals, ULONG64 count) {
//
//
//     double sum_x = 0;
//     double sum_y = 0;
//     double sum_xy = 0;
//     double sum_x2 = 0;
//
//     double x;
//     double y;
//     for (int i = 0; i < count; i++) {
//          x = (double)i;           // Time index
//          y = (double) intervals[i];       // Interval length
//
//         sum_x += x;
//         sum_y += y;
//         sum_xy += x * y;
//         sum_x2 += x * x;
//     }
//
//     double n = (float)count;
//     double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
//
//     return slope;
//
// }
//
// // assume index is greater that sixteen
// PULONG64 calculateDeltaAccessTimes(double* changeInTimestamps,PULONG64 actualTimes, ULONG64 currentIndex) {
//
//
//     ULONG64 nextIndex = 0;
//     ULONG64 counter;
//     ULONG64 index = (currentIndex %  NUMBER_OF_TIME_STAMPS) + 1;
//     counter = 0;
//     ULONG64 delta;
//     LARGE_INTEGER frequency;
//     QueryPerformanceFrequency(&frequency);
//
//     while (index != currentIndex && counter < NUMBER_OF_TIME_STAMPS - 1) {
//
//         nextIndex = (index + 1) % NUMBER_OF_TIME_STAMPS;
//
//         delta = actualTimes[nextIndex] - actualTimes[index];
//
//         changeInTimestamps[counter] = delta/frequency.QuadPart;
//
//         index = nextIndex;
//         counter++;
//     }
//
//
//     return changeInTimestamps;
//
//
// }
//
// // accounts for overflow by repeatetly doing a weighted average
// double mean(double* numbers, ULONG64 number_of_entries) {
//     double mean;
//     int i;
//
//     mean = numbers[0];
//
//
//     for (i = 1; i < number_of_entries; i++) {
//         mean += (double) numbers[i];
//         //mean = (mean * ((i)/(i+1.0)))  +  (numbers[i] * (1.0 / (i+1.0)));
//     }
//     return mean / (double) number_of_entries;
// }
// double variance(double* numbers, ULONG64 number_of_entries, double mean) {
//     double variance;
//
//     variance = 0;
//
//     for (int i = 0; i < number_of_entries; i++) {
//         variance += (numbers[i] - mean) * (numbers[i] - mean);
//     }
//
//     variance = variance / (number_of_entries - 1);
//
//     return variance;
// }
//
