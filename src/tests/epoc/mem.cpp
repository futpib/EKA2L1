#include <catch2/catch.hpp>
#include <mem/page.h>

TEST_CASE("Executable mapping cache follows mapping lifetimes", "[mem]") {
    using namespace eka2l1::mem;
    executable_mapping_cache cache;
    std::uint8_t first[4096]{}, second[4096]{};
    page_info page{};
    unsigned resolutions = 0;
    auto resolve = [&] { ++resolutions; return &page; };
    page.assign(first, prot_exec);
    REQUIRE(cache.lookup(1,0x1000,12,resolve).host_addr == first);
    REQUIRE(cache.lookup(1,0x1000,12,resolve).host_addr == first);
    REQUIRE(resolutions == 1);
    page.assign(second, prot_read);
    REQUIRE(cache.lookup(1,0x1000,12,resolve).host_addr == second);
    REQUIRE(cache.lookup(1,0x1000,12,resolve).perm == prot_read);
    page.clear();
    REQUIRE(cache.lookup(1,0x1000,12,resolve).host_addr == nullptr);
    page.assign(first, prot_exec);
    REQUIRE(cache.lookup(2,0x1000,12,resolve).host_addr == first);
    auto before = resolutions;
    REQUIRE(cache.lookup(1,0x1000,12,resolve).host_addr == first);
    REQUIRE(resolutions == before + 1);
    page_directory directory(12,1);
    directory.reset();
    before = resolutions;
    cache.lookup(1,0x1000,12,resolve);
    REQUIRE(resolutions == before + 1);
    directory.occupied(true);
    directory.occupied(false);
    before = resolutions;
    cache.lookup(1,0x1000,12,resolve);
    REQUIRE(resolutions == before + 1);
    auto epoch = mapping_generation.load();
    { page_table table(0,12); REQUIRE(directory.set_page_table(0,&table)); }
    REQUIRE(mapping_generation.load() > epoch);
    REQUIRE(cache.lookup(3,0x2000,12,[]() -> page_info * {return nullptr;}).host_addr == nullptr);
}
