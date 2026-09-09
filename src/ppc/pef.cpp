#include "pef.h"

#include <cstring>

namespace ppc {
namespace {

inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
inline uint16_t rd16(const uint8_t* p) {
    return uint16_t((uint32_t(p[0]) << 8) | p[1]);
}
inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

// Decode a pattern-initialised data section (section kind 2).
bool expandPattern(const uint8_t* raw, size_t rawLen, uint32_t total,
                   std::vector<uint8_t>* out) {
    out->clear();
    out->reserve(total);
    size_t i = 0;
    auto argval = [&](uint32_t* v) -> bool {
        uint32_t r = 0;
        for (;;) {
            if (i >= rawLen) return false;
            uint8_t b = raw[i++];
            r = (r << 7) | (b & 0x7F);
            if (!(b & 0x80)) { *v = r; return true; }
        }
    };
    while (i < rawLen) {
        uint8_t b = raw[i++];
        uint32_t opcode = b >> 5;
        uint32_t count  = b & 0x1F;
        if (count == 0 && !argval(&count)) return false;
        switch (opcode) {
            case 0:
                out->insert(out->end(), count, 0);
                break;
            case 1:
                if (i + count > rawLen) return false;
                out->insert(out->end(), raw + i, raw + i + count);
                i += count;
                break;
            case 2: {
                uint32_t rep = 0;
                if (!argval(&rep)) return false;
                if (i + count > rawLen) return false;
                for (uint32_t k = 0; k <= rep; ++k)
                    out->insert(out->end(), raw + i, raw + i + count);
                i += count;
                break;
            }
            case 3: {
                uint32_t custom = 0, rep = 0;
                if (!argval(&custom) || !argval(&rep)) return false;
                if (i + count > rawLen) return false;
                const uint8_t* common = raw + i;
                i += count;
                for (uint32_t k = 0; k < rep; ++k) {
                    out->insert(out->end(), common, common + count);
                    if (i + custom > rawLen) return false;
                    out->insert(out->end(), raw + i, raw + i + custom);
                    i += custom;
                }
                out->insert(out->end(), common, common + count);
                break;
            }
            case 4: {
                uint32_t custom = 0, rep = 0;
                if (!argval(&custom) || !argval(&rep)) return false;
                for (uint32_t k = 0; k < rep; ++k) {
                    out->insert(out->end(), count, 0);
                    if (i + custom > rawLen) return false;
                    out->insert(out->end(), raw + i, raw + i + custom);
                    i += custom;
                }
                out->insert(out->end(), count, 0);
                break;
            }
            default:
                return false;
        }
    }
    out->resize(total, 0);
    return true;
}

}  // namespace

bool Pef::load(const uint8_t* data, size_t size, uint32_t codeBase,
               uint32_t dataBase, const std::vector<uint32_t>& importAddresses,
               std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (size < 40) return fail("PEF too small");
    if (memcmp(data, "Joy!peffpwpc", 12) != 0) return fail("not a PowerPC PEF container");

    codeBase_ = codeBase;
    dataBase_ = dataBase;
    importAddrs_ = importAddresses;

    uint16_t sectionCount = rd16(data + 32);
    size_t nameTable = 40 + size_t(sectionCount) * 28;
    if (nameTable > size) return fail("truncated section table");

    sections_.resize(sectionCount);
    const uint8_t* loaderRaw = nullptr;
    size_t loaderSize = 0;

    for (uint16_t s = 0; s < sectionCount; ++s) {
        const uint8_t* h = data + 40 + size_t(s) * 28;
        PefSection& sec = sections_[s];
        sec.nameOffset      = int32_t(rd32(h + 0));
        sec.defaultAddress  = rd32(h + 4);
        sec.totalLength     = rd32(h + 8);
        sec.unpackedLength  = rd32(h + 12);
        sec.containerLength = rd32(h + 16);
        sec.containerOffset = rd32(h + 20);
        sec.kind      = h[24];
        sec.shareKind = h[25];
        sec.alignment = h[26];
        if (size_t(sec.containerOffset) + sec.containerLength > size)
            return fail("section payload out of range");
        const uint8_t* raw = data + sec.containerOffset;
        if (sec.kind == 2) {
            if (!expandPattern(raw, sec.containerLength, sec.totalLength, &sec.image))
                return fail("bad pattern-initialised data");
        } else if (sec.kind == 4) {
            loaderRaw = raw;
            loaderSize = sec.containerLength;
        } else {
            sec.image.assign(raw, raw + sec.containerLength);
            sec.image.resize(sec.totalLength, 0);
        }
    }
    if (sections_.size() < 2) return fail("expected at least a code and a data section");
    if (!loaderRaw) return fail("no loader section");

    code_ = sections_[0].image;
    data_ = sections_[1].image;
    if (!relocate(loaderRaw, loaderSize, err)) return false;

    // Resolve the entry point.
    if (mainSection_ != 1) return fail("unexpected main section");
    if (size_t(mainOffset_) + 24 > data_.size()) return fail("main symbol out of range");
    const uint8_t* m = data_.data() + mainOffset_;
    uint32_t tvec;
    if (rd16(m) == 0xAAFE) {              // Mixed Mode RoutineDescriptor
        if (m[17] != 1) return fail("component entry is not PowerPC");
        tvec = rd32(m + 20);
    } else {
        tvec = dataBase_ + mainOffset_;
    }
    if (tvec < dataBase_ || size_t(tvec - dataBase_) + 8 > data_.size())
        return fail("entry tvector out of range");
    const uint8_t* tv = data_.data() + (tvec - dataBase_);
    entryCode_ = rd32(tv);
    entryToc_  = rd32(tv + 4);
    return true;
}

bool Pef::relocate(const uint8_t* d, size_t dLen, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (dLen < 56) return fail("loader section too small");

    mainSection_ = int32_t(rd32(d + 0));
    mainOffset_  = rd32(d + 4);
    uint32_t libCount   = rd32(d + 24);
    uint32_t symCount   = rd32(d + 28);
    uint32_t relSecs    = rd32(d + 32);
    relocInstrOffset_   = rd32(d + 36);
    uint32_t stringsOff = rd32(d + 40);

    auto lstr = [&](uint32_t off) {
        std::string s;
        size_t p = size_t(stringsOff) + off;
        while (p < dLen && d[p]) s.push_back(char(d[p++]));
        return s;
    };

    size_t p = 56;
    struct Lib { std::string name; uint32_t count, first; };
    std::vector<Lib> libs;
    for (uint32_t i = 0; i < libCount; ++i, p += 24) {
        if (p + 24 > dLen) return fail("truncated library table");
        libs.push_back({lstr(rd32(d + p)), rd32(d + p + 12), rd32(d + p + 16)});
    }
    imports_.resize(symCount);
    for (uint32_t i = 0; i < symCount; ++i, p += 4) {
        if (p + 4 > dLen) return fail("truncated import table");
        uint32_t v = rd32(d + p);
        imports_[i].symClass = uint8_t(v >> 24);
        imports_[i].name = lstr(v & 0xFFFFFF);
    }
    for (const Lib& L : libs)
        for (uint32_t j = 0; j < L.count && L.first + j < imports_.size(); ++j)
            imports_[L.first + j].library = L.name;

    for (uint32_t i = 0; i < relSecs; ++i, p += 12) {
        if (p + 12 > dLen) return fail("truncated relocation headers");
        relocHeaders_.push_back({rd16(d + p), rd32(d + p + 4), rd32(d + p + 8)});
    }
    if (importAddrs_.size() < imports_.size()) return fail("import address table too small");

    for (const RelocHeader& rh : relocHeaders_) {
        std::vector<uint8_t>* img =
            rh.section == 0 ? &code_ : rh.section == 1 ? &data_ : nullptr;
        if (!img) return fail("relocation targets an uninstantiated section");

        std::vector<uint16_t> instrs;
        instrs.reserve(rh.count);
        for (uint32_t k = 0; k < rh.count; ++k) {
            size_t o = size_t(relocInstrOffset_) + rh.offset + size_t(k) * 2;
            if (o + 2 > dLen) return fail("truncated relocation stream");
            instrs.push_back(rd16(d + o));
        }

        uint32_t pos = 0, importIndex = 0;
        uint32_t sectionC = codeBase_, sectionD = dataBase_;
        auto secAddr = [&](uint32_t idx) { return idx == 0 ? codeBase_ : dataBase_; };
        auto load = [&](uint32_t o, uint32_t* v) {
            if (size_t(o) + 4 > img->size()) return false;
            *v = rd32(img->data() + o); return true;
        };
        auto store = [&](uint32_t o, uint32_t v) {
            if (size_t(o) + 4 > img->size()) return false;
            wr32(img->data() + o, v); return true;
        };
        auto addTo = [&](uint32_t o, uint32_t delta) {
            uint32_t v; if (!load(o, &v)) return false; return store(o, v + delta);
        };

        for (size_t i = 0; i < instrs.size();) {
            size_t start = i;
            uint16_t instr = instrs[i++];
            uint32_t top2 = uint32_t(instr) >> 14;
            if (top2 == 0) {                       // DDAT: skip, then relocate by data
                uint32_t skip = (instr >> 6) & 0xFF, cnt = instr & 0x3F;
                pos += skip * 4;
                for (uint32_t k = 0; k < cnt; ++k, pos += 4)
                    if (!addTo(pos, sectionD)) return fail("DDAT out of range");
            } else if (top2 == 1) {                // 7-bit opcode, 9-bit operand
                uint32_t op = (instr >> 9) & 0x1F, arg = instr & 0x1FF;
                switch (op) {
                    case 0: for (uint32_t k = 0; k <= arg; ++k, pos += 4)
                                if (!addTo(pos, sectionC)) return fail("CODE reloc");
                            break;
                    case 1: for (uint32_t k = 0; k <= arg; ++k, pos += 4)
                                if (!addTo(pos, sectionD)) return fail("DATA reloc");
                            break;
                    case 2: for (uint32_t k = 0; k <= arg; ++k, pos += 12)
                                if (!addTo(pos, sectionC) || !addTo(pos + 4, sectionD))
                                    return fail("DESC reloc");
                            break;
                    case 3: for (uint32_t k = 0; k <= arg; ++k, pos += 8)
                                if (!addTo(pos, sectionC) || !addTo(pos + 4, sectionD))
                                    return fail("DSC2 reloc");
                            break;
                    case 4: for (uint32_t k = 0; k <= arg; ++k, pos += 8)
                                if (!addTo(pos, sectionD)) return fail("VTBL reloc");
                            break;
                    case 5: for (uint32_t k = 0; k <= arg; ++k, pos += 4) {
                                if (importIndex >= importAddrs_.size())
                                    return fail("import run overflow");
                                if (!addTo(pos, importAddrs_[importIndex++]))
                                    return fail("SYMR reloc");
                            }
                            break;
                    case 6: if (arg >= importAddrs_.size()) return fail("SYMB index");
                            if (!addTo(pos, importAddrs_[arg])) return fail("SYMB reloc");
                            pos += 4; importIndex = arg + 1;
                            break;
                    case 7: sectionC = secAddr(arg); break;
                    case 8: sectionD = secAddr(arg); break;
                    case 9: if (!addTo(pos, secAddr(arg))) return fail("SECN reloc");
                            pos += 4;
                            break;
                    default: return fail("unknown small relocation opcode");
                }
            } else {
                uint32_t nib = (uint32_t(instr) >> 12) & 0xF;
                if (nib == 0x8) {                          // DELTA
                    pos += (instr & 0x0FFF) + 1;
                } else if (nib == 0x9) {                   // RPT
                    uint32_t chunk = ((instr >> 8) & 0x0F) + 1;
                    uint32_t rep   = (instr & 0xFF) + 1;
                    if (chunk > start) return fail("RPT underflow");
                    std::vector<uint16_t> blk(instrs.begin() + (start - chunk),
                                              instrs.begin() + start);
                    std::vector<uint16_t> expanded;
                    for (uint32_t k = 0; k < rep; ++k)
                        expanded.insert(expanded.end(), blk.begin(), blk.end());
                    instrs.insert(instrs.begin() + i, expanded.begin(), expanded.end());
                } else if (nib == 0xA) {                   // two-word forms
                    if (i >= instrs.size()) return fail("truncated large relocation");
                    uint16_t next = instrs[i++];
                    uint32_t sub = (instr >> 10) & 0x3;
                    if (sub == 0) {                        // LABS
                        pos = ((uint32_t(instr) & 0x3FF) << 16) | next;
                    } else if (sub == 1) {                 // LSYM
                        uint32_t idx = ((uint32_t(instr) & 0x3FF) << 16) | next;
                        if (idx >= importAddrs_.size()) return fail("LSYM index");
                        if (!addTo(pos, importAddrs_[idx])) return fail("LSYM reloc");
                        pos += 4; importIndex = idx + 1;
                    } else if (sub == 2) {                 // LRPT
                        uint32_t chunk = ((instr >> 6) & 0x0F) + 1;
                        uint32_t rep = (((uint32_t(instr) & 0x3F) << 16) | next) + 1;
                        if (chunk > start) return fail("LRPT underflow");
                        std::vector<uint16_t> blk(instrs.begin() + (start - chunk),
                                                  instrs.begin() + start);
                        std::vector<uint16_t> expanded;
                        for (uint32_t k = 0; k < rep; ++k)
                            expanded.insert(expanded.end(), blk.begin(), blk.end());
                        instrs.insert(instrs.begin() + i, expanded.begin(), expanded.end());
                    } else {                               // LSEC
                        uint32_t s2 = (instr >> 6) & 0x0F;
                        uint32_t idx = ((uint32_t(instr) & 0x3F) << 16) | next;
                        if (s2 == 0) { if (!addTo(pos, secAddr(idx))) return fail("LSEC");
                                       pos += 4; }
                        else if (s2 == 1) sectionC = secAddr(idx);
                        else if (s2 == 2) sectionD = secAddr(idx);
                        else return fail("bad LSEC subopcode");
                    }
                } else {
                    return fail("unknown relocation instruction");
                }
            }
        }
    }
    return true;
}

}  // namespace ppc
