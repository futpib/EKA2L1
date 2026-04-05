#pragma once

#include <common/types.h>
#include <system/installation/common.h>
#include <system/installation/firmware.h>

#include <functional>
#include <string>
#include <vector>

namespace eka2l1 {
    class device_manager;

    enum device_install_method {
        device_install_method_dump_rpkg,
        device_install_method_dump_rom_only,
        device_install_method_firmware
    };

    struct device_install_params {
        device_install_method method;
        std::string storage;
        std::string rom_path;
        std::string rpkg_path;
        std::string vpl_path;
    };

    device_installation_error install_device(device_manager *dvcmngr, const device_install_params &params,
        device_firmware_choose_variant_callback variant_cb, progress_changed_callback progress_cb,
        cancel_requested_callback cancel_cb);
}
