// USB serial console (UART0, 115200). The only place bench drive can be
// switched on: never over the network.
#pragma once

namespace airo::esp {
void startConsole();
}
