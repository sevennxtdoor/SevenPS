#include "pch.hpp"

#include "popup.hpp"
#include "drop_item.hpp"
#include "trash_item.hpp"
#include "find_item.hpp"
#include "gateway_edit.hpp"
#include "billboard_edit.hpp"
#include "lock_edit.hpp"
#include "create_blast.hpp"
#include "socialportal.hpp"
#include "megaphone.hpp"
#include "commands/titles.hpp"
#include "action/trade.hpp"
#include "tools/vending.hpp"
#include "tools/surgery.hpp"
#include "tools/wrench_menu.hpp"
#include "tools/profile.hpp"
#include "tools/social.hpp"
#include "tools/rank.hpp"
#include "tools/area_lock.hpp"
#include "tools/boxes.hpp"

#include "__dialog_return.hpp"

std::unordered_map<std::string, std::function<void(ENetEvent &, const ::hPipe &)>> dialog_return_pool
{
    {"popup", std::bind(&popup, std::placeholders::_1, std::placeholders::_2)},

    {"drop_item", std::bind(&drop_item, std::placeholders::_1, std::placeholders::_2)},
    {"trash_item", std::bind(&trash_item, std::placeholders::_1, std::placeholders::_2)},
    {"find_item", std::bind(&find_item, std::placeholders::_1, std::placeholders::_2)},

    {"gateway_edit", std::bind(&gateway_edit, std::placeholders::_1, std::placeholders::_2)},
    {"door_edit", std::bind(&gateway_edit, std::placeholders::_1, std::placeholders::_2)},
    {"sign_edit", std::bind(&gateway_edit, std::placeholders::_1, std::placeholders::_2)},

    {"billboard_edit", std::bind(&billboard_edit, std::placeholders::_1, std::placeholders::_2)},
    {"lock_edit", std::bind(&lock_edit, std::placeholders::_1, std::placeholders::_2)},

    {"create_blast", std::bind(&create_blast, std::placeholders::_1, std::placeholders::_2)},
    {"socialportal", std::bind(&socialportal, std::placeholders::_1, std::placeholders::_2)},
    {"megaphone", std::bind(&megaphone, std::placeholders::_1, std::placeholders::_2)},
    {"title_edit", std::bind(&title_edit, std::placeholders::_1, std::placeholders::_2)},
    {"trade_add", std::bind(&trade_add, std::placeholders::_1, std::placeholders::_2)},
    {"trade_confirm", std::bind(&trade_confirm, std::placeholders::_1, std::placeholders::_2)},
    {"vending", std::bind(&vending_return, std::placeholders::_1, std::placeholders::_2)},
    {"surge", std::bind(&surgery_return, std::placeholders::_1, std::placeholders::_2)},
    {"notebook", std::bind(&notebook_return, std::placeholders::_1, std::placeholders::_2)},
    {"wl_bank", std::bind(&wl_bank_return, std::placeholders::_1, std::placeholders::_2)},
    {"online_status", std::bind(&online_status_return, std::placeholders::_1, std::placeholders::_2)},
    {"sendpm", std::bind(&sendpm_return, std::placeholders::_1, std::placeholders::_2)},
    {"mt_profile", std::bind(&profile_return, std::placeholders::_1, std::placeholders::_2)},
    {"friends", std::bind(&friends_return, std::placeholders::_1, std::placeholders::_2)},
    {"mt_friend", std::bind(&friend_request_return, std::placeholders::_1, std::placeholders::_2)},
    {"mt_report", std::bind(&report_return, std::placeholders::_1, std::placeholders::_2)},
    {"mt_rank", std::bind(&rank_return, std::placeholders::_1, std::placeholders::_2)},
    {"area_lock", std::bind(&area_lock_return, std::placeholders::_1, std::placeholders::_2)},
    {"mt_box", std::bind(&box_return, std::placeholders::_1, std::placeholders::_2)},
};