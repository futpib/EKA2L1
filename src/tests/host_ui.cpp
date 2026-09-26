// Unit tests link the emulator services without the Qt frontend. Fail loudly if
// a test unexpectedly attempts to interact with a host UI.
#include <common/applauncher.h>
#include <drivers/ui/input_dialog.h>
#include <stdexcept>

namespace eka2l1::common {
    bool launch_browser(const std::string &) {
        throw std::runtime_error("Unexpected browser launch in unit test");
    }
}
namespace eka2l1::drivers::ui {
    bool open_input_view(const std::u16string &, int, input_dialog_complete_callback) {
        throw std::runtime_error("Unexpected input dialog in unit test");
    }
    void close_input_view() {
        throw std::runtime_error("Unexpected input dialog in unit test");
    }
    void show_yes_no_dialog(const std::u16string &, const std::u16string &, const std::u16string &,
                            yes_no_dialog_complete_callback) {
        throw std::runtime_error("Unexpected confirmation dialog in unit test");
    }
}
