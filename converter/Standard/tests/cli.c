#include "../src/builder.h"
#include "../src/config.h"
#include <stdio.h>
#include <signal.h>
static volatile sig_atomic_t interrupted;
static void handle_signal(int sig) { (void)sig; interrupted=1; }
static int progress(enum Stage stage,unsigned percent,void *userdata) {
    (void)userdata;
    static unsigned previous=101; static int previous_stage=-1;
    if (previous!=percent || previous_stage!=(int)stage) {
        fprintf(stderr,"%3u%%  %s\n",percent,builder_stage(stage));
        previous=percent; previous_stage=stage;
    }
    return !interrupted;
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
#else
int main(int argc,char **argv) {
#endif
    if (argc!=4) {
        fprintf(stderr,"Diablo GBA ROM Builder %s\n"
            "Usage: builder DIABDAT.MPQ v094.dgb NEW-output.gba\n"
            "Offline MPQ-bound ROM restoration, not an asset re-encoder.\n",BUILDER_VERSION);
        return 64;
    }
    signal(SIGINT,handle_signal); signal(SIGTERM,handle_signal);
    int result=builder_run(argv[1],argv[2],argv[3],progress,NULL);
    fprintf(stderr,"[%d] %s\n",result,builder_message(result));
    return result;
}
