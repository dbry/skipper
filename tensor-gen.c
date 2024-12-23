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

#include "skipper.h"
#include "lzwlib.h"

static const char *sign_on = "\n"
" TENSOR-GEN  Tensor Generator for Skipper  Version 0.1\n"
" Copyright (c) 2024 David Bryant. All Rights Reserved.\n\n";

static const char *usage =
" Usage:     TENSOR-GEN [-options] music.bin talk.bin [out.tensor]\n\n"
" Operation: combine two raw results files (generated by SKIPPER -a)\n"
"            to create a compressed discriminator file, using\n"
"            either 1, 2, 3, or 4 dimensions\n\n"
" Options:  -a            = alternate windows between analysis & test\n"
"           -d<n>         = dimension count (1-4)\n\n"
" Web:      Visit www.github.com/dbry/skipper for latest version and info\n\n";

struct distribution {
    int dist_array [ARRAY_BINS_1] [ARRAY_BINS_2] [ARRAY_BINS_3] [ARRAY_BINS_4];
};

static tensor_array tensor, new_tensor;
static struct distribution dist1;
static struct distribution dist2;

static int array_bins_1 = ARRAY_BINS_1;
static int array_bins_2 = ARRAY_BINS_2;
static int array_bins_3 = ARRAY_BINS_3;
static int array_bins_4 = ARRAY_BINS_4;

static int alternate, dimensions;

static void display_2D_tensor (tensor_array tensor);
static int read_analysis_results (FILE *file, struct distribution *dist);
static void write_tensor_file (tensor_array tensor, char *filename);

int main (int argc, char **argv)
{
    char *filenames [3] = { NULL };
    FILE *files [3];

    // loop through command-line arguments

    while (--argc) {
#if defined (_WIN32)
        if ((**++argv == '-' || **argv == '/') && (*argv)[1])
#else
        if ((**++argv == '-') && (*argv)[1])
#endif
            while (*++*argv)
                switch (**argv) {
                    case 'A': case 'a':
                        alternate = 1;
                        break;

                    case 'D': case 'd':
                        dimensions = strtol (++*argv, argv, 10);

                        if (dimensions < 1 || dimensions > 4) {
                            fprintf (stderr, "\ndimensions must be 1 to 4!\n");
                            return -1;
                        }

                        --*argv;
                        break;

                    default:
                        fprintf (stderr, "\nillegal option: %c !\n", **argv);
                        return 1;
                }
        else if (!filenames [0]) {
            filenames [0] = malloc (strlen (*argv) + 10);
            strcpy (filenames [0], *argv);
        }
        else if (!filenames [1]) {
            filenames [1] = malloc (strlen (*argv) + 10);
            strcpy (filenames [1], *argv);
        }
        else if (!filenames [2]) {
            filenames [2] = malloc (strlen (*argv) + 10);
            strcpy (filenames [2], *argv);
        }
        else {
            fprintf (stderr, "\nextra unknown argument: %s !\n", *argv);
            return 1;
        }
    }

    if (!filenames [1]) {
        fprintf (stderr, "%s", sign_on);
        fprintf (stderr, "%s", usage);
        return 0;
    }

    switch (dimensions) {
        case 1:
            array_bins_2 = 1;
        case 2:
            array_bins_3 = 1;
        case 3:
            array_bins_4 = 1;
        default:
            break;
    }

    for (int i = 0; i < 2; ++i) {
        files [i] = fopen (filenames [i], "rb");

        if (!files [i]) {
            fprintf (stderr, "can't open file \"%s\" for reading!\n", filenames [i]);
            exit (1);
        }
    }

    int window_count1 = read_analysis_results (files [0], &dist1);
    int window_count2 = read_analysis_results (files [1], &dist2);

    for (int i = 0; i < 2; ++i)
        fclose (files [i]);

    int unique_hits1 = 0, unique_hits2 = 0, unique_slots1 = 0, unique_slots2 = 0;
    int guess_hits1 = 0, guess_hits2 = 0, guess_misses1 = 0, guess_misses2 = 0;
    int common_slots = 0, common_hits1 = 0, common_hits2 = 0;

    for (int h = 0; h < array_bins_1; ++h)
        for (int i = 0; i < array_bins_2; ++i)
            for (int j = 0; j < array_bins_3; ++j)
                for (int k = 0; k < array_bins_4; ++k) {
                    if (dist1.dist_array [h] [i] [j] [k] && !dist2.dist_array [h] [i] [j] [k]) {
                        unique_hits1 += dist1.dist_array [h] [i] [j] [k];
                        tensor [h] [i] [j] [k] = +99;
                        unique_slots1++;
                    }
                    else if (!dist1.dist_array [h] [i] [j] [k] && dist2.dist_array [h] [i] [j] [k]) {
                        unique_hits2 += dist2.dist_array [h] [i] [j] [k];
                        tensor [h] [i] [j] [k] = -99;
                        unique_slots2++;
                    }
                    else if (dist1.dist_array [h] [i] [j] [k] && dist2.dist_array [h] [i] [j] [k]) {
                        double file1_weight = (double) dist1.dist_array [h] [i] [j] [k] / window_count1;
                        double file2_weight = (double) dist2.dist_array [h] [i] [j] [k] / window_count2;

                        if (file1_weight > file2_weight) {
                            file2_weight /= file1_weight;
                            file1_weight = 1.0;
                        }
                        else {
                            file1_weight /= file2_weight;
                            file2_weight = 1.0;
                        }

                        if (file1_weight > file2_weight) {
                            guess_misses2 += dist2.dist_array [h] [i] [j] [k];
                            guess_hits1 += dist1.dist_array [h] [i] [j] [k];
                        }
                        else {
                            guess_misses1 += dist1.dist_array [h] [i] [j] [k];
                            guess_hits2 += dist2.dist_array [h] [i] [j] [k];
                        }

                        common_hits1 += dist1.dist_array [h] [i] [j] [k];
                        common_hits2 += dist2.dist_array [h] [i] [j] [k];
                        tensor [h] [i] [j] [k] = (int) floor (file1_weight * 99 + file2_weight * -99 + 0.5);
                        common_slots++;
                    }
                }

    fprintf (stderr, "file1: %d windows\n", window_count1);
    fprintf (stderr, "       %d unique hits in %d slots, %.1f%%\n", unique_hits1, unique_slots1, unique_hits1 * 100.0 / window_count1);
    fprintf (stderr, "       %d common hits in %d slots\n", common_hits1, common_slots);
    fprintf (stderr, "       %d guess hits in %d slots, %.1f%%\n", guess_hits1, common_slots, guess_hits1 * 100.0 / window_count1);
    fprintf (stderr, "       %d guess misses in %d slots, %.1f%%\n", guess_misses1, common_slots, guess_misses1 * 100.0 / window_count1);
    fprintf (stderr, "       %d unique hits and %d guess hits = %.1f%%\n\n", unique_hits1, guess_hits1, (unique_hits1 + guess_hits1) * 100.0 / window_count1);
    fprintf (stderr, "file2: %d windows\n", window_count2);
    fprintf (stderr, "       %d unique hits in %d slots, %.1f%%\n", unique_hits2, unique_slots2, unique_hits2 * 100.0 / window_count2);
    fprintf (stderr, "       %d common hits in %d slots\n", common_hits2, common_slots);
    fprintf (stderr, "       %d guess hits in %d slots, %.1f%%\n", guess_hits2, common_slots, guess_hits2 * 100.0 / window_count2);
    fprintf (stderr, "       %d guess misses in %d slots, %.1f%%\n", guess_misses2, common_slots, guess_misses2 * 100.0 / window_count2);
    fprintf (stderr, "       %d unique hits and %d guess hits = %.1f%%\n\n", unique_hits2, guess_hits2, (unique_hits2 + guess_hits2) * 100.0 / window_count2);

    display_2D_tensor (tensor);

    while (1) {
        int total_slots = 0, used_slots = 0, border_slots = 0, total_border_hits = 0;

        memcpy (new_tensor, tensor, sizeof (new_tensor));

        for (int h = 0; h < array_bins_1; ++h)
            for (int i = 0; i < array_bins_2; ++i)
                for (int j = 0; j < array_bins_3; ++j)
                    for (int k = 0; k < array_bins_4; ++k) {

                        total_slots++;

                        if (tensor [h] [i] [j] [k])
                            used_slots++;
                        else {
                            int border_hits = 0, values_sum = 0;

                            for (int dh = -1; dh <= 1; dh++)
                                for (int di = -1; di <= 1; di++)
                                    for (int dj = -1; dj <= 1; dj++)
                                        for (int dk = -1; dk <= 1; dk++)
                                            if (h + dh >= 0 && h + dh < array_bins_1 && i + di >= 0 && i + di < array_bins_2 &&
                                                j + dj >= 0 && j + dj < array_bins_3 && k + dk >= 0 && k + dk < array_bins_4)
                                                    if (tensor [h + dh] [i + di] [j + dj] [k + dk]) {
                                                        values_sum += tensor [h + dh] [i + di] [j + dj] [k + dk];
                                                        border_hits++;
                                                    }
                            if (border_hits) {
                                if (!border_slots)
                                    fprintf (stderr, "first slot filled is tensor [%d] [%d] [%d] [%d], sum = %d, hits = %d\n",
                                        h, i, j, k, values_sum, border_hits);

                                new_tensor [h] [i] [j] [k] = (int) floor ((double) values_sum / border_hits + 0.5);
                                total_border_hits += border_hits;
                                border_slots++;
                            }
                        }
                    }

        fprintf (stderr, "%d / %d slots used, %.1f%%\n", used_slots, total_slots, used_slots * 100.0 / total_slots);

        if (border_slots) {
            fprintf (stderr, "%d border slots found, average hits = %.1f\n", border_slots, (double) total_border_hits / border_slots);
            fprintf (stderr, "%d / %d used + border slots, %.1f%%\n", used_slots + border_slots, total_slots, (used_slots + border_slots) * 100.0 / total_slots);

            if (memcmp (tensor, new_tensor, sizeof (tensor))) {
                memcpy (tensor, new_tensor, sizeof (tensor));
                fprintf (stderr, "updated tensor\n\n");
            }
            else {
                fprintf (stderr, "nothing changed!\n\n");
                break;
            }
        }
        else {
            fprintf (stderr, "no border slots found!\n\n");
            break;
        }
    }

    for (int h = 0; h < ARRAY_BINS_1; ++h)
        for (int i = 0; i < ARRAY_BINS_2; ++i)
            for (int j = 0; j < ARRAY_BINS_3; ++j)
                for (int k = 0; k < ARRAY_BINS_4; ++k)
                    tensor [h] [i] [j] [k] = tensor
                        [h * (h < array_bins_1)]
                        [i * (i < array_bins_2)]
                        [j * (j < array_bins_3)]
                        [k * (k < array_bins_4)];

    display_2D_tensor (tensor);

    if (filenames [2])
        write_tensor_file (tensor, filenames [2]);

    for (int i = 0; i < 2; ++i) {
        int window_count = 0, file1_hits = 0, file2_hits = 0;
        struct analysis_result result;

        files [i] = fopen (filenames [i], "rb");

        if (!files [i]) {
            fprintf (stderr, "can't open file \"%s\" for reading!\n", filenames [i]);
            exit (1);
        }

        while (fread (&result, sizeof (result), 1, files [i])) {
            signed char tensor_value = *analysis_result_to_tensor_pointer (&result, tensor);

            if (!alternate || !(window_count & 1)) {
                if (tensor_value > 0)
                    file1_hits += alternate + 1;
                else if (tensor_value < 0)
                    file2_hits += alternate + 1;
            }

            window_count++;
        }

        fprintf (stderr, "read %d windows, file1 hits = %d (%.1f%%), file2 hits = %d (%.1f%%), ??? = %d (%.1f%%)\n", window_count,
            file1_hits, file1_hits * 100.0 / window_count, file2_hits, file2_hits * 100.0 / window_count,
            window_count - file1_hits - file2_hits, (window_count - file1_hits - file2_hits) * 100.0 / window_count);

        fclose (files [i]);
    }

    return 0;
}

static void display_2D_tensor (tensor_array tensor)
{
    char string [256] = "";

    for (int k = 0; k < array_bins_2; ++k)
        sprintf (string + strlen (string), " %3d", k);

    fprintf (stderr, "cycles: %s\n", string);
    string [0] = 0;

    for (int k = 0; k < array_bins_2; ++k)
        strcat (string, "----");

    fprintf (stderr, "-------  %s\n", string);

    for (int h = 0; h < array_bins_1; ++h) {
        string [0] = 0;

        for (int i = 0; i < array_bins_2; ++i)
            sprintf (string + strlen (string), " %3d", tensor [h] [i] [array_bins_3/2] [array_bins_4/2]);

        fprintf (stderr, "%2d dB:  %s\n", h, string);
    }

    fprintf (stderr, "\n");
}

static int read_analysis_results (FILE *file, struct distribution *dist)
{
    struct analysis_result result;
    int window_count = 0;

    while (fread (&result, sizeof (result), 1, file)) {
        int h, i, j, k;

        analysis_result_to_tensor_index (&result, &h, &i, &j, &k);

        if (h >= array_bins_1) h = array_bins_1 - 1;
        if (i >= array_bins_2) i = array_bins_2 - 1;
        if (j >= array_bins_3) j = array_bins_3 - 1;
        if (k >= array_bins_4) k = array_bins_4 - 1;

        if (!alternate || (window_count & 1))
            dist->dist_array [h] [i] [j] [k] += alternate + 1;

        window_count++;
    }

    fprintf (stderr, "read %d windows\n", window_count);

    return window_count;
}

typedef struct {
    unsigned int size, index, wrapped;
    unsigned char *buffer;
} streamer;

static int read_buff (void *ctx)
{
    streamer *stream = ctx;

    if (stream->index == stream->size)
        return EOF;

    return stream->buffer [stream->index++];
}

static void write_buff (int value, void *ctx)
{
    streamer *stream = ctx;

    if (stream->index == stream->size) {
        stream->index = 0;
        stream->wrapped++;
    }

    stream->buffer [stream->index++] = value;
}

static void write_tensor_file (tensor_array tensor, char *filename)
{
    unsigned char dimensions [4] = { ARRAY_BINS_1, ARRAY_BINS_2, ARRAY_BINS_3, ARRAY_BINS_4 };
    int best_maxbits, smallest_output = sizeof (tensor_array) + 1;
    FILE *tensor_file = fopen (filename, "wb");
    struct tensor_header header;
    streamer reader, writer;

    if (!tensor_file) {
        fprintf (stderr, "error: can't open \"%s\" for writing!\n", filename);
        return;
    }

    memset (&header, 0, sizeof (header));
    memset (&reader, 0, sizeof (reader));
    memset (&writer, 0, sizeof (writer));

    for (int i = 0; i < sizeof (tensor_array); ++i)
        header.checksum += ((unsigned char *) tensor) [i];

    memcpy (header.dimensions, dimensions, sizeof (dimensions));
    header.version = TENSOR_VERSION;

    fwrite (&header, sizeof (header), 1, tensor_file);

    reader.buffer = (unsigned char *) tensor;
    reader.size = sizeof (tensor_array);

    writer.buffer = malloc (writer.size = reader.size);

    for (int maxbits = 9; maxbits <= 16; ++maxbits) {
        writer.index = reader.index = 0;

        if (lzw_compress (write_buff, &writer, read_buff, &reader, maxbits)) {
            fprintf (stderr, "lzw_decompress() returned error!\n");
            return;
        }

        if (writer.index < smallest_output) {
            smallest_output = writer.index;
            best_maxbits = maxbits;
        }
    }

    writer.index = reader.index = 0;

    if (lzw_compress (write_buff, &writer, read_buff, &reader, best_maxbits)) {
        fprintf (stderr, "lzw_decompress() returned error!\n");
        return;
    }

    fprintf (stderr, "tensor checksum = %d, stored with maxbits %d in %d bytes (ratio = %.1f%%)\n",
        header.checksum, best_maxbits, writer.index, writer.index * 100.0 / sizeof (tensor_array));

    fwrite (writer.buffer, writer.index, 1, tensor_file);
    fclose (tensor_file);
    free (writer.buffer);
}