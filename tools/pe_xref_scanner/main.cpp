// PeXrefScanner - purely static analysis tool.
//
// Reads a PE (.exe) file from disk, locates known string literals (the
// authorization-gate strings found during the earlier string-scan phase of
// this project, see docs/CHANGELOG.md), converts their file offsets to
// virtual addresses via the PE section table, then disassembles the .text
// section with Zydis looking for instructions that reference those
// addresses (RIP-relative LEA/MOV, or relative CALL/JMP). For each match,
// dumps a window of surrounding disassembly to a text file for manual
// review.
//
// Deliberately does NOT touch a running process - this only ever reads the
// .exe file from disk. Safe to run against a live server's binary while it
// is running (Windows allows shared read access to an executing PE file).
//
// This is our own, independent static analysis of the SCUM server binary we
// are licensed to run - no code or binary from herbie96x/SCUM-RCON or
// jasonuithol/SCUM-Mods (DeveloperMode) is examined, used, or referenced.
// Only the general publicly-described concept (find the authorization gate
// via its string references) is being independently reimplemented here.

#include <Zydis/Zydis.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct Section
    {
        std::string name;
        std::uint32_t virtualAddress;
        std::uint32_t virtualSize;
        std::uint32_t pointerToRawData;
        std::uint32_t sizeOfRawData;
        bool executable;
    };

    struct PeImage
    {
        std::vector<std::uint8_t> bytes;
        std::uint64_t imageBase;
        std::vector<Section> sections;

        // Converts a file offset (position within `bytes`) to the RVA it
        // would have when the PE is loaded, by finding the section whose
        // raw-data range contains this offset.
        std::int64_t fileOffsetToRva(std::uint64_t fileOffset) const
        {
            for (const auto& s : sections)
            {
                if (fileOffset >= s.pointerToRawData && fileOffset < s.pointerToRawData + s.sizeOfRawData)
                {
                    return static_cast<std::int64_t>(s.virtualAddress) + (fileOffset - s.pointerToRawData);
                }
            }
            return -1;
        }

        std::int64_t rvaToFileOffset(std::uint64_t rva) const
        {
            for (const auto& s : sections)
            {
                if (rva >= s.virtualAddress && rva < s.virtualAddress + s.virtualSize)
                {
                    return static_cast<std::int64_t>(s.pointerToRawData) + (rva - s.virtualAddress);
                }
            }
            return -1;
        }

        std::uint64_t fileOffsetToVa(std::uint64_t fileOffset) const
        {
            const auto rva = fileOffsetToRva(fileOffset);
            return rva < 0 ? 0 : imageBase + static_cast<std::uint64_t>(rva);
        }

        const Section* findExecutableSection() const
        {
            for (const auto& s : sections)
            {
                if (s.executable) return &s;
            }
            return nullptr;
        }
    };

    bool loadPe(const std::string& path, PeImage& out)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::cerr << "Could not open: " << path << "\n";
            return false;
        }
        const auto size = static_cast<std::size_t>(file.tellg());
        file.seekg(0);
        out.bytes.resize(size);
        file.read(reinterpret_cast<char*>(out.bytes.data()), static_cast<std::streamsize>(size));

        const auto& b = out.bytes;
        if (size < 0x40 || b[0] != 'M' || b[1] != 'Z')
        {
            std::cerr << "Not a valid PE file (missing MZ header)\n";
            return false;
        }
        const std::uint32_t peOffset = *reinterpret_cast<const std::uint32_t*>(&b[0x3C]);
        if (peOffset + 24 >= size || b[peOffset] != 'P' || b[peOffset + 1] != 'E')
        {
            std::cerr << "Not a valid PE file (missing PE signature)\n";
            return false;
        }

        const std::uint16_t numberOfSections = *reinterpret_cast<const std::uint16_t*>(&b[peOffset + 6]);
        const std::uint16_t sizeOfOptionalHeader = *reinterpret_cast<const std::uint16_t*>(&b[peOffset + 20]);
        const std::size_t optionalHeaderOffset = peOffset + 24;
        const std::uint16_t magic = *reinterpret_cast<const std::uint16_t*>(&b[optionalHeaderOffset]);
        if (magic != 0x20B)
        {
            std::cerr << "Only PE32+ (x64) is supported (magic=0x" << std::hex << magic << ")\n";
            return false;
        }
        out.imageBase = *reinterpret_cast<const std::uint64_t*>(&b[optionalHeaderOffset + 24]);

        const std::size_t sectionTableOffset = optionalHeaderOffset + sizeOfOptionalHeader;
        for (std::uint16_t i = 0; i < numberOfSections; ++i)
        {
            const std::size_t entry = sectionTableOffset + i * 40;
            Section s;
            char name[9] = {};
            std::memcpy(name, &b[entry], 8);
            s.name = name;
            s.virtualSize = *reinterpret_cast<const std::uint32_t*>(&b[entry + 8]);
            s.virtualAddress = *reinterpret_cast<const std::uint32_t*>(&b[entry + 12]);
            s.sizeOfRawData = *reinterpret_cast<const std::uint32_t*>(&b[entry + 16]);
            s.pointerToRawData = *reinterpret_cast<const std::uint32_t*>(&b[entry + 20]);
            const std::uint32_t characteristics = *reinterpret_cast<const std::uint32_t*>(&b[entry + 36]);
            s.executable = (characteristics & 0x20000000) != 0; // IMAGE_SCN_MEM_EXECUTE
            out.sections.push_back(s);
        }
        return true;
    }

    struct StringHit
    {
        std::string label;
        std::string encoding;
        std::uint64_t va;
    };

    std::vector<std::size_t> findAllAscii(const std::vector<std::uint8_t>& bytes, const std::string& needle)
    {
        std::vector<std::size_t> hits;
        if (needle.empty()) return hits;
        auto it = bytes.begin();
        while (true)
        {
            it = std::search(it, bytes.end(), needle.begin(), needle.end());
            if (it == bytes.end()) break;
            hits.push_back(static_cast<std::size_t>(it - bytes.begin()));
            ++it;
        }
        return hits;
    }

    std::vector<std::size_t> findAllUtf16(const std::vector<std::uint8_t>& bytes, const std::string& needleAscii)
    {
        std::vector<std::uint8_t> needle;
        needle.reserve(needleAscii.size() * 2);
        for (char c : needleAscii)
        {
            needle.push_back(static_cast<std::uint8_t>(c));
            needle.push_back(0);
        }
        return findAllAscii(bytes, std::string(reinterpret_cast<const char*>(needle.data()), needle.size()));
    }

    void dumpContext(const PeImage& image, const ZydisDecoder& decoder, std::uint64_t xrefVa,
                      std::size_t xrefFileOffset, const std::string& reason, std::ofstream& out)
    {
        out << "\n=== XREF: " << reason << " at VA 0x" << std::hex << xrefVa << " (file offset 0x"
            << xrefFileOffset << std::dec << ") ===\n";

        constexpr std::size_t BEFORE = 400;  // bytes, rough - re-synced by decoding forward from an earlier point
        constexpr std::size_t AFTER = 600;
        const std::size_t windowStart = xrefFileOffset > BEFORE ? xrefFileOffset - BEFORE : 0;
        const std::size_t windowEnd = std::min(image.bytes.size(), xrefFileOffset + AFTER);

        std::uint64_t runtimeAddress = image.fileOffsetToVa(windowStart);
        std::size_t offset = windowStart;
        while (offset < windowEnd)
        {
            ZydisDisassembledInstruction instr;
            const auto status = ZydisDisassembleIntel(
                    ZYDIS_MACHINE_MODE_LONG_64, runtimeAddress,
                    image.bytes.data() + offset, windowEnd - offset, &instr);
            if (!ZYAN_SUCCESS(status))
            {
                out << "  <decode failed at file offset 0x" << std::hex << offset << std::dec << ">\n";
                ++offset;
                ++runtimeAddress;
                continue;
            }
            const char* marker = (offset <= xrefFileOffset && xrefFileOffset < offset + instr.info.length) ? " <== XREF HERE" : "";
            out << "  0x" << std::hex << runtimeAddress << std::dec << "  " << instr.text << marker << "\n";
            offset += instr.info.length;
            runtimeAddress += instr.info.length;
        }
    }

    // Straight linear disassembly starting at a known virtual address (as
    // opposed to dumpContext, which centers a window around an xref hit).
    // Used for explicitly-targeted follow-up analysis once a function
    // address is already known (e.g. the callee of an xref, not the xref
    // site itself). Stops early at `ret` if one is found with no
    // outstanding unresolved jumps tracked - this is a best-effort human-
    // readable dump, not a real control-flow-aware decompiler.
    void dumpFunctionAt(const PeImage& image, std::uint64_t va, std::size_t maxBytes,
                         const std::string& label, std::ofstream& out)
    {
        out << "\n=== FUNCTION DUMP: " << label << " at VA 0x" << std::hex << va << std::dec << " ===\n";
        const auto rva = va - image.imageBase;
        const auto fileOffsetSigned = image.rvaToFileOffset(rva);
        if (fileOffsetSigned < 0)
        {
            out << "  <VA not found in any section>\n";
            return;
        }
        std::size_t offset = static_cast<std::size_t>(fileOffsetSigned);
        const std::size_t end = std::min(image.bytes.size(), offset + maxBytes);
        std::uint64_t runtimeAddress = va;
        while (offset < end)
        {
            ZydisDisassembledInstruction instr;
            const auto status = ZydisDisassembleIntel(
                    ZYDIS_MACHINE_MODE_LONG_64, runtimeAddress,
                    image.bytes.data() + offset, end - offset, &instr);
            if (!ZYAN_SUCCESS(status))
            {
                out << "  <decode failed at file offset 0x" << std::hex << offset << std::dec << ">\n";
                ++offset;
                ++runtimeAddress;
                continue;
            }
            out << "  0x" << std::hex << runtimeAddress << std::dec << "  " << instr.text << "\n";
            offset += instr.info.length;
            runtimeAddress += instr.info.length;
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: PeXrefScanner <path-to-exe> <output.txt>\n";
        return 1;
    }
    const std::string exePath = argv[1];
    const std::string outputPath = argv[2];

    PeImage image;
    if (!loadPe(exePath, image))
    {
        return 1;
    }
    std::cerr << "Loaded " << image.bytes.size() << " bytes, ImageBase=0x" << std::hex << image.imageBase << std::dec << "\n";
    for (const auto& s : image.sections)
    {
        std::cerr << "  Section " << s.name << " VA=0x" << std::hex << s.virtualAddress
                   << " size=0x" << s.virtualSize << " raw=0x" << s.pointerToRawData
                   << " rawsize=0x" << s.sizeOfRawData << std::dec
                   << (s.executable ? " [EXEC]" : "") << "\n";
    }

    // Known strings from the earlier reflection/string-scan phase of this
    // project (see docs/CHANGELOG.md, "Erste Reverse-Engineering-Ergebnisse").
    const std::vector<std::string> targets = {
        "Player must be developer",
        "Not authorized to execute command",
        "Command is on cooldown. Try again later.",
        "Command is disabled in shipping build",
    };

    std::vector<StringHit> hits;
    for (const auto& target : targets)
    {
        for (auto off : findAllAscii(image.bytes, target))
        {
            const auto rva = image.fileOffsetToRva(off);
            if (rva >= 0) hits.push_back({target, "ASCII", image.imageBase + static_cast<std::uint64_t>(rva)});
        }
        for (auto off : findAllUtf16(image.bytes, target))
        {
            const auto rva = image.fileOffsetToRva(off);
            if (rva >= 0) hits.push_back({target, "UTF-16", image.imageBase + static_cast<std::uint64_t>(rva)});
        }
    }

    std::cerr << "Found " << hits.size() << " string occurrence(s) to search for xrefs to.\n";
    for (const auto& h : hits)
    {
        std::cerr << "  [" << h.encoding << "] \"" << h.label << "\" @ VA 0x" << std::hex << h.va << std::dec << "\n";
    }

    const Section* text = image.findExecutableSection();
    if (!text)
    {
        std::cerr << "No executable section found.\n";
        return 1;
    }
    std::cerr << "Scanning executable section " << text->name << " (" << text->sizeOfRawData << " bytes) for xrefs...\n";

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    std::ofstream out(outputPath);
    out << "PeXrefScanner report for " << exePath << "\n";
    out << "ImageBase=0x" << std::hex << image.imageBase << std::dec << "\n";
    out << "Searched strings:\n";
    for (const auto& h : hits)
    {
        out << "  [" << h.encoding << "] \"" << h.label << "\" @ VA 0x" << std::hex << h.va << std::dec << "\n";
    }

    std::size_t offset = text->pointerToRawData;
    const std::size_t end = std::min(image.bytes.size(), static_cast<std::size_t>(text->pointerToRawData) + text->sizeOfRawData);
    std::uint64_t runtimeAddress = image.imageBase + text->virtualAddress;
    std::size_t instructionCount = 0;
    std::size_t xrefCount = 0;

    while (offset < end)
    {
        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        const auto status = ZydisDecoderDecodeFull(&decoder, image.bytes.data() + offset, end - offset, &instruction, operands);
        if (!ZYAN_SUCCESS(status))
        {
            ++offset;
            ++runtimeAddress;
            continue;
        }
        ++instructionCount;

        for (int i = 0; i < instruction.operand_count; ++i)
        {
            const auto& op = operands[i];
            const bool isRipRelMem = op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.base == ZYDIS_REGISTER_RIP;
            const bool isRelativeImm = op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && op.imm.is_relative;
            if (!isRipRelMem && !isRelativeImm) continue;

            std::uint64_t absoluteAddress = 0;
            if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &op, runtimeAddress, &absoluteAddress))) continue;

            for (const auto& h : hits)
            {
                // Small tolerance window: a reference might point a few bytes
                // into the string (rare) or we might be matching the start
                // exactly (common case).
                if (absoluteAddress >= h.va && absoluteAddress < h.va + 64)
                {
                    ++xrefCount;
                    dumpContext(image, decoder, runtimeAddress, offset,
                                "[" + h.encoding + "] \"" + h.label + "\"", out);
                    break;
                }
            }
        }

        offset += instruction.length;
        runtimeAddress += instruction.length;
    }

    std::cerr << "Decoded " << instructionCount << " instructions, found " << xrefCount << " xref(s) to target strings.\n";
    out << "\n\nSummary: decoded " << instructionCount << " instructions, found " << xrefCount << " xref(s).\n";

    // Optional: dump specific already-known function addresses directly
    // (follow-up analysis once an xref pointed us at a callee address).
    // Usage: PeXrefScanner <exe> <out.txt> 0x141A45AA0 0x141A4D050:4096 ...
    // (":<bytes>" suffix overrides the default 1024-byte dump length, for
    // functions too large to fit the default window.)
    for (int i = 3; i < argc; ++i)
    {
        std::string arg = argv[i];
        std::size_t maxBytes = 1024;
        const auto colonPos = arg.find(':');
        if (colonPos != std::string::npos)
        {
            maxBytes = std::stoull(arg.substr(colonPos + 1));
            arg = arg.substr(0, colonPos);
        }
        const std::uint64_t va = std::stoull(arg, nullptr, 16);
        std::cerr << "Dumping function at VA 0x" << std::hex << va << std::dec
                   << " (" << maxBytes << " bytes)...\n";
        dumpFunctionAt(image, va, maxBytes, argv[i], out);
    }

    std::cout << "Done. Decoded " << instructionCount << " instructions, found " << xrefCount << " xref(s). Report: " << outputPath << "\n";
    return 0;
}
