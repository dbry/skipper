////////////////////////////////////////////////////////////////////////////
//                             **** BIN2C ****                            //
//                      Binary to C-source converter                      //
//                    Copyright (c) 2024 David Bryant.                    //
//                          All Rights Reserved.                          //
//      Distributed under the BSD Software License (see license.txt)      //
////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#endif

#include "lzwlib.h"

#define BYTES_PER_LINE  16

int main (int argc, char **argv)
{
    int num_bytes = 0, alloced_bytes = 0, ch;
    unsigned char *buffer = NULL;

#ifdef _WIN32
    setmode (fileno (stdin), O_BINARY);
#endif

    while ((ch = getchar ()) != EOF) {
        if (num_bytes == alloced_bytes)
            buffer = realloc (buffer, alloced_bytes += 65536);

        buffer [num_bytes++] = ch;
    }

    printf ("static unsigned char %s [%d] = {\n", argc == 2 ? argv [1] : "array", num_bytes);

    for (int i = 0; i < num_bytes; i += BYTES_PER_LINE) {
        char string [256] = { 0 };

        strcat (string, "    ");
        for (int j = 0; i + j < num_bytes && j < BYTES_PER_LINE; ++j) {
            sprintf (string + strlen (string), "0x%02x", buffer [i+j]);
            if (i + j < num_bytes - 1)
                strcat (string, ",");
            if (i + j < num_bytes - 1 && j < BYTES_PER_LINE - 1)
                strcat (string, " ");
        }

        printf ("%s\n", string);
    }

    printf ("};\n");
    free (buffer);
    return 0;
}
