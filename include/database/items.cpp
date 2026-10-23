#include "pch.hpp"
#include <filesystem>
#include <fstream>

#include "items.hpp"

std::vector<::item> items;

const ::item &id_to_item(u_short id) noexcept // @note std::out_of_range is handled
{
    if (id >= items.size()) return items[0]; // @note we can't return a contructor so this is gonna be our dummy value

    return items[id];
}


std::vector<u_char> im_data(sizeof(::gamePacket)/*inital packet*/, 0x00);

template<typename T>
void shift_pos(const std::vector<u_char> &data, u_int &pos, T &value) noexcept // @note std::out_of_range is handled
{
    u_char *i8 = reinterpret_cast<u_char*>(&value);

    if (pos + sizeof(T) >= data.size()) puts("this items.dat is unsupported");
    for (std::size_t i = 0ull; i < sizeof(T); ++i) 
    {
        i8[i] = data[pos + i];
    }
    pos += sizeof(T);
}

/* have not tested modifying string values··· */
template<typename T>
void data_modify(std::vector<u_char> &data, const u_int &pos, const T &value) noexcept // @note std::out_of_range is handled
{
    const u_char *i8 = reinterpret_cast<const u_char*>(&value);

    if (pos + sizeof(T) >= data.size()) return; // @todo for custom items we would need resize data (im_data)
    for (std::size_t i = 0ull; i < sizeof(T); ++i) 
    {
        data[pos + i] = i8[i];
    }
}

u_int items_hash{};

/* @note the packet header in front of the items.dat bytes */
static std::vector<u_char> items_header(u_int size)
{
    return compress_state(::gamePacket{ .type = 0x10,/*PACKET_SEND_ITEM_DATABASE_DATA*/ .state = state::S_EXTENDED, .size = size }).data();
}

static void parse_items();

void decode_items()
{
    const u_int size = std::filesystem::file_size("items.dat");
    im_data = items_header(size);

    u_int pos = im_data.size(); // @note sizeof(::gamePacket)
    im_data.resize(pos + size); // @note resize to fit binary data

    std::ifstream("items.dat", std::ios::binary)
        .read((char*)&im_data[pos], size); // @note the binary data···

    parse_items();
    if (apply_sponsor_items()) // @note resources/sponsor.txt renamed some items: read the new bytes again
    {
        items.clear();
        parse_items();
    }
    /* @note same hash as Proton's HashString (GrowServer RTTEX.hash), over the items.dat bytes only */
    const u_int header = static_cast<u_int>(items_header(0).size());
    u_int acc = 0x55555555;
    for (std::size_t i = header; i < im_data.size(); ++i) acc = (acc >> 27) + (acc << 5) + im_data[i];
    items_hash = acc;
    printf("items.dat parsed successfully! (hash %u)\n", items_hash);
}

/* @note sponsor items: "id|name|description" lines. the name is stored XOR'd with the token like the client expects
 * (GTopia ItemInfo: XorCipherString(name, "PBG892FXX982ABC*", id)), the description is plain. both are u16-length strings,
 * so the file is rebuilt with the new strings spliced in. @return true if anything changed */
bool apply_sponsor_items()
{
    std::ifstream file("resources/sponsor.txt");
    if (!file) return false;

    struct patch { u_short id; std::string name, info; };
    std::vector<patch> patches{};
    for (std::string line; std::getline(file, line); )
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const std::vector<std::string> f = readch(line, '|');
        if (f.size() < 3) continue;
        const int id = atoi(f[0].c_str());
        if (id <= 0 || id >= static_cast<int>(items.size()) || f[1].empty() || f[1].size() > 60 || f[2].size() > 500) continue;
        patches.push_back(patch{ static_cast<u_short>(id), f[1], f[2] });
    }
    if (patches.empty()) return false;
    std::ranges::sort(patches, {}, &patch::id); // @note items are stored in id order

    const std::string_view token{"PBG892FXX982ABC*"};
    const u_int header = static_cast<u_int>(items_header(0).size());
    std::vector<u_char> out(im_data.begin() + header, im_data.begin() + header); // @note empty, items.dat bytes only
    out.reserve(im_data.size());
    u_int cursor = header;
    auto copy_to = [&](u_int pos) { out.insert(out.end(), im_data.begin() + cursor, im_data.begin() + pos); cursor = pos; };
    auto put_string = [&](u_int at, const std::string &text, auto encode) {
        copy_to(at);
        const short old_len = *reinterpret_cast<const short*>(&im_data[at]);
        const short len = static_cast<short>(text.size());
        out.push_back(static_cast<u_char>(len & 0xff)); out.push_back(static_cast<u_char>((len >> 8) & 0xff));
        for (short i = 0; i < len; ++i) out.push_back(encode(i, text[i]));
        cursor = at + sizeof(short) + old_len; // @note skip the old string
    };
    int changed = 0;
    for (const patch &p : patches)
    {
        const ::item &it = items[p.id];
        if (it.id != p.id || it.name_at == 0) continue;
        put_string(it.name_at, p.name, [&](short i, char c) { return static_cast<u_char>(c ^ token[(i + p.id) % token.length()]); });
        if (it.info_at != 0) put_string(it.info_at, p.info, [](short, char c) { return static_cast<u_char>(c); });
        ++changed;
    }
    copy_to(static_cast<u_int>(im_data.size()));

    std::vector<u_char> data = items_header(static_cast<u_int>(out.size()));
    data.insert(data.end(), out.begin(), out.end());
    im_data = std::move(data);
    printf("sponsor items: %d item(s) renamed\n", changed);
    return changed > 0;
}

static void parse_items()
{
    u_int pos = static_cast<u_int>(items_header(0).size());
    u_short version{};
    shift_pos(im_data, pos, version);
    u_int count{};
    shift_pos(im_data, pos, count);

    const std::string_view token{"PBG892FXX982ABC*"};
    for (u_int i = 0; i < count; ++i)
    {
        ::item item;

        shift_pos(im_data, pos, item.id); pos += 2; // @note downside im.id to 2 bit (short)
        shift_pos(im_data, pos, item.property);

        shift_pos(im_data, pos, item.cat);

        shift_pos(im_data, pos, item.type);
        pos += sizeof(u_char);

        item.name_at = pos;
        short len = *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);
        item.raw_name.resize(len);
        for (short i = 0; i < len; ++i) 
            item.raw_name[i] = im_data[pos] ^ token[(i + item.id) % token.length()], 
            ++pos;

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += sizeof(int);
        pos += sizeof(u_char);

        shift_pos(im_data, pos, item.ingredient);
        pos += sizeof(u_char);
        pos += sizeof(u_char);
        pos += sizeof(u_char);
        pos += sizeof(u_char);

        shift_pos(im_data, pos, item.collision);
        shift_pos(im_data, pos, item.hits);
        if (item.hits != 0) item.hits /= 6; // @note unknown reason behind why break hit is muliplied by 6 then having to divide by 6

        shift_pos(im_data, pos, item.hit_reset);

        if (item.type == type::CLOTHING) 
        {
            u_char cloth_type{};
            shift_pos(im_data, pos, item.cloth_type);
        }
        else pos += 1; // @note assign nothing
        if (item.type == type::AURA) item.cloth_type = clothing::ANCES;
        shift_pos(im_data, pos, item.rarity);

        pos += sizeof(u_char);
        {
            len = *reinterpret_cast<short*>(&im_data[pos]);
            pos += sizeof(short);
            std::string audio_directory{};
            audio_directory.assign(reinterpret_cast<char*>(&im_data[pos]), len);
            pos += len;

            if (audio_directory.ends_with(".mp3"))
                data_modify(im_data, pos, 0); // @todo make it only for IOS
        }
        pos += sizeof(int);

        pos += sizeof(std::array<u_char, 4ull>);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += sizeof(std::array<u_char, 16ull>);

        shift_pos(im_data, pos, item.tick);

        pos += sizeof(short);
        pos += sizeof(short);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += *(reinterpret_cast<short*>(&im_data[pos]));
        pos += sizeof(short);

        pos += sizeof(std::array<u_char, 80ull>);

        if (version >= 0x0b) // @date February 2019
        {
            pos += *(reinterpret_cast<short*>(&im_data[pos]));
            pos += sizeof(short);
        }
        if (version >= 0x0c) // @date October 2020
        {
            pos += sizeof(int);
            pos += sizeof(std::array<u_char, 9ull>);
        }
        if (version >= 0x0d) pos += sizeof(int); // @date May 2021
        if (version >= 0x0e) pos += sizeof(int); // @date October 2021
        if (version >= 0x0f)
        {
            pos += sizeof(std::array<u_char, 25ull>);
            pos += *(reinterpret_cast<short*>(&im_data[pos]));
            pos += sizeof(short);
        }
        if (version >= 0x10)
        {
            pos += *(reinterpret_cast<short*>(&im_data[pos]));
            pos += sizeof(short);
        }
        if (version >= 0x11) pos += sizeof(int); // @date April 2024
        if (version >= 0x12) pos += sizeof(int); // @date December 2024
        if (version >= 0x13) pos += sizeof(std::array<u_char, 9ull>);
        if (version >= 0x15) pos += sizeof(short); // @date September 2025
        if (version >= 0x16)
        {
            item.info_at = pos;
            len = *reinterpret_cast<short*>(&im_data[pos]);
            pos += sizeof(short);
            item.info.assign(reinterpret_cast<char*>(&im_data[pos]), len);
            pos += len;
        }
        if (version >= 0x17) 
        {
            shift_pos(im_data, pos, item.splice[0]);
            shift_pos(im_data, pos, item.splice[1]);
        }
        if (version >= 0x18) pos += sizeof(u_char); // @date December 2025
        if (version >= 0x19)
        {
            len = *reinterpret_cast<short*>(&im_data[pos]);
            pos += sizeof(short);
            if (len > (sizeof(short) + sizeof(int))) // @note {size} 0x00 0x00 0x00 0x00
            {
                // @todo add the string for the player punch FX
                pos += len;
            }
            pos += sizeof(int); // @note default: 0x00 0x00 0x00 0x00
        }
        if (version >= 0x1a) pos += sizeof(u_char); // May 2026
        if (version == 0x1b) // September 2026 | Immortal items
        {
            len = *reinterpret_cast<short*>(&im_data[pos]);
            pos += sizeof(short);
            if (len > 0) 
            {
                /* 0x36 */
                pos += sizeof(u_char);
            }
        }

        items.emplace_back(item);
    }
}
