#include <catch2/catch.hpp>
#include <config/config.h>
#include <cpu/12l1r/exclusive_monitor.h>
#include <cpu/dyncom/arm_dyncom.h>
#include <kernel/kernel.h>
#include <kernel/property.h>
#include <kernel/timing.h>
#include <vfs/vfs.h>

#include <cstring>
#include <new>

TEST_CASE("A newly defined integer property starts at zero", "property") {
    eka2l1::config::state config;
    eka2l1::io_system io;
    eka2l1::ntimer timer(1000000);
    eka2l1::arm::r12l1::exclusive_monitor monitor(1);
    eka2l1::arm::dyncom_core cpu(&monitor, 12);
    eka2l1::kernel_system kernel(nullptr, &timer, &io, &config, nullptr, nullptr, &cpu, nullptr);

    // Force nonzero backing storage: a fresh allocation can otherwise hide the bug.
    using property = eka2l1::service::property;
    alignas(property) unsigned char storage[sizeof(property)];
    std::memset(storage, 0xA5, sizeof(storage));
    property *value = new (storage) property(&kernel);
    struct cleanup {
        property *value;
        ~cleanup() { value->~property(); }
    } destroy{value};
    REQUIRE_FALSE(value->is_defined());
    value->define(eka2l1::service::property_type::int_data, 0);
    REQUIRE(value->get_int() == 0);
    REQUIRE(value->set_int(42));
    REQUIRE(value->get_int() == 42);
}
