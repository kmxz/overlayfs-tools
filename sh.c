#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include "sh.h"

FILE* create_shell_script(char *tmp_path_buffer) {
    int tmp_file = mkstemps(tmp_path_buffer, 3); // the 3 is for suffix length (".sh")
    if (tmp_file < 0) { return NULL; }
    fchmod(tmp_file, S_IRWXU); // chmod to 0700
    FILE* f = fdopen(tmp_file, "w");
    if (f == NULL) { return NULL; }
    fprintf(f, "#!/usr/bin/env bash\n");
    fprintf(f, "set -x\n");
    time_t rawtime;
    time (&rawtime);
    fprintf(f, "# This sheel script is generated by overlayfs-tools on %s.\n", ctime (&rawtime));
    return f;
}

int quote(const char *filename, FILE *output) {
    if (fputc('\'', output) == EOF) { return -1; }
    for (const char *s = filename; *s != '\0'; s++) {
        if (*s == '\'') {
            if (fprintf(output, "'\"'\"'") < 0) { return -1; }
        } else {
            if (fputc(*s, output) == EOF) { return -1; }
        }
    }
    if (fputc('\'', output) == EOF) { return -1; }
    return 0;
}

int command(FILE *output, const char *command_format, ...) {
    va_list arg;
    va_start(arg, program);
    for (size_t i = 0; command_format[i] != '\0'; i++) {
        if (command_format[i] == '%') {
            const char *s = va_arg(arg, char *);
            if (quote(s, output) < 1) { return -1; }
        } else {
            if (fputc(command_format[i], output) == EOF) { return -1; }
        }
    }
    va_end(arg);
    if (fputs('\n', output) == EOF) { return -1; }
    return 0;
}