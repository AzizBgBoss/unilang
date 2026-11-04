#include <assert.h>
#include "../C-interpreter/host_options.h"

int main(void)
{
    char *plain[] = {"main", "game.ulc"};
    char *enabled[] = {"main", "game.ulc", "--net"};
    assert(!ULVM_desktop_network_flag(2, plain));
    assert(ULVM_desktop_network_flag(3, enabled));
    return 0;
}
