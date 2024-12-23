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
#include <ctype.h>
#include <math.h>

#ifdef _WIN32
#include <fcntl.h>
#endif

#include "4d-tensor.h"
#include "skipper.h"
#include "lzwlib.h"
#include "biquad.h"

#define VERSION         0.1

#define OUTPUT_AUDIO    0
#define OUTPUT_MONO     1
#define OUTPUT_FILTERED 2
#define OUTPUT_LEVEL    3
#define OUTPUT_TENSOR   4

#define SKIP_NOTHING    0
#define SKIP_TALK       1
#define SKIP_MUSIC      2
#define SKIP_EVERYTHING 3

#define MODE_NOTHING    0
#define MODE_MUSIC      1
#define MODE_TALK       -1

static const char *sign_on = "\n"
" SKIPPER  Selective Audio Detection and Filter  Version %.1f\n"
" Copyright (c) 2024 David Bryant. All Rights Reserved.\n\n";

static const char *usage =
" Usage:     SKIPPER [-options] < SourceAudio.pcm > StereoOutput.pcm\n\n"
" Operation: scan source audio (stdin) using tensor discrimination to filter\n"
"            output (stdout), skipping either music (-m) or talk (-t); or\n"
"            output raw scan analytics for use with TENSOR-GEN util (-a)\n\n"
" Options:  -a <file.bin>    = output analysis results to specified file\n"
"           -c<n>            = override default channel count of 2\n"
"           -d <file.tensor> = specify alternate discrimination tensor file\n"
"           -k               = keep-alive crossfading for long skips\n"
"           -l<n>            = left output override (for debug, n = 1-4:\n"
"                            = 1=mono, 2=filtered, 3=level, 4=tensor)\n"
"           -m[<n>]          = skip over music, with optional threshold offset\n"
"                            = (raise or lower music threshold +/- 99 points)\n"
"           -n               = no audio output (skip everything)\n"
"           -p               = pass all audio (no skipping, default)\n"
"           -q               = no messaging except errors\n"
"           -r<n>            = right output override (for debug, n = 1-4:\n"
"                            = 1=mono, 2=filtered, 3=level, 4=tensor)\n"
"           -s<n>            = override default sample rate of 44.1 kHz\n"
"           -t[<n>]          = skip over talk, with optional threshold offset\n"
"                            = (raise or lower talk threshold +/- 99 points)\n"
"           -v[<n>]          = set verbosity + [rate in seconds]\n\n"
" Web:      Visit www.github.com/dbry/skipper for latest version and info\n\n";

#define CHANNELS        2       // default, overridable
#define SAMPLE_RATE     44100   // default, overridable

#define LEVEL_WIN_MS    50
#define WINDOW_SECONDS  5
#define AVERAGE_SECONDS 5
#define STEP_MSECS      200
#define AVERAGE_COUNT   (AVERAGE_SECONDS*1000/STEP_MSECS)

#define CROSSFADE_SECS  2
#define MIN_TALK_SECS   10
#define MIN_MUSIC_SECS  20
#define MAX_PEND_SECS   60
#define OUTPUT_SECONDS  120

#define LOWPASS_FREQ    2000.0
#define HIGHPASS_FREQ   250.0

#define MAX_CYCLES      128

static void fade_out (int16_t *samples, int num_samples, int stride);
static void fade_in (int16_t *samples, int num_samples, int stride);

static int read_tensor_file (tensor_array tensor, char *filename);
static int local_tensor_file (tensor_array tensor, unsigned char *compressed_tensor, int compressed_size);
static int analyze_window (float *levels, long sample_index, int num_samples, int sample_rate);
static void display_histogram (const char *name, int *histogram, int count);
static void display_analysis_results (void);

static tensor_array tensor;
static FILE *analysis_output_file;
static int verbose, quiet;

#define MINS(s,r) ((int)((s)/((r)*60)))
#define SECS(s,r) ((int)(((s)/(r))%60))

int main (int argc, char **argv)
{
    int channels = CHANNELS, sample_rate = SAMPLE_RATE, keepalive = 0;
    int left_output = 0, right_output = 0, skip_mode = 0, threshold = 0;
    int level_buffer_index = 0, output_buffer_index = 0, num_windows = 0, step_samples;
    int level_buff_len, output_buff_len, crossfade_buff_len, ring_buff_len, results_buffer_count = 0;
    int music_hits = 0, talk_hits = 0, analysis_output_file_follows = 0, tensor_input_file_follows = 0;
    int current_mode = 0, music_up_counter = 0, talk_up_counter = 0, pend_up_counter = 0, input_samples;
    int64_t num_samples = 0, transition_sample = 0, confirmed_sample = 0, samples_discarded = 0, samples_written = 0;
    char *analysis_output_filename = NULL, *tensor_input_filename = NULL;
    int16_t *input_buffer, *output_buffer, *crossfade_buffer;
    double full_scale_rms = 32768.0 * 32767.0 * 0.5;
    float *fsamples, *level_buffer, *ring_buffer;
    signed char results_buffer [AVERAGE_COUNT];
    Biquad lowpass [2], highpass [2];
    BiquadCoefficients coefficients;
    uint32_t random = 0x31415926;
    double level = 0.0;

    if (argc == 1) {
        fprintf (stderr, sign_on, VERSION);
        fprintf (stderr, "%s", usage);
        return 0;
    }

#ifdef _WIN32
    setmode (fileno (stdout), O_BINARY);
    setmode (fileno (stdin), O_BINARY);
#endif

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
                        analysis_output_file_follows = 1;
                        break;

                    case 'C': case 'c':
                        channels = strtol (++*argv, argv, 10);

                        if (channels < 1 || channels > 2) {
                            fprintf (stderr, "\nerror: channels must be 1 or 2\n");
                            return -1;
                        }

                        --*argv;
                        break;

                    case 'D': case 'd':
                        tensor_input_file_follows = 1;
                        break;

                    case 'K': case 'k':
                        keepalive = 1;
                        break;

                    case 'L': case 'l':
                        left_output = strtol (++*argv, argv, 10);

                        if (left_output < 0 || left_output > 4) {
                            fprintf (stderr, "\nerror: output spec must be 0 - 4\n");
                            return -1;
                        }

                        --*argv;
                        break;

                    case 'M': case 'm':
                        if (isdigit (*++*argv) || **argv == '-')
                            threshold = strtol (*argv, argv, 10);

                        if (threshold < -99 || threshold > 99) {
                            fprintf (stderr, "\nerror: threshold is from -99 (most music skipped) to 99 (least music skipped)\n");
                            return -1;
                        }

                        skip_mode = SKIP_MUSIC;
                        --*argv;
                        break;

                    case 'N': case 'n':
                        skip_mode = SKIP_EVERYTHING;
                        break;

                    case 'P': case 'p':
                        skip_mode = SKIP_NOTHING;
                        break;

                    case 'Q': case 'q':
                        quiet = 1;
                        break;

                    case 'R': case 'r':
                        right_output = strtol (++*argv, argv, 10);

                        if (right_output < 0 || right_output > 4) {
                            fprintf (stderr, "\nerror: output spec must be 0 - 4\n");
                            return -1;
                        }

                        --*argv;
                        break;

                    case 'S': case 's':
                        sample_rate = strtol (++*argv, argv, 10);

                        if (sample_rate < 11025 || sample_rate > 96000) {
                            fprintf (stderr, "\nerror: invalid sample rate specified (11025 Hz - 96000 Hz only)\n");
                            return -1;
                        }

                        --*argv;
                        break;

                    case 'T': case 't':
                        if (isdigit (*++*argv) || **argv == '-')
                            threshold = -strtol (*argv, argv, 10);

                        if (threshold < -99 || threshold > 99) {
                            fprintf (stderr, "\nerror: threshold is from -99 (most talk skipped) to 99 (least talk skipped)\n");
                            return -1;
                        }

                        skip_mode = SKIP_TALK;
                        --*argv;
                        break;

                    case 'V': case 'v':
                        if (isdigit (*++*argv))
                            verbose = strtol (*argv, argv, 10);
                        else
                            verbose = 300;    // default is every 5 minutes

                        --*argv;
                        break;

                    default:
                        fprintf (stderr, "\nillegal option: %c !\n", **argv);
                        return 1;
                }
        else if (analysis_output_file_follows) {
            analysis_output_filename = *argv;
            analysis_output_file_follows = 0;
        }
        else if (tensor_input_file_follows) {
            tensor_input_filename = *argv;
            tensor_input_file_follows = 0;
        }
        else {
            fprintf (stderr, "\nextra unknown argument: %s !\n", *argv);
            return 1;
        }
    }

    if (tensor_input_filename ? !read_tensor_file (tensor, tensor_input_filename) : !local_tensor_file (tensor, tensor_4d, sizeof (tensor_4d))) {
        fprintf (stderr, "\nerror: no tensor file, exiting!\n");
        return 1;
    }

    if (analysis_output_filename) {
        analysis_output_file = fopen (analysis_output_filename, "wb");

        if (!analysis_output_file) {
            fprintf (stderr, "\nerror: can't open \"%s\" for writing!\n", analysis_output_filename);
            return 1;
        }
    }

    input_buffer = calloc (sample_rate, sizeof (int16_t) * channels);
    fsamples = calloc (sample_rate, sizeof (float));

    step_samples = STEP_MSECS * sample_rate / 1000;
    ring_buff_len = (sample_rate * LEVEL_WIN_MS + 500) / 1000;
    ring_buffer = calloc (ring_buff_len, sizeof (float));

    level_buff_len = WINDOW_SECONDS * sample_rate;
    level_buffer = calloc (level_buff_len, sizeof (float));

    output_buff_len = OUTPUT_SECONDS * sample_rate;
    output_buffer = calloc (output_buff_len, sizeof (int16_t) * 2);

    crossfade_buff_len = CROSSFADE_SECS * sample_rate;
    crossfade_buffer = calloc (crossfade_buff_len, sizeof (int16_t) * 2);

#ifdef HIGHPASS_FREQ
    biquad_highpass (&coefficients, HIGHPASS_FREQ / sample_rate);
    biquad_init (highpass + 0, &coefficients, 1.0);
    biquad_init (highpass + 1, &coefficients, 1.0);
#endif

#ifdef LOWPASS_FREQ
    biquad_lowpass (&coefficients, LOWPASS_FREQ / sample_rate);
    biquad_init (lowpass + 0, &coefficients, 1.0);
    biquad_init (lowpass + 1, &coefficients, 1.0);
#endif

    for (int i = 0; i < ring_buff_len; ++i)
        ring_buffer [i] = (int32_t)(random = ((random << 4) - random) ^ 1) >> 26;

#ifdef HIGHPASS_FREQ
    biquad_apply_buffer (highpass + 0, ring_buffer, ring_buff_len, 1);
    biquad_apply_buffer (highpass + 1, ring_buffer, ring_buff_len, 1);
#endif

#ifdef LOWPASS_FREQ
    biquad_apply_buffer (lowpass + 0, ring_buffer, ring_buff_len, 1);
    biquad_apply_buffer (lowpass + 1, ring_buffer, ring_buff_len, 1);
#endif

    while ((input_samples = fread (input_buffer, sizeof (int16_t) * channels, sample_rate, stdin))) {

        if (channels == 2)
            for (int j = 0; j < input_samples; j++)
                fsamples [j] = ((float) input_buffer [j * 2] + input_buffer [j * 2 + 1]) / 2.0 + ((int32_t)(random = ((random << 4) - random) ^ 1) >> 26);
        else
            for (int j = 0; j < input_samples; j++)
                fsamples [j] = (float) input_buffer [j] + ((int32_t)(random = ((random << 4) - random) ^ 1) >> 26);

#ifdef HIGHPASS_FREQ
        biquad_apply_buffer (highpass + 0, fsamples, input_samples, 1);
        biquad_apply_buffer (highpass + 1, fsamples, input_samples, 1);
#endif

#ifdef LOWPASS_FREQ
        biquad_apply_buffer (lowpass + 0, fsamples, input_samples, 1);
        biquad_apply_buffer (lowpass + 1, fsamples, input_samples, 1);
#endif

        for (int j = 0; j < input_samples; j++) {
            int ring_buff_index = num_samples % ring_buff_len;

            if (ring_buff_index == 0) {
                level = (ring_buffer [0] = fsamples [j]) * fsamples [j];

                for (int i = 1; i < ring_buff_len; ++i)
                    level += ring_buffer [i] * ring_buffer [i];
            }
            else {
                level -= ring_buffer [ring_buff_index] * ring_buffer [ring_buff_index];
                ring_buffer [ring_buff_index] = fsamples [j];
                level += ring_buffer [ring_buff_index] * ring_buffer [ring_buff_index];
            }

            level_buffer [level_buffer_index] = level / ring_buff_len;

            if (left_output == OUTPUT_AUDIO)
                output_buffer [output_buffer_index * 2] = input_buffer [j * channels];
            else if (left_output == OUTPUT_MONO)
                output_buffer [output_buffer_index * 2] = (input_buffer [j * channels] + input_buffer [j * channels + channels - 1]) >> 1;
            else if (left_output == OUTPUT_FILTERED)
                output_buffer [output_buffer_index * 2] = fsamples [j];
            else if (left_output == OUTPUT_LEVEL && output_buffer_index >= ring_buff_len / 2)
                output_buffer [(output_buffer_index - ring_buff_len / 2) * 2] = floor ((log10 (level_buffer [level_buffer_index] / full_scale_rms) + 9.6) * 3413 + 0.5);

            if (right_output == OUTPUT_AUDIO)
                output_buffer [output_buffer_index * 2 + 1] = input_buffer [j * channels + channels - 1];
            else if (right_output == OUTPUT_MONO)
                output_buffer [output_buffer_index * 2 + 1] = (input_buffer [j * channels] + input_buffer [j * channels + channels - 1]) >> 1;
            else if (right_output == OUTPUT_FILTERED)
                output_buffer [output_buffer_index * 2 + 1] = fsamples [j];
            else if (right_output == OUTPUT_LEVEL && output_buffer_index >= ring_buff_len / 2)
                output_buffer [(output_buffer_index - ring_buff_len / 2) * 2 + 1] = floor ((log10 (level_buffer [level_buffer_index] / full_scale_rms) + 9.6) * 3413 + 0.5);

            ++level_buffer_index;
            ++output_buffer_index;
            ++num_samples;

            if (level_buffer_index == level_buff_len) {
                int tensor_value = analyze_window (level_buffer, num_samples, level_buff_len, sample_rate), detected_mode = MODE_NOTHING;

                if (tensor_value > threshold)
                    music_hits++;
                else if (tensor_value < threshold)
                    talk_hits++;

                results_buffer [results_buffer_count++] = tensor_value;

                if (results_buffer_count == AVERAGE_COUNT) {
                    for (int i = tensor_value = 0; i < results_buffer_count; ++i)
                        tensor_value += results_buffer [i];

                    memmove (results_buffer, results_buffer + 1, AVERAGE_COUNT - 1);
                    results_buffer_count--;

                    if (left_output == OUTPUT_TENSOR || right_output == OUTPUT_TENSOR) {
                        int16_t *outbuff_window = output_buffer + output_buffer_index * 2;

                        outbuff_window -= WINDOW_SECONDS * sample_rate / 2 * 2;
                        outbuff_window -= AVERAGE_SECONDS * sample_rate / 2 * 2;
                        outbuff_window -= step_samples / 2 * 2;

                        if (outbuff_window >= output_buffer) {
                            int16_t value = (tensor_value * 100 + results_buffer_count / 2) / results_buffer_count;

                            for (int i = 0; i < step_samples; ++i) {
                                if (left_output == OUTPUT_TENSOR)
                                    outbuff_window [i * 2] = value - threshold * 100;
                                if (right_output == OUTPUT_TENSOR)
                                    outbuff_window [i * 2 + 1] = value - threshold * 100;
                            }
                        }
                    }

                    if (tensor_value > threshold * results_buffer_count) {
                        if (current_mode == MODE_MUSIC) {
                            if (talk_up_counter && --talk_up_counter) {
                                if (++pend_up_counter >= MAX_PEND_SECS * 1000 / STEP_MSECS) {
                                    if (verbose)
                                        fprintf (stderr, "TALK detection pending for %d secs, cancelled...\n",
                                            (pend_up_counter * STEP_MSECS + 500) / 1000);

                                    talk_up_counter = 0;
                                }
                            }
                        }
                        else {
                            if (!music_up_counter) {
                                transition_sample = num_samples - ((WINDOW_SECONDS + AVERAGE_SECONDS) * sample_rate) / 2;
                                pend_up_counter = 0;
                            }

                            if (++music_up_counter == MIN_MUSIC_SECS * 1000 / STEP_MSECS) {
                                detected_mode = MODE_MUSIC;
                                music_up_counter = 0;
                            }

                            pend_up_counter++;
                        }
                    }
                    else {
                        if (current_mode == MODE_TALK) {
                            if (music_up_counter && --music_up_counter) {
                                if (++pend_up_counter >= MAX_PEND_SECS * 1000 / STEP_MSECS) {
                                    if (verbose)
                                        fprintf (stderr, "MUSIC detection pending for %d secs, cancelled...\n",
                                            (pend_up_counter * STEP_MSECS + 500) / 1000);

                                    music_up_counter = 0;
                                }
                            }
                        }
                        else {
                            if (!talk_up_counter) {
                                transition_sample = num_samples - ((WINDOW_SECONDS + AVERAGE_SECONDS) * sample_rate) / 2;
                                pend_up_counter = 0;
                            }

                            if (++talk_up_counter == MIN_TALK_SECS * 1000 / STEP_MSECS) {
                                detected_mode = MODE_TALK;
                                talk_up_counter = 0;
                            }

                            pend_up_counter++;
                        }
                    }

                    if (detected_mode) {
                        if (skip_mode == SKIP_MUSIC || skip_mode == SKIP_TALK) {
                            int audio_offset = transition_sample - num_samples + output_buffer_index;
                            int crossfade_start = audio_offset - crossfade_buff_len / 2;

                            if (skip_mode == (detected_mode == MODE_MUSIC ? SKIP_MUSIC : SKIP_TALK)) {
                                if (crossfade_start >= 0) {
                                    fwrite (output_buffer, sizeof (int16_t) * 2, crossfade_start, stdout);
                                    samples_written += crossfade_start;
                                    memmove (output_buffer, output_buffer + crossfade_start * 2, (output_buff_len - crossfade_start) * sizeof (int16_t) * 2);
                                    output_buffer_index -= crossfade_start;

                                    if (verbose)
                                        fprintf (stderr, "fade out: wrote %d samples (%.1f secs), %.1f secs remaining in buffer\n",
                                            crossfade_start, (float) crossfade_start / sample_rate, (float) output_buffer_index / sample_rate);

                                    memcpy (crossfade_buffer, output_buffer, crossfade_buff_len * 4);
                                    fade_out (crossfade_buffer, crossfade_buff_len * 2, 1);
                                }
                                else {
                                    fprintf (stderr, "error: skipped transition, buffer out of range\n");
                                    exit (1);
                                }
                            }
                            else {
                                if (crossfade_start >= 0) {
                                    memmove (output_buffer, output_buffer + crossfade_start * 2, (output_buff_len - crossfade_start) * sizeof (int16_t) * 2);
                                    output_buffer_index -= crossfade_start;
                                    samples_discarded += crossfade_start;

                                    if (verbose)
                                        fprintf (stderr, "fade in: discarded %d samples (%.1f secs), %.1f secs remaining in buffer\n",
                                            crossfade_start, (float) crossfade_start / sample_rate, (float) output_buffer_index / sample_rate);

                                    if (!quiet)
                                        fprintf (stderr, "crossfade to %s at %02d:%02d\n", detected_mode == MODE_MUSIC ? "MUSIC" : "TALK",
                                            MINS (samples_written + crossfade_buff_len / 2, sample_rate), SECS (samples_written + crossfade_buff_len / 2, sample_rate));

                                    fade_in (output_buffer, crossfade_buff_len * 2, 1);

                                    for (int i = 0; i < crossfade_buff_len * 2; ++i) {
                                        int32_t sum = output_buffer [i] + crossfade_buffer [i];

                                        if (sum > 32767) output_buffer [i] = 32767;
                                        else if (sum < -32768) output_buffer [i] = -32768;
                                        else output_buffer [i] = sum;
                                    }
                                }
                                else {
                                    fprintf (stderr, "error: skipped transition, buffer out of range\n");
                                    exit (1);
                                }
                            }
                        }
                        else if (!quiet)
                            fprintf (stderr, "%02d:%02d: detected %s starting at %02d:%02d\n",
                                MINS (num_samples, sample_rate), SECS (num_samples, sample_rate), detected_mode == MODE_MUSIC ? "MUSIC" : " TALK",
                                MINS (transition_sample, sample_rate), SECS (transition_sample, sample_rate));

                        current_mode = detected_mode;
                    }

                    if (!talk_up_counter && !music_up_counter)
                        confirmed_sample = num_samples - ((WINDOW_SECONDS + AVERAGE_SECONDS) * sample_rate + step_samples + crossfade_buff_len) / 2;
                }

                memmove (level_buffer, level_buffer + step_samples, (WINDOW_SECONDS * sample_rate - step_samples) * sizeof (float));
                level_buffer_index -= step_samples;
                num_windows++;
            }

            int available_samples = confirmed_sample - num_samples + output_buffer_index + step_samples / 2;

            if (output_buffer_index == output_buff_len || available_samples >= sample_rate * 60) {

                if (keepalive && available_samples > crossfade_buff_len * 2 && skip_mode == (current_mode == MODE_MUSIC ? SKIP_MUSIC : SKIP_TALK)) {
                    int crossfade_start = available_samples / 2 - crossfade_buff_len;
                    int16_t *crossfade_ptr = output_buffer + crossfade_start * 2;

                    for (int i = 0; i < crossfade_buff_len * 4; ++i)
                        crossfade_ptr [i] >>= 2;

                    fade_in (crossfade_ptr, crossfade_buff_len * 2, 1);

                    for (int i = 0; i < crossfade_buff_len * 2; ++i)
                        crossfade_ptr [i] += crossfade_buffer [i];

                    fwrite (crossfade_ptr, sizeof (int16_t) * 2, crossfade_buff_len, stdout);
                    memcpy (crossfade_buffer, crossfade_ptr + crossfade_buff_len * 2, crossfade_buff_len * 4);
                    fade_out (crossfade_buffer, crossfade_buff_len * 2, 1);

                    samples_discarded += available_samples - crossfade_buff_len;
                    samples_written += crossfade_buff_len;

                    memmove (output_buffer, output_buffer + available_samples * 2, (output_buff_len - available_samples) * sizeof (int16_t) * 2);
                    output_buffer_index -= available_samples;

                    if (verbose)
                        fprintf (stderr, "discarded %d samples (%.1f secs), inserted a %s crossfade at %02d:%02d\n",
                            available_samples - crossfade_buff_len, (float) (available_samples - crossfade_buff_len) / sample_rate,
                            current_mode == MODE_MUSIC ? "MUSICAL" : "TALKING",
                            MINS (samples_written - crossfade_buff_len / 2, sample_rate),
                            SECS (samples_written - crossfade_buff_len / 2, sample_rate));
                    else if (!quiet)
                        fprintf (stderr, "%s keep-alive at %02d:%02d\n", current_mode == MODE_MUSIC ? "MUSICAL" : "TALKING",
                            MINS (samples_written - crossfade_buff_len / 2, sample_rate),
                            SECS (samples_written - crossfade_buff_len / 2, sample_rate));
                }
                else if (available_samples > 0) {
                    int write_data = skip_mode == SKIP_NOTHING || skip_mode == (current_mode == MODE_MUSIC ? SKIP_TALK : SKIP_MUSIC);

                    if (write_data) {
                        fwrite (output_buffer, sizeof (int16_t) * 2, available_samples, stdout);
                        samples_written += available_samples;
                    }
                    else
                        samples_discarded += available_samples;

                    memmove (output_buffer, output_buffer + available_samples * 2, (output_buff_len - available_samples) * sizeof (int16_t) * 2);
                    output_buffer_index -= available_samples;

                    if (verbose)
                        fprintf (stderr, "%s %d samples (%.1f secs), output_buffer_index now %d (%.1f secs), music/talk counts = %d/%d\n",
                            write_data ? "wrote" : "discarded", available_samples, (float) available_samples / sample_rate,
                            output_buffer_index, (float) output_buffer_index / sample_rate, music_up_counter, talk_up_counter);
                }
                else {
                    fprintf (stderr, "error: buffer full with no confirmed samples!\n");
                    exit (1);
                }
            }
        }
    }

    if (output_buffer_index) {
        int write_data = skip_mode == SKIP_NOTHING || skip_mode == (current_mode == MODE_MUSIC ? SKIP_TALK : SKIP_MUSIC);

        if (write_data) {
            fwrite (output_buffer, sizeof (int16_t) * 2, output_buffer_index, stdout);
            samples_written += output_buffer_index;
        }
        else
            samples_discarded += output_buffer_index;

        if (verbose)
            fprintf (stderr, "final: %s %d samples (%.1f secs), music/talk counts = %d/%d\n",
                write_data ? "wrote" : "discarded", output_buffer_index, (float) output_buffer_index / sample_rate,
                music_up_counter, talk_up_counter);
    }

    if (!quiet) {
        fprintf (stderr, "total input duration = %02d:%02d\n", MINS (num_samples, sample_rate), SECS (num_samples, sample_rate));

        if (verbose)
            fprintf (stderr, "total windows = %d\n", num_windows);

        fprintf (stderr, "raw music hits = %d (%.1f%%), raw talk hits = %d (%.1f%%), unknowns = %d (%.1f%%)\n",
            music_hits, music_hits * 100.0 / num_windows, talk_hits, talk_hits * 100.0 / num_windows,
            num_windows - music_hits - talk_hits, (num_windows - music_hits - talk_hits) * 100.0 / num_windows);
        fprintf (stderr, "audio written = %02d:%02d (%.1f%%), audio discarded = %02d:%02d (%.1f%%)\n\n",
            MINS (samples_written, sample_rate), SECS (samples_written, sample_rate), samples_written * 100.0 / (samples_written + samples_discarded),
            MINS (samples_discarded, sample_rate), SECS (samples_discarded, sample_rate), samples_discarded * 100.0 / (samples_written + samples_discarded));

        if (analysis_output_file)
            display_analysis_results ();
    }

    free (crossfade_buffer);
    free (output_buffer);
    free (level_buffer);
    free (ring_buffer);
    free (fsamples);
    free (input_buffer);

    if (analysis_output_file)
        fclose (analysis_output_file);

    return 0;
}

static void fade_out (int16_t *samples, int num_samples, int stride)
{
    for (int total_samples = num_samples; num_samples--; samples += stride)
        *samples = (int64_t) *samples * num_samples / total_samples;
}

static void fade_in (int16_t *samples, int num_samples, int stride)
{
    for (int total_samples = num_samples; num_samples--; samples += stride)
        *samples = (int64_t) *samples * (total_samples - num_samples) / total_samples;
}

static int peak_to_trough_histogram [96] = { 0 };
static int cycles_histogram [256] = { 0 };
static int low_third_histogram [256] = { 0 };
static int mid_third_histogram [256] = { 0 };
static int high_third_histogram [256] = { 0 };
static int attack_ratio_histogram [256] = { 0 };
static int peak_jitter_histogram [256] = { 0 };

static int analyze_window (float *levels, long sample_index, int num_samples, int sample_rate)
{
    double full_scale_rms = 32768.0 * 32767.0 * 0.5;
    float prev_peak = levels [0], prev_trough = levels [0];
    float peak = levels [0], trough = levels [0];
    int prev_peak_pos = 0, prev_trough_pos = 0;
    int zones [4] = { 0 }, cycles = 0;
    int trigger_points [MAX_CYCLES];
    struct analysis_result result;

    for (int i = 1; i < num_samples; ++i) {
        if (levels [i] < trough) trough = levels [i];
        if (levels [i] > peak) peak = levels [i];
    }

    double peak_to_trough_dB = log10 (peak / trough) * 10.0;
    double square_root = sqrt (peak / trough);
    double cube_root = cbrt (peak / trough);

    result.range_dB = (int) floor (peak_to_trough_dB + 0.5);

    for (int i = 1; i < num_samples; ++i) {
        int zone;

        if (levels [i] > peak / cube_root) zone = 2;
        else if (levels [i] > trough * cube_root) zone = 1;
        else zone = 0;

        zones [zone]++;

        if (cycles & 1) {       // cycles odd: finding peak level, trigger on trough (which stores peak)
            if (levels [i] > prev_peak) {
                prev_peak = levels [i];
                prev_peak_pos = i;
            }
            else if (levels [i] < prev_peak / square_root) {
                trigger_points [cycles++] = prev_peak_pos;
                prev_trough = levels [i];

                if (cycles == MAX_CYCLES)
                    cycles -= 2;
            }
        }
        else {                  // cycles even (initial): finding trough level, trigger on peak (which stores trough)
            if (levels [i] < prev_trough) {
                prev_trough = levels [i];
                prev_trough_pos = i;
            }
            else if (levels [i] > prev_trough * square_root) {
                trigger_points [cycles++] = prev_trough_pos;
                prev_peak = levels [i];
            }
        }
    }

    double attack_ratio = 0.5;

    if (cycles >= 4) {
        int attack_count = 0, attack_time = 0, decay_count = 0, decay_time = 0;

        for (int i = 2; i < cycles; ++i)
            if (i & 1) {
                attack_time += trigger_points [i] - trigger_points [i - 1];
                attack_count++;
            }
            else {
                decay_time += trigger_points [i] - trigger_points [i - 1];
                decay_count++;
            }

        if (attack_count && decay_count) {
            attack_ratio = (double) attack_time / (attack_time + decay_time);

            if (attack_count != decay_count)
                attack_ratio *= (double) (attack_count + decay_count) / (attack_count * 2.0);
        }
        else
            exit (1);
    }

    double peak_jitter = 1.0;

    if (cycles >= 6) {
        int num_peaks = cycles >> 1;
        double period = (double) (trigger_points [num_peaks * 2 - 1] - trigger_points [1]) / (num_peaks - 1), error_sum = 0.0;

        for (int i = 3; i < cycles - 2; i += 2) {
            double prediction = trigger_points [1] + (period * (i >> 1));
            error_sum += fabs (trigger_points [i] - prediction);
        }

        peak_jitter = (error_sum / (num_peaks - 2)) / period;

        if (peak_jitter > 1.0)
            peak_jitter = 1.0;
    }

    // calculate the low, mid and high zone fractions, then normalize them to 0.5
    double low_fraction = (double) zones [0] / num_samples;
    double mid_fraction = (double) zones [1] / num_samples;
    double high_fraction = (double) zones [2] / num_samples;

    low_fraction *= (1.0 - low_fraction) * (3.0 / 4.0) + 1.0;
    mid_fraction *= (1.0 - mid_fraction) * (3.0 / 4.0) + 1.0;
    high_fraction *= (1.0 - high_fraction) * (3.0 / 4.0) + 1.0;

    result.low_third = (int) floor (low_fraction * 255.0 + 0.5);
    result.mid_third = (int) floor (mid_fraction * 255.0 + 0.5);
    result.high_third = (int) floor (high_fraction * 255.0 + 0.5);
    result.attack_ratio = (int) floor (attack_ratio * 255.0 + 0.5);
    result.peak_jitter = (int) floor (peak_jitter * 255.0 + 0.5);
    result.cycles = cycles;

    if (verbose && ((sample_index - num_samples) % (sample_rate * verbose)) == 0)
        fprintf (stderr, "%02d:%02d-%02d:%02d: level: %5.1f dB - %5.1f dB, peak/trough = %4.1f dB, cycles = %2d, zones = %.3f, %.3f, %.3f, attack = %.3f, jitter = %.3f\n",
            MINS (sample_index - num_samples, sample_rate), SECS (sample_index - num_samples, sample_rate),
            MINS (sample_index, sample_rate), SECS (sample_index, sample_rate),
            log10 (trough / full_scale_rms) * 10.0, log10 (peak / full_scale_rms) * 10.0,
            peak_to_trough_dB, result.cycles,
            result.low_third / 255.0, result.mid_third / 255.0, result.high_third / 255.0,
            attack_ratio, peak_jitter);

    peak_to_trough_histogram [result.range_dB]++;
    cycles_histogram [result.cycles]++;
    low_third_histogram [result.low_third]++;
    mid_third_histogram [result.mid_third]++;
    high_third_histogram [result.high_third]++;

    if (cycles >= 4)
        attack_ratio_histogram [result.attack_ratio]++;

    if (cycles >= 6)
        peak_jitter_histogram [result.peak_jitter]++;

    if (analysis_output_file)
        fwrite (&result, sizeof (result), 1, analysis_output_file);

    return *analysis_result_to_tensor_pointer (&result, tensor);
}

static void display_analysis_results (void)
{
    display_histogram ("peak_to_trough", peak_to_trough_histogram, 96);
    display_histogram ("cycles", cycles_histogram, 256);
    display_histogram ("lower third", low_third_histogram, 256);
    display_histogram ("middle third", mid_third_histogram, 256);
    display_histogram ("upper third", high_third_histogram, 256);
    display_histogram ("attack ratio", attack_ratio_histogram, 256);
    display_histogram ("peak jitter", peak_jitter_histogram, 256);
}

static void display_population (int *histogram, int count, int percent);

static void display_histogram (const char *name, int *histogram, int count)
{
    int min_value = 1000000, max_value = -1, hits = 0, sum = 0, hits2 = 0, max_hits = 0, mode1 = 0, mode2 = 0;
    double median = 0.0;

    for (int value = 0; value < count; ++value)
        if (histogram [value]) {
            if (histogram [value] > max_hits) max_hits = histogram [mode1 = mode2 = value, value];
            else if (histogram [value] == max_hits) mode2 = value;
            if (value < min_value) min_value = value;
            if (value > max_value) max_value = value;
            sum += histogram [value] * value;
            hits += histogram [value];
        }

    for (int value = 0; value < count; ++value)
        if (histogram [value]) {
            if (hits2 + histogram [value] > hits / 2.0) {
                median = value - 0.5 + (hits / 2.0 - hits2) / histogram [value];
                break;
            }
            else
                hits2 += histogram [value];
        }

    if (hits) {
        fprintf (stderr, "%s: range = %d to %d, mean = %g, median = %g, mode = %g\n",
            name, min_value, max_value, (double) sum / hits, median, (mode1 + mode2) / 2.0);
        display_population (histogram, count, 50);
        display_population (histogram, count, 75);
        display_population (histogram, count, 90);
        display_population (histogram, count, 95);
        display_population (histogram, count, 98);
    }
}

static void display_population (int *histogram, int count, int percent)
{
    int low_value, high_value, sum = 0, sum2, target;

    for (int value = 0; value < count; ++value)
        if (histogram [value]) {
            if (sum == 0) low_value = value;
            sum += histogram [value];
            high_value = value;
        }

    if (sum) {
        int toggle = 0;

        target = floor ((double) sum * percent / 100.0 + 0.5);
        sum2 = sum;

        while (sum2 > target)
            if (histogram [low_value] < histogram [high_value] ||
                (histogram [low_value] == histogram [high_value] && (toggle ^= 1))) {
                    if (sum2 - histogram [low_value] / 2 > target)
                        sum2 -= histogram [low_value++];
                    else
                        break;
            }
            else if (sum2 - histogram [high_value] / 2 > target)
                sum2 -= histogram [high_value--];
            else
                break;

        int sum3 = 0;

        for (int value = low_value; value <= high_value; ++value)
            sum3 += histogram [value];

        if (sum2 != sum3) {
            fprintf (stderr, "display_population() error, sum = %d, target = %d, sum2 = %d, sum3 = %d, low = %d, high = %d\n",
                sum, target, sum2, sum3, low_value, high_value);

            exit (1);
        }

        fprintf (stderr, "    %d (%.1f%%): %d to %d\n", sum2, sum2 * 100.0 / sum, low_value, high_value);
    }
}

static int read_tensor_file (tensor_array tensor, char *filename)
{
    int num_bytes = 0, alloced_bytes = 0, res, ch;
    FILE *tensor_file = fopen (filename, "rb");
    unsigned char *buffer = NULL;

    if (!tensor_file) {
        fprintf (stderr, "\nerror: can't open \"%s\" for reading!\n", filename);
        return 0;
    }

    while ((ch = getc (tensor_file)) != EOF) {
        if (num_bytes == alloced_bytes)
            buffer = realloc (buffer, alloced_bytes += 65536);

        buffer [num_bytes++] = ch;
    }

    fclose (tensor_file);
    res = local_tensor_file (tensor, buffer, num_bytes);
    free (buffer);

    return res;
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

static int local_tensor_file (tensor_array tensor, unsigned char *compressed_tensor, int compressed_size)
{
    unsigned char dimensions [4] = { ARRAY_BINS_1, ARRAY_BINS_2, ARRAY_BINS_3, ARRAY_BINS_4 };
    struct tensor_header header;
    streamer reader, writer;

    if (compressed_size < sizeof (header)) {
        fprintf (stderr, "invalid tensor!\n");
        return 0;
    }

    memcpy (&header, compressed_tensor, sizeof (header));
    compressed_tensor += sizeof (header);
    compressed_size -= sizeof (header);

    if (memcmp (header.dimensions, dimensions, sizeof (dimensions)) || header.version != TENSOR_VERSION) {
        fprintf (stderr, "invalid tensor!\n");
        return 0;
    }

    memset (&reader, 0, sizeof (reader));
    memset (&writer, 0, sizeof (writer));

    reader.buffer = compressed_tensor;
    reader.size = compressed_size;

    writer.buffer = (unsigned char *) tensor;
    writer.size = sizeof (tensor_array);

    if (lzw_decompress (write_buff, &writer, read_buff, &reader)) {
        fprintf (stderr, "lzw_decompress() returned error!\n");
        return 0;
    }

    if (reader.index != reader.size || writer.index != writer.size || reader.wrapped || writer.wrapped) {
        fprintf (stderr, "other error in decompressing tensor!\n");
        return 0;
    }

    for (int i = 0; i < sizeof (tensor_array); ++i)
        header.checksum -= ((unsigned char *) tensor) [i];

    if (header.checksum) {
        fprintf (stderr, "checksum error in decompressed tensor!\n");
        return 0;
    }

    return 1;
}
