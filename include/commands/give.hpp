#pragma once

extern void give(ENetEvent& event, const std::string_view text);

/* /givegems {amount} | /givegems {growid} {amount} - developer+ */
extern void givegems(ENetEvent& event, const std::string_view text);
