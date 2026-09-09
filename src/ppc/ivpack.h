// Reader for the .ivp resource packs the installer ships.
//
// Each pack is one classic Mac OS resource fork flattened into a single file:
// the component itself (infovox210.ivp) or one language's voices.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ppc {

struct ResKey {
    uint32_t type;   // four-char code, e.g. 'IVvx'
    int32_t  id;
    bool operator<(const ResKey& o) const {
        return type != o.type ? type < o.type : id < o.id;
    }
};

class ResourcePack {
public:
    bool loadFile(const std::wstring& path, std::string* err);

    const std::vector<uint8_t>* find(uint32_t type, int32_t id) const;
    std::vector<ResKey> keys() const;
    const std::string& name(uint32_t type, int32_t id) const;
    bool empty() const { return items_.empty(); }

private:
    std::map<ResKey, std::vector<uint8_t>> items_;
    std::map<ResKey, std::string> names_;
};

// 'ABCD' style literal without relying on the compiler's multi-char constants.
constexpr uint32_t FourCC(const char (&s)[5]) {
    return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
           (uint32_t(uint8_t(s[2])) << 8)  |  uint32_t(uint8_t(s[3]));
}

}  // namespace ppc
