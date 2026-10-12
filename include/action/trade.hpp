#pragma once

/* @note trade flow (ported from GTOS' trade_system):
 * wrench > Trade  -> request_trade() sends OnStartTrade to us
 * client          -> action|trade_started (both sides opened = trade begins)
 * add / remove    -> action|mod_trade, dialog trade_add, action|rem_trade
 * both accept     -> action|trade_accept, then dialog trade_confirm on both sides
 * cancel          -> action|trade_cancel, leaving the world, disconnecting */

extern void request_trade(ENetEvent& event, ENetPeer &target);
extern void end_trade(ENetPeer &p); // @note cancels for both sides

extern void trade_add(ENetEvent& event, const ::hPipe &hPipe);     // @note dialog_return
extern void trade_confirm(ENetEvent& event, const ::hPipe &hPipe); // @note dialog_return

namespace action
{
    extern void trade_started(ENetEvent& event, const std::string& header);
    extern void mod_trade(ENetEvent& event, const std::string& header);
    extern void rem_trade(ENetEvent& event, const std::string& header);
    extern void trade_accept(ENetEvent& event, const std::string& header);
    extern void trade_cancel(ENetEvent& event, const std::string& header);
}
