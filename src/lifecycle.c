// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern int sceKernelUsleep(uint32_t microseconds);
extern int sceKernelDebugOutText(int channel, const char *text);
extern int sceSystemServiceLoadExec(const char *path, const char **args);
// main returned: ask the system to end this title. The C library's exit() and _Exit() are not a
// way out for a native title: the kernel answers them with signal 12, so the app "crashes", the
// system writes a crash report and force-kills it (every development run ended that way, and three
// console losses followed forced kills). If the request is refused, wait to be closed as before.
__attribute__((noreturn)) void catchReturnFromMain(int status) {
    char marker[96];
    fflush(NULL);
    snprintf(marker, sizeof(marker), "EDEN_PPSA99121_MAIN_RETURN status=%d\n", status);
    sceKernelDebugOutText(0, marker);
    const int refused = sceSystemServiceLoadExec("exit", NULL);
    snprintf(marker, sizeof(marker), "EDEN_PPSA99121_EXIT_REFUSED rc=%x\n", (unsigned)refused);
    sceKernelDebugOutText(0, marker);
    for (;;) sceKernelUsleep(100000);
}
// Start this app again in a fresh process: the system ends this one and runs the app's own
// executable. After a crash report was written (headless/crash_report.cpp), and for a game that
// will not stop (headless/stop_limit.h). No stdio: the caller is a helper beside a crashed or a
// stuck thread. Returns only when the system refuses.
int eden_restart_app(void) {
    char marker[96];
    sceKernelDebugOutText(0, "EDEN_PPSA99121_RESTART\n");
    const int refused = sceSystemServiceLoadExec("/app0/eboot.bin", NULL);
    snprintf(marker, sizeof(marker), "EDEN_PPSA99121_RESTART_REFUSED rc=%x\n", (unsigned)refused);
    sceKernelDebugOutText(0, marker);
    return refused;
}
// End the app the way a return from main does, from any thread. Returns only when refused.
int eden_exit_app(void) {
    char marker[96];
    sceKernelDebugOutText(0, "EDEN_PPSA99121_EXIT\n");
    const int refused = sceSystemServiceLoadExec("exit", NULL);
    snprintf(marker, sizeof(marker), "EDEN_PPSA99121_EXIT_REFUSED rc=%x\n", (unsigned)refused);
    sceKernelDebugOutText(0, marker);
    return refused;
}
__attribute__((noreturn)) void __assert(const char *function, const char *file,
                                      int line, const char *expression) {
    fprintf(stderr, "assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}
