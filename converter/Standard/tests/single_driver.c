#define _POSIX_C_SOURCE 200809L
#include "../src/builder.h"
#include "../src/embedded.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const char *mode; const char *output; } Test;
static int progress(enum Stage stage,unsigned percent,void *context) {
    Test *t=context;
    if (!strcmp(t->mode,"cancel") && stage==STAGE_SAVE && percent>=97) return 0;
    if (!strcmp(t->mode,"race") && stage==STAGE_SAVE && percent==99) {
        FILE *f=fopen(t->output,"wbx"); if (!f) return 0;
        fputs("concurrent writer",f); fclose(f);
    }
    return 1;
}
int main(int argc,char **argv) {
    if (argc!=4) return 64;
    Test t={argv[3],argv[2]};
    const unsigned char *package=embedded_package;
    size_t size=embedded_package_size;
    unsigned char *modified=NULL;
    if (!strcmp(t.mode,"null")) package=NULL;
    if (!strcmp(t.mode,"short")) size--;
    if (!strcmp(t.mode,"long")) size++;
    if (!strcmp(t.mode,"header") || !strcmp(t.mode,"cipher") || !strcmp(t.mode,"tag")) {
        modified=malloc(size); if (!modified) return 3;
        memcpy(modified,package,size); package=modified;
        modified[!strcmp(t.mode,"header") ? 0 : !strcmp(t.mode,"cipher") ? 1024 : size-1]^=1;
    }
    int result=builder_run_memory(argv[1],package,size,argv[2],progress,&t);
    free(modified); fprintf(stderr,"[%d] %s\n",result,builder_message(result)); return result;
}
