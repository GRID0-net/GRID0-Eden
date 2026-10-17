// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include "common/host_memory.h"
#include "common/sparse_large_vector.h"

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr auto ReadWrite = Common::MemoryPermission::ReadWrite;

bool CanAccess(u8* address, bool write = false) {
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        const rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        std::signal(SIGSEGV, SIG_DFL);
        std::signal(SIGBUS, SIG_DFL);
        volatile u8* pointer = address;
        if (write) {
            *pointer = 17;
        } else {
            const u8 value = *pointer;
            (void)value;
        }
        _exit(0);
    }
    int status{};
    REQUIRE(waitpid(child, &status, 0) == child);
    if (WIFSIGNALED(status)) {
        REQUIRE((WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
        return false;
    }
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    return true;
}
} // namespace

TEST_CASE("Host page aliases share backing storage", "[subpages]") {
    Common::HostMemory memory(0x100000, 0x100000);
    REQUIRE(memory.VirtualBasePointer() != nullptr);
    const size_t page = Common::HostPageSize;
    memory.Map(0, 0, page, ReadWrite, false);
    memory.Map(page, 0, page, ReadWrite, false);
    auto* base = memory.VirtualBasePointer();
    base[75] = 93;
    REQUIRE(base[page + 75] == 93);
    REQUIRE(memory.BackingBasePointer()[75] == 93);
}

TEST_CASE("Fresh partial host pages trap until their mapping is complete", "[subpages]") {
    if (Common::HostPageSize == 0x1000) {
        SKIP("Requires a host page larger than the Switch's 4 KiB pages");
    }
    Common::HostMemory memory(0x100000, 0x100000);
    const size_t page = Common::HostPageSize;
    auto* base = memory.VirtualBasePointer();
    REQUIRE_FALSE(CanAccess(base + 3 * page));
    memory.BackingBasePointer()[page + 0x1000] = 31;
    memory.Map(0x1000, page + 0x1000, 0x1000, ReadWrite, false);
    REQUIRE_FALSE(CanAccess(base));
    REQUIRE_FALSE(CanAccess(base + 0x1000));
    memory.Map(0, page, page, ReadWrite, false);
    REQUIRE(base[0x1000] == 31);
}

TEST_CASE("Removing one guest subpage cannot expose its neighbors", "[subpages]") {
    if (Common::HostPageSize == 0x1000) {
        SKIP("Requires a host page larger than the Switch's 4 KiB pages");
    }
    Common::HostMemory memory(0x100000, 0x100000);
    const size_t page = Common::HostPageSize;
    auto* base = memory.VirtualBasePointer();
    memory.Map(0, 0, page, ReadWrite, false);
    base[0] = 91;
    base[0x1000] = 92;
    memory.Unmap(0x1000, 0x1000, false);
    REQUIRE_FALSE(CanAccess(base));
    REQUIRE_FALSE(CanAccess(base + 0x1000));
    REQUIRE(memory.BackingBasePointer()[0] == 91);
    REQUIRE(memory.BackingBasePointer()[0x1000] == 92);
    memory.Map(0x1000, 0x1000, 0x1000, ReadWrite, false);
    REQUIRE(CanAccess(base, true));
    REQUIRE(base[0x1000] == 92);
}

TEST_CASE("Incompatible subpage aliases stay on the software path", "[subpages]") {
    if (Common::HostPageSize == 0x1000) {
        SKIP("Requires a host page larger than the Switch's 4 KiB pages");
    }
    Common::HostMemory memory(0x100000, 0x100000);
    const size_t page = Common::HostPageSize;
    auto* base = memory.VirtualBasePointer();
    memory.Map(0, 0, page, ReadWrite, false);
    base[0x1000] = 79;
    memory.BackingBasePointer()[page + 0x1000] = 80;
    memory.Map(0x1000, page + 0x1000, 0x1000, ReadWrite, false);
    REQUIRE_FALSE(CanAccess(base + 0x1000));
    REQUIRE(memory.BackingBasePointer()[0x1000] == 79);
    REQUIRE(memory.BackingBasePointer()[page + 0x1000] == 80);
    memory.Map(0x1000, 0x1000, 0x1000, ReadWrite, false);
    REQUIRE(base[0x1000] == 79);
}

TEST_CASE("Subpage permissions use the most restrictive host protection", "[subpages]") {
    if (Common::HostPageSize == 0x1000) {
        SKIP("Requires a host page larger than the Switch's 4 KiB pages");
    }
    Common::HostMemory memory(0x100000, 0x100000);
    const size_t page = Common::HostPageSize;
    auto* base = memory.VirtualBasePointer();
    memory.Map(0, 0, page, ReadWrite, false);
    memory.Protect(0x1000, 0x1000, Common::MemoryPermission::Read);
    REQUIRE(CanAccess(base));
    REQUIRE_FALSE(CanAccess(base, true));
    REQUIRE_FALSE(CanAccess(base + 0x1000, true));
    memory.Protect(0x1000, 0x1000, ReadWrite);
    REQUIRE(CanAccess(base, true));
    memory.Protect(0x1000, 0x1000, {});
    memory.FlushInstructionCache(0, page);
    REQUIRE_FALSE(CanAccess(base));
    memory.Protect(0x1000, 0x1000, ReadWrite);
    REQUIRE(CanAccess(base, true));
}

TEST_CASE("Direct guest addresses retain subpage mappings", "[subpages]") {
    Common::HostMemory memory(0x100000, 0x100000);
    const size_t page = Common::HostPageSize;
    auto* base = memory.VirtualBasePointer();
    memory.EnableDirectMappedAddress();
    memory.Map(reinterpret_cast<size_t>(base), 0, page, ReadWrite, false);
    base[75] = 19;
    REQUIRE(memory.BackingBasePointer()[75] == 19);
    memory.Unmap(reinterpret_cast<size_t>(base), page, false);
    REQUIRE_FALSE(CanAccess(base));
}

#if defined(ARCHITECTURE_arm64)
TEST_CASE("AArch64 instructions execute from a native host-page group", "[subpages]") {
    Common::HostMemory memory(0x100000, 0x100000);
    const size_t page = Common::HostPageSize;
    const u32 instructions[]{0xD2800540, 0xD65F03C0}; // mov x0, #42; ret
    std::memcpy(memory.BackingBasePointer(), instructions, sizeof(instructions));
    memory.Map(0, 0, page,
               Common::MemoryPermission::Read | Common::MemoryPermission::Execute, false);
    memory.FlushInstructionCache(0, page);
    const auto function = reinterpret_cast<u64 (*)()>(memory.VirtualBasePointer());
    REQUIRE(function() == 42);
}
#endif
#endif
