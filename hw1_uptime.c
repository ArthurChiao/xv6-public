#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char* argv[]) {
    int n = uptime();

    printf(1, "Uptime %d\n", n);
    exit();
}
