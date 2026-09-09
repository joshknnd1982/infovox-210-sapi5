#include "ivpack.h"

#include <cstdio>
#include <cstring>

namespace ppc {
namespace {
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
const std::string kEmpty;
}  // namespace

bool ResourcePack::loadFile(const std::wstring& path, std::string* err) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) { if (err) *err = "cannot open resource pack"; return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size_t(len < 0 ? 0 : len));
    if (!buf.empty() && fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        fclose(f);
        if (err) *err = "short read on resource pack";
        return false;
    }
    fclose(f);

    if (buf.size() < 12 || memcmp(buf.data(), "IVXPACK1", 8) != 0) {
        if (err) *err = "not an Infovox resource pack";
        return false;
    }
    uint32_t count = rd32(buf.data() + 8);
    const size_t entSize = 4 + 4 + 4 + 4 + 32;
    if (buf.size() < 12 + size_t(count) * entSize) {
        if (err) *err = "truncated resource table";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = buf.data() + 12 + size_t(i) * entSize;
        ResKey k{rd32(e), int32_t(rd32(e + 4))};
        uint32_t off = rd32(e + 8), size = rd32(e + 12);
        if (size_t(off) + size > buf.size()) {
            if (err) *err = "resource payload out of range";
            return false;
        }
        items_[k].assign(buf.begin() + off, buf.begin() + off + size);
        const char* nm = reinterpret_cast<const char*>(e + 16);
        names_[k].assign(nm, strnlen(nm, 32));
    }
    return true;
}

const std::vector<uint8_t>* ResourcePack::find(uint32_t type, int32_t id) const {
    auto it = items_.find(ResKey{type, id});
    return it == items_.end() ? nullptr : &it->second;
}

std::vector<ResKey> ResourcePack::keys() const {
    std::vector<ResKey> out;
    out.reserve(items_.size());
    for (const auto& kv : items_) out.push_back(kv.first);
    return out;
}

const std::string& ResourcePack::name(uint32_t type, int32_t id) const {
    auto it = names_.find(ResKey{type, id});
    return it == names_.end() ? kEmpty : it->second;
}

}  // namespace ppc
