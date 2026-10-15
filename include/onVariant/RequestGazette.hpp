#pragma once

namespace on
{
    extern void RequestGazette(ENetEvent& event);
}

extern void gazette_return(ENetEvent &event, const ::hPipe &hPipe); // @note buttons in the gazette (Peraturan Server)
