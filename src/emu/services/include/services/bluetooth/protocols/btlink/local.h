/*
 * Copyright (c) 2026 EKA2L1 Team
 * This file is part of EKA2L1 project and is licensed under GPLv3 or later.
 */

#pragma once

#include <services/bluetooth/btmidman.h>
#include <services/bluetooth/protocols/common.h>
#include <services/socket/protocol.h>
#include <utils/err.h>
#include <utils/reqsts.h>

namespace eka2l1::epoc::bt {
    // Local device-name queries do not require a host networking loop.
    class btlink_local_resolver final : public socket::host_resolver {
        midman *mid_;
    public:
        explicit btlink_local_resolver(midman *mid) : mid_(mid) {}
        std::u16string host_name() const override { return mid_->device_name(); }
        bool host_name(const std::u16string &name) override {
            mid_->device_name(name);
            return true;
        }
        void get_by_address(socket::saddress &, socket::name_entry *, epoc::notify_info &info) override {
            info.complete(epoc::error_not_supported);
        }
        void get_by_name(socket::name_entry *, epoc::notify_info &info) override {
            info.complete(epoc::error_not_supported);
        }
        void next(socket::name_entry *, epoc::notify_info &info) override {
            info.complete(epoc::error_not_supported);
        }
    };

    class btlink_local_protocol final : public socket::protocol {
        midman *mid_;
    public:
        explicit btlink_local_protocol(midman *mid, bool oldarch) : socket::protocol(oldarch), mid_(mid) {}
        std::u16string name() const override { return u"BTLinkManager"; }
        std::vector<std::uint32_t> family_ids() const override { return { BTADDR_PROTOCOL_FAMILY_ID }; }
        std::vector<std::uint32_t> supported_ids() const override { return { BTLINK_MANAGER_PROTOCOL_ID }; }
        epoc::version ver() const override { return { 0, 0, 1 }; }
        socket::byte_order get_byte_order() const override { return socket::byte_order_little_endian; }
        std::unique_ptr<socket::host_resolver> make_host_resolver(std::uint32_t, std::uint32_t) override {
            return std::make_unique<btlink_local_resolver>(mid_);
        }
        std::unique_ptr<socket::socket> make_socket(std::uint32_t, std::uint32_t, socket::socket_type) override {
            return nullptr;
        }
        std::unique_ptr<socket::net_database> make_net_database(std::uint32_t, std::uint32_t) override {
            return nullptr;
        }
    };
}
