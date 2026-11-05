#define _POSIX_C_SOURCE 200809L
#include "builder.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
static volatile sig_atomic_t interrupted;
static void handle_signal(int sig) { (void)sig; interrupted=1; }
static int progress(enum Stage stage,unsigned percent,void *unused) {
    (void)unused;
    fprintf(stderr,"%3u%%  %s\n",percent,builder_stage(stage));
    return !interrupted;
}
int main(int argc, char **argv) {
    char executable[PATH_MAX], raw[PATH_MAX], resources[PATH_MAX], package[PATH_MAX], script[PATH_MAX];
#ifdef __APPLE__
    uint32_t size=sizeof(raw);
    if (_NSGetExecutablePath(raw,&size) || !realpath(raw,executable)) return 70;
#else
    ssize_t n=readlink("/proc/self/exe",raw,sizeof(raw)-1);
    if (n<0) return 70;
    raw[n]=0;
    if (!realpath(raw,executable)) return 70;
#endif
    memcpy(raw,executable,strlen(executable)+1);
    char *slash=strrchr(raw,'/'); if (!slash) return 70; *slash=0;
    if (snprintf(resources,sizeof(resources),"%s/../Resources",raw)>=(int)sizeof(resources) ||
        snprintf(package,sizeof(package),"%s/v095.dgb",resources)>=(int)sizeof(package) ||
        snprintf(script,sizeof(script),"%s/Builder.applescript",resources)>=(int)sizeof(script)) return 70;
    if (argc==1 || (argc==2 && !strncmp(argv[1],"-psn_",5))) {
        char *args[]={"/usr/bin/osascript",script,executable,package,NULL};
        execv(args[0],args);
        perror("Cannot start macOS dialogs"); return 70;
    }
    if (argc==2 && !strcmp(argv[1],"--version")) {
        printf("Diablo GBA ROM Builder %s\nROM: %s\n",BUILDER_VERSION,ROM_VERSION); return 0;
    }
    if (argc==2 && !strcmp(argv[1],"--help")) {
        printf("Diablo GBA ROM Builder %s\n"
               "Open the app for file selection dialogs.\n"
               "CLI: builder --build /path/DIABDAT.MPQ /path/NEW-output.gba\n"
               "Offline restoration of %s. Existing files are never replaced.\n",BUILDER_VERSION,ROM_VERSION); return 0;
    }
    if (argc!=4 || strcmp(argv[1],"--build")) { fprintf(stderr,"Use --help for usage.\n"); return 64; }
    signal(SIGINT,handle_signal); signal(SIGTERM,handle_signal);
    int result=builder_run(argv[2],package,argv[3],progress,NULL);
    fprintf(stderr,"[%d] %s\n",result,builder_message(result));
    return result;
}
