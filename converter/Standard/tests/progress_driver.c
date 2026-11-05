#define _POSIX_C_SOURCE 200809L
#include "../src/builder.h"
#include <stdio.h>
#include <string.h>
typedef struct { const char *mode; const char *output; } Test;
static int callback(enum Stage s,unsigned p,void *context) {
    Test *t=context;
    if (!strcmp(t->mode,"check") && s==STAGE_CHECK) return 0;
    if (!strcmp(t->mode,"mpq") && s==STAGE_MPQ && p>=10) return 0;
    if (!strcmp(t->mode,"package") && s==STAGE_PACKAGE && p>=74) return 0;
    if (!strcmp(t->mode,"decrypt") && s==STAGE_DECRYPT) return 0;
    if (!strcmp(t->mode,"verify") && s==STAGE_VERIFY) return 0;
    if (!strcmp(t->mode,"save") && s==STAGE_SAVE && p>=97) return 0;
    if (!strcmp(t->mode,"commit") && s==STAGE_SAVE && p==99) return 0;
    if (!strcmp(t->mode,"race") && s==STAGE_SAVE && p==99) {
        FILE *f=fopen(t->output,"wbx");
        if (!f) return 0;
        fputs("concurrent writer",f); fclose(f);
    }
    return 1;
}
int main(int argc,char **argv) {
    if (argc!=5) return 64;
    Test test={argv[4],argv[3]};
    return builder_run(argv[1],argv[2],argv[3],callback,&test);
}
