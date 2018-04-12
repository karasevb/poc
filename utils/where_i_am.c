#include <stdio.h>
#include <sys/sysinfo.h>
#include <sched.h>

int main(int argc, char *argv[]) {
    int num_cpus = get_nprocs_conf();
    int i;
    char hostname[1024];

    gethostname(hostname, 1024);
    printf("[%s:%d] [", hostname, getpid());
    for (i=0; i < num_cpus; i++) {
        printf("%s", sched_getcpu() == i ? "B" : ".");
        if (i != num_cpus-1) {
            printf("/");
        }
    }
    printf("]\n");

    return 0;
}
