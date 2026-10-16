#include "pch.hpp"
#include "buy.hpp"

#include "storenavigate.hpp"

void action::storenavigate(ENetEvent& event, const std::string& header)
{
    std::vector<std::string> pipes = readch(header, '|');

    std::string item{};
    std::string selection{};

    for (std::size_t i = 0; i + 1 < pipes.size(); ++i) // @note i+1 read past the end on the last pipe
    {
        if      (pipes[i] == "item") item = pipes[i+1];
        else if (pipes[i] == "selection") selection = pipes[i+1];
    }
    buy(event, header, selection);

}