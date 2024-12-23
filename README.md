## SKIPPER

Selective Audio Detection and Filter

Copyright (c) 2024 David Bryant.

All Rights Reserved.

Distributed under the [BSD Software License](https://github.com/dbry/skipper/blob/main/LICENSE).

## What is this then?

**Skipper** is a simple machine-learning-trained audio filter that can
differentiate between musical material and talking in audio streams
and, optionally, filter out (i.e., skip) one or the other.

I developed this because I enjoy listening to and archiving FM radio
and Internet music program streams from local stations. These are great
for discovering new music, learning about the local music scene, and listening
to interviews with artists and others in the music community. I find that
these programs provide a superior curation of music than any automated
methods or "genre" streams I have listened to. One local station's
catch phrase is "don't let the robots win", and I'm on board!

The problem is that if I listen to these archived programs more than once
the dialog starts to get repetitive and sometimes even irrelevent and
outdated (e.g., upcoming concerts or long finished pledge drives). Also,
there are times when the dialog is undesirable, such as when I'm using the
stream as background music for intense exercise, or reading, or when I
have guests over.

For these situations I have even gone as far as editing particularly good
archived streams and removing the dialog. Unfortunately this is somewhat
time-consuming and not at all practical on a regular basis. However, when
I did do this I noticed that I could fairly easily distinguish the music
and dialog by just glancing at the waveforms, and have thought that this
would be something that could be automated without too much difficulty
(famous last words).

Anyway, _that_ is what **Skipper** is. By default it simply acts as a filter,
consuming raw PCM audio (stereo or mono, 16-bit) from `stdin` and writing it
unchanged (except always stereo) to `stdout`. However, it will be detecting
music/talk transitions and reporting those timestamps to `stderr`, and two
options are provided for filtering based on that detection.

Specifying `-t` will skip over the detected talk and pass only the music
(with crossfades to smooth the transitions) and, conversely, `-m` will
skip over the detected music and pass only the talking portions, which is
handy to see how well (or poorly) **Skipper** is working, and might even
be useful on its own.

## Caveats

So, while it's pretty trivial to distinguish _most_ music and talk in
audio streams, it's basically impossible to be 100% accurate. Why? Well consider
the situation where the DJ is talking during and over the music. Depending
on the relative levels, and what portion of the time the talking is occurring,
this can essentially make the talk detection impossible. Or consider a cappella
singing (which can range from operatic singing to essentially talking), or music
that includes people actually talking (my brother used to like putting preachers
in his songs). And some music genres simply have a very similar temporal acoustic
profile to talk, and **Skipper** gets easily confused.

Despite this, I find the program useful enough, and I have provided options for
adjusting the detection threshold if it's getting too much talk or skipping too
much music. And I'm working on improving the algorithm by creating a larger
training corpus and more analysis functionality, so improvements will come...

## Building

I have provided a Makefile that should build the program on Linux and similar
setups. I'll provide a Windows executable as well.

Note that the executable `skipper` is the only one required. The other
executables `tensor-gen` and `bin2c` are used, along with the `-a` option
of `skipper` for generating tensor files from training audio data.

## Usage

There are probably many ways to use **Skipper**, but I have been using it with
[FFmpeg](https://www.ffmpeg.org/) as the source because it handles virtually every
format and works well writing to pipes. The output of `FFmpeg` is piped directly to
`skipper` and its output is then piped to an appropriate encoder like
[lame](https://lame.sourceforge.io/):

> ffmpeg -i sourcefile.ext -f s16le - | ./skipper -t | lame -r - music-only.mp3

Alternatively, it's also possible to pipe the output of `skipper` directly to
[FFplay](https:www.ffmpeg.org/) for immediate playback. In this use case we use the
`-k` option to add "keep-alive" crossfades during long skips so that the playback
does not underrun.

> ffmpeg -i sourcefile.ext -f s16le - | ./skipper -tk | ffplay - -f s16le -ch_layout stereo

Currently **Skipper**'s functionality is only available as a command-line filter.
I have plans to create a callable library as well to make it possible to more easily
integrate into an existing application.

## Help

```
 SKIPPER  Selective Audio Detection and Filter  Version 0.1
 Copyright (c) 2024 David Bryant. All Rights Reserved.

 Usage:     SKIPPER [-options] < SourceAudio.pcm > StereoOutput.pcm

 Operation: scan source audio (`stdin`) using tensor discrimination to filter
            output (`stdout`), skipping either music (-m) or talk (-t); or
            output raw scan analytics for use with TENSOR-GEN util (-a)

 Options:  -a <file.bin>    = output analysis results to specified file
           -c<n>            = override default channel count of 2
           -d <file.tensor> = specify alternate discrimination tensor file
           -k               = keep-alive crossfading for long skips
           -l<n>            = left output override (for debug, n = 1-4:
                            = 1=mono, 2=filtered, 3=level, 4=tensor)
           -m[<n>]          = skip over music, with optional threshold offset
                            = (raise or lower music threshold +/- 99 points)
           -n               = no audio output (skip everything)
           -p               = pass all audio (no skipping, default)
           -q               = no messaging except errors
           -r<n>            = right output override (for debug, n = 1-4:
                            = 1=mono, 2=filtered, 3=level, 4=tensor)
           -s<n>            = override default sample rate of 44.1 kHz
           -t[<n>]          = skip over talk, with optional threshold offset
                            = (raise or lower talk threshold +/- 99 points)
           -v[<n>]          = set verbosity + [rate in seconds]

 Web:      Visit www.github.com/dbry/skipper for latest version and info

```
