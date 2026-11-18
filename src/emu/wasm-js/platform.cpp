/*
 * Copyright (c) 2026 EKA2L1 Team.
 * This file is part of EKA2L1 project and is licensed under GPLv3 or later.
 */
#include <drivers/ui/input_dialog.h>
#include <stdexcept>

namespace eka2l1::drivers::ui {
    bool open_input_view(const std::u16string &, int, input_dialog_complete_callback) {
        throw std::runtime_error("Host text input is unavailable in the cooperative JavaScript profile");
    }

    void close_input_view() {
        throw std::runtime_error("Host text input is unavailable in the cooperative JavaScript profile");
    }

    dialog_cancel_callback show_yes_no_dialog(const std::u16string &, const std::u16string &,
        const std::u16string &, yes_no_dialog_complete_callback) {
        throw std::runtime_error("Host dialogs are unavailable in the cooperative JavaScript profile");
    }
}
