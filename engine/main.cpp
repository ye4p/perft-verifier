#include "board.h"
#include <iostream>
#include <memory>
#include "chrono"
#include <thread>

int main()
{
    // std::cout << "Running\n";
    auto board = std::make_unique<Board>();

    board->uci_loop();

    return 0;
}
