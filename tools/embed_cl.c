/*
 * embed_cl.c -- turn a kernel source file into a C string literal.
 *
 * This is free and unencumbered software released into the public domain.
 * See LICENSE.
 *
 * The OpenCL kernel has to reach the driver as a string, but writing it as a
 * string literal by hand costs syntax highlighting, escaping, and any hope of a
 * readable diff. So the kernel lives in a real .cl file and this emits the
 * header that embeds it.
 *
 * The result is generated into $(BUILD) on every build and is not committed:
 * it is a pure function of its input, and this program needs nothing but the C
 * compiler the build already requires, so a second copy in the tree could only
 * ever be a thing to keep in sync. The binary stays self-contained either way
 * -- the array is compiled in, so there is no data file to install and nothing
 * to locate at run time.
 *
 * Written in C rather than as a shell or Python step so the build depends on
 * nothing beyond the compiler it already needs. xxd would have done the job but
 * lives in vim-common, which is exactly the kind of incidental dependency
 * the dependency budget rules out.
 *
 * Emitted as a byte array rather than a string literal on purpose. C99 only
 * guarantees string literals up to 4095 characters (5.2.4.1); real compilers
 * accept far more, but -Wpedantic rightly complains, and a kernel is easily
 * longer than that once it carries comments. An array initialiser has no such
 * limit and stays warning-clean everywhere.
 *
 * The consequence is that the generated header is machine output and not worth
 * reading. Review changes in the .cl file, which is the source of truth.
 *
 *   Usage: embed_cl SYMBOL GUARD < input.cl > output.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s SYMBOL GUARD < input.cl > output.h\n",
                argv[0]);
        return 2;
    }

    const char *symbol = argv[1];
    const char *guard = argv[2];

    printf("/*\n");
    printf(" * %s -- GENERATED FILE, DO NOT EDIT.\n", guard);
    printf(" *\n");
    printf(" * Produced by tools/embed_cl.c from the kernel source, into the\n");
    printf(" * build directory. Not committed and not edited: change the .cl\n");
    printf(" * file or the constant tables and rebuild.\n");
    printf(" *\n");
    printf(" * Machine output -- review changes in the .cl file, not here. A\n");
    printf(" * byte array rather than a string literal because C99 only\n");
    printf(" * guarantees 4095-character string literals and a kernel exceeds\n");
    printf(" * that; an array initialiser has no such limit.\n");
    printf(" *\n");
    printf(" * This is free and unencumbered software released into the public"
           " domain.\n");
    printf(" */\n\n");
    printf("#ifndef %s\n", guard);
    printf("#define %s\n\n", guard);
    printf("static const char %s[] = {\n", symbol);

    int c;
    size_t count = 0;

    while ((c = fgetc(stdin)) != EOF) {
        if (count % 16 == 0)
            fputs("    ", stdout);
        printf("0x%02x,", (unsigned) (unsigned char) c);
        count++;
        if (count % 16 == 0)
            fputc('\n', stdout);
    }

    /* NUL terminate: callers pass this straight to clCreateProgramWithSource
       as a C string. */
    if (count % 16 == 0)
        fputs("    ", stdout);
    printf("0x00\n");

    printf("};\n\n");
    printf("#endif /* %s */\n", guard);
    return 0;
}
