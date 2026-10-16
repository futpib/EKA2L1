#include <catch2/catch.hpp>
#include <common/virtualmem.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

TEST_CASE("file mappings release their descriptors and views", "[virtualmem]") {
    char path[] = "/tmp/eka2l1-map-file-XXXXXX";
    const int file = mkstemp(path);
    REQUIRE(file >= 0);
    REQUIRE(write(file, "ROM!", 4) == 4);
    REQUIRE(close(file) == 0);

    const auto open_descriptors = [] {
        return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator());
    };
    const auto before = open_descriptors();

    for (int i = 0; i < 16; ++i) {
        auto *mapped = static_cast<char *>(eka2l1::common::map_file(path, prot_read_write, 0, true));
        REQUIRE(mapped != nullptr);
        REQUIRE(mapped[0] == 'R');
        mapped[0] = 'X';
        REQUIRE(eka2l1::common::unmap_file(mapped, 4));
        unsigned char resident = 0;
        REQUIRE(mincore(mapped, 4096, &resident) == -1);
        REQUIRE(errno == ENOMEM);
    }

    REQUIRE(open_descriptors() == before);

    char contents[4];
    const int check = open(path, O_RDONLY);
    REQUIRE(check >= 0);
    REQUIRE(read(check, contents, sizeof(contents)) == 4);
    REQUIRE(contents[0] == 'R');
    REQUIRE(close(check) == 0);

    REQUIRE(truncate(path, 0) == 0);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(eka2l1::common::map_file(path) == nullptr);
    }
    REQUIRE(open_descriptors() == before);
    REQUIRE(unlink(path) == 0);
}
#endif
