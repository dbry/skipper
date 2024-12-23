////////////////////////////////////////////////////////////////////////////
//                            **** SKIPPER ****                           //
//                  Selective Audio Detection and Filter                  //
//                    Copyright (c) 2024 David Bryant.                    //
//                          All Rights Reserved.                          //
//      Distributed under the BSD Software License (see license.txt)      //
////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

struct analysis_result {
    unsigned char range_dB, cycles;
    unsigned char low_third, mid_third, high_third;
    unsigned char attack_ratio, peak_jitter;
    unsigned char spare;
};

#define ARRAY_BINS_1    48
#define ARRAY_BINS_2    24
#define ARRAY_BINS_3    16
#define ARRAY_BINS_4    16

typedef signed char tensor_array [ARRAY_BINS_1] [ARRAY_BINS_2] [ARRAY_BINS_3] [ARRAY_BINS_4];

#define TENSOR_VERSION  1

struct tensor_header {
    uint32_t version, checksum;
    unsigned char dimensions [4];
};

static void analysis_result_to_tensor_index (const struct analysis_result *result, int *h, int *i, int *j, int *k)
{
    int h_index = result->range_dB >> 0;
    int i_index = result->cycles >> 1;
    int j_index = result->low_third >> 4;
    int k_index = result->mid_third >> 4;

    if (h_index >= ARRAY_BINS_1) h_index = ARRAY_BINS_1 - 1;
    if (i_index >= ARRAY_BINS_2) i_index = ARRAY_BINS_2 - 1;
    if (j_index >= ARRAY_BINS_3) j_index = ARRAY_BINS_3 - 1;
    if (k_index >= ARRAY_BINS_4) k_index = ARRAY_BINS_4 - 1;

    *h = h_index;
    *i = i_index;
    *j = j_index;
    *k = k_index;
}

static signed char *analysis_result_to_tensor_pointer (const struct analysis_result *result, tensor_array tensor)
{
    int h_index = result->range_dB >> 0;
    int i_index = result->cycles >> 1;
    int j_index = result->low_third >> 4;
    int k_index = result->mid_third >> 4;

    if (h_index >= ARRAY_BINS_1) h_index = ARRAY_BINS_1 - 1;
    if (i_index >= ARRAY_BINS_2) i_index = ARRAY_BINS_2 - 1;
    if (j_index >= ARRAY_BINS_3) j_index = ARRAY_BINS_3 - 1;
    if (k_index >= ARRAY_BINS_4) k_index = ARRAY_BINS_4 - 1;

    return &tensor [h_index] [i_index] [j_index] [k_index];
}
