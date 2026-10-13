#include "pch.hpp"
#include "tools/economy.hpp"
#include <sstream>
#include "onVariant/SetBux.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "give.hpp"

/* /give {id} {amount} - developer only. amount is optional (default 1) */
void give(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < DEVELOPER)
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }

    std::istringstream args{ std::string{ text.substr(sizeof("give ")-1) } };
    int id{}, amount{1};
    if (!(args >> id) || id <= 0 || id >= (int)items.size())
    {
        send_action(*event.peer, "log", "msg|`4Usage:`` /give {item id} {amount} - use `$/find`` to look up item ids.");
        return;
    }
    if (int n{}; args >> n) amount = n;
    amount = std::clamp(amount, 1, 200);

    /* @note peer::emplace() caps a slot at 200, so only send what actually fits or the client shows a wrong count */
    auto it = std::ranges::find(pPeer->slots, (short)id, &::slot::id);
    if (it == pPeer->slots.end() && (int)pPeer->slots.size() >= pPeer->slot_size)
    {
        send_action(*event.peer, "log", "msg|`4Your backpack is full!``");
        return;
    }
    if (it != pPeer->slots.end()) amount = std::min(amount, 200 - it->count);
    if (amount <= 0)
    {
        send_action(*event.peer, "log", "msg|`4You already have 200 of that item.``");
        return;
    }

    modify_item_inventory(event, ::slot{(short)id, (short)amount});
    staff_log(*pPeer, "/give", "", id, amount);
    send_action(*event.peer, "log", std::format("msg|`2Given `w{}`` `w{}`` `2(id {}).``", amount, id_to_item(id).raw_name, id));
}

static std::string to_lower(std::string str)
{
    for (char &c : str) c = std::tolower(c);
    return str;
}

/* /givegems {amount} - yourself | /givegems {growid} {amount} - someone else (online or offline). negative takes gems away */
void givegems(ENetEvent& event, const std::string_view text)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < DEVELOPER)
    {
        send_action(*event.peer, "log", "msg|`4Unknown command.`` Enter `$/?`` for a list of valid commands.");
        return;
    }

    std::istringstream args{ std::string{ text.substr(sizeof("givegems ")-1) } };
    std::string first{}, second{};
    args >> first >> second;

    std::string growid = second.empty() ? pPeer->growid : first;
    long long amount{};
    try { amount = std::stoll(second.empty() ? first : second); }
    catch (const std::logic_error &) 
    {
        send_action(*event.peer, "log", "msg|`4Usage:`` /givegems {amount} or /givegems {growid} {amount}");
        return;
    }
    amount = std::clamp(amount, -2'000'000'000LL, 2'000'000'000LL);
    if (pPeer->role < OWNER && to_lower(growid) != to_lower(pPeer->growid)) // @note a developer could hand out unlimited gems
    {
        send_action(*event.peer, "log", "msg|`4Only the owner can give gems to other players.`` You can only /givegems yourself.");
        return;
    }
    staff_log(*pPeer, "/givegems", growid, 0, static_cast<int>(amount));

    /* @note online: change memory (saved on logout). offline: change the database directly */
    ENetPeer *target{};
    for (ENetPeer *p : peers())
        if (::peer *pOthers = static_cast<::peer*>(p->data); pOthers && to_lower(pOthers->growid) == to_lower(growid)) target = p;

    if (target)
    {
        ::peer *pTarget = static_cast<::peer*>(target->data);
        const signed before = pTarget->gems;
        pTarget->gems = static_cast<signed>(std::clamp(pTarget->gems + amount, 0LL, (long long)std::numeric_limits<signed>::max()));
        econ("staff /givegems", pTarget->gems - before);
        ENetEvent other{ .peer = target };
        on::SetBux(other);
        if (target != event.peer)
            on::ConsoleMessage(target, std::format("`oYou received `${}`` gems from {}`o!``", amount, pPeer->display_growid));
        send_action(*event.peer, "log", std::format("msg|`2{} now has `${}`` `2gems.``", pTarget->growid, pTarget->gems));
        return;
    }
    {
        ::hStmt hStmt{ "SELECT 1 FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(growid);
        hStmt.bind_param(&param);
        hStmt.execute();
        if (mysql_stmt_store_result(hStmt.pStmt) || mysql_stmt_num_rows(hStmt.pStmt) == 0)
        {
            send_action(*event.peer, "log", std::format("msg|`4Player `w{}`` `4not found.``", growid));
            return;
        }
    }
    ::hStmt hStmt{ "UPDATE peer SET gems = LEAST(2147483647, GREATEST(0, CAST(gems AS SIGNED) + ?)) WHERE growid = ?" };
    MYSQL_BIND params[2] = { make_bind_in(amount), make_bind_in(growid) };
    hStmt.bind_param(params);
    hStmt.execute();
    send_action(*event.peer, "log", std::format("msg|`2Gave `${}`` `2gems to `w{}`` (offline).``", amount, growid));
}
