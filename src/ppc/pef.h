// PEF (Preferred Executable Format) loader for classic Mac OS PowerPC fragments.
//
// Infovox 210 ships as a Component Manager component whose PowerPC build is a
// PEF container in resource 'PPCm' 128.  This loader instantiates the code and
// data sections at chosen addresses, applies the PEF relocations, and resolves
// the fragment's entry point.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ppc {

struct PefSection {
    int32_t  nameOffset = 0;
    uint32_t defaultAddress = 0;
    uint32_t totalLength = 0;
    uint32_t unpackedLength = 0;
    uint32_t containerLength = 0;
    uint32_t containerOffset = 0;
    uint8_t  kind = 0;          // 0 code, 1 unpacked data, 2 pattern data, 4 loader
    uint8_t  shareKind = 0;
    uint8_t  alignment = 0;
    std::vector<uint8_t> image;  // expanded, totalLength bytes
};

struct PefImport {
    std::string name;
    std::string library;
    uint8_t     symClass = 0;
};

// A loaded fragment: sections placed in memory and relocated.
class Pef {
public:
    // `importAddress(i)` must return the address the i'th imported symbol
    // resolves to (for tvector imports, the address of a fake TVector).
    bool load(const uint8_t* data, size_t size, uint32_t codeBase,
              uint32_t dataBase,
              const std::vector<uint32_t>& importAddresses,
              std::string* err);

    const std::vector<uint8_t>& code() const { return code_; }
    const std::vector<uint8_t>& data() const { return data_; }
    const std::vector<PefImport>& imports() const { return imports_; }

    // Fragment entry.  For a Component Manager component the PEF main symbol is
    // a Mixed Mode RoutineDescriptor, not a TVector; this follows it through.
    uint32_t entryCode() const { return entryCode_; }
    uint32_t entryToc()  const { return entryToc_; }

private:
    bool relocate(const uint8_t* loader, size_t loaderSize, std::string* err);

    std::vector<uint8_t>   code_, data_;
    std::vector<PefSection> sections_;
    std::vector<PefImport> imports_;
    uint32_t codeBase_ = 0, dataBase_ = 0;
    uint32_t entryCode_ = 0, entryToc_ = 0;
    int32_t  mainSection_ = -1;
    uint32_t mainOffset_ = 0;
    std::vector<uint32_t> importAddrs_;
    // relocation section headers
    struct RelocHeader { uint16_t section; uint32_t count; uint32_t offset; };
    std::vector<RelocHeader> relocHeaders_;
    uint32_t relocInstrOffset_ = 0;
};

}  // namespace ppc
