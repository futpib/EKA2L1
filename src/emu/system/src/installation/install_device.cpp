#include <system/installation/install_device.h>
#include <system/installation/rpkg.h>
#include <system/installation/firmware.h>
#include <system/devices.h>

#include <common/fileutils.h>
#include <common/log.h>
#include <common/path.h>

namespace eka2l1 {
    device_installation_error install_device(device_manager *dvcmngr, const device_install_params &params,
        device_firmware_choose_variant_callback variant_cb, progress_changed_callback progress_cb,
        cancel_requested_callback cancel_cb) {

        const std::string root_c_path = add_path(params.storage, "drives/c/");
        const std::string root_e_path = add_path(params.storage, "drives/e/");
        const std::string root_z_path = add_path(params.storage, "drives/z/");
        const std::string rom_resident_path = add_path(params.storage, "roms/");

        common::create_directories(rom_resident_path);

        device_installation_error error = device_installation_none;

        switch (params.method) {
        case device_install_method_dump_rpkg: {
            LOG_INFO(eka2l1::SYSTEM, "Installing device from ROM dump: rom={}, rpkg={}, storage={}", params.rom_path, params.rpkg_path, params.storage);
            std::string firmware_code;
            error = loader::install_rpkg(dvcmngr, params.rpkg_path, root_z_path, firmware_code, progress_cb, cancel_cb);

            if (error != device_installation_none) {
                return error;
            }

            const std::string rom_directory = add_path(params.storage, add_path("roms", firmware_code + "\\"));
            common::create_directories(rom_directory);
            common::copy_file(params.rom_path, add_path(rom_directory, "SYM.ROM"), true);
            break;
        }

        case device_install_method_dump_rom_only:
            LOG_INFO(eka2l1::SYSTEM, "Installing device from ROM only: rom={}, storage={}", params.rom_path, params.storage);
            error = loader::install_rom(dvcmngr, params.rom_path, rom_resident_path, root_z_path, progress_cb, cancel_cb);
            break;

        case device_install_method_firmware:
            LOG_INFO(eka2l1::SYSTEM, "Installing device from firmware: vpl={}, storage={}", params.vpl_path, params.storage);
            error = install_firmware(dvcmngr, params.vpl_path, root_c_path, root_e_path, root_z_path, rom_resident_path, variant_cb, progress_cb, cancel_cb);
            break;
        }

        if (error != device_installation_none) {
            LOG_ERROR(eka2l1::SYSTEM, "Device installation failed with error code {}", static_cast<int>(error));
            return error;
        }

        dvcmngr->save_devices();
        return device_installation_none;
    }
}
