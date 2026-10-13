#include "pch.hpp"
#include "find.hpp"

void find(ENetEvent& event, const std::string_view text)
{
    if (static_cast<::peer*>(event.peer->data)->role < DEVELOPER) // @note real-Growtopia economy: developers & owner only
        return send_action(*event.peer, "log", "msg|`4/find is for staff only.`` Farm items, buy them in the `2Store`` or trade!");
    send_varlist(event.peer, {
        "OnDialogRequest", 
        "set_default_color|`o\n"
        "add_text_input|n|Search: ||26|\n"
        "add_searchable_item_list||sourceType:allItems;listType:iconWithCustomLabel;resultLimit:30|n|\n"
        "add_quick_exit|\n"
        "end_dialog|find_item|||"
    });
}