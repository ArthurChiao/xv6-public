#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf(2, "Usage: %s <n>\n", argv[0]);
        exit();
    }

    int n = atoi(argv[1]);

    printf(1, "Going to sleep %d\n", n);
    if (sleep(n)) {
        printf(2, "sleep failed\n");
        exit();
    }

    printf(1, "Sleep done\n", n);
    exit();
}
