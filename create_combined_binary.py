Import("env")
from os.path import join, exists
import shutil
import os

def combine_binaries(source, target, env):
    # Define paths for binary files
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    combined_bin = join(build_dir, "combined.bin")
    firmware_bin = join(build_dir, "firmware.bin")
    spiffs_bin = join(build_dir, "spiffs.bin")
    
    # Define target firmware path
    firmware_dir = join(project_dir, "firmware")
    target_firmware = join(firmware_dir, "filaman.bin")

    # Build firmware if it doesn't exist
    if not exists(firmware_bin):
        print("Building firmware...")
        env.Execute("pio run -t buildprog")

    # Build SPIFFS if it doesn't exist
    if not exists(spiffs_bin):
        print("Building SPIFFS image...")
        env.Execute("pio run -t buildfs")

    # Check if files exist after build attempts
    if not exists(firmware_bin):
        raise Exception("Firmware binary not found at: " + firmware_bin)
    
    if not exists(spiffs_bin):
        raise Exception("SPIFFS binary not found at: " + spiffs_bin)

    print("Found firmware at:", firmware_bin)
    print("Found SPIFFS at:", spiffs_bin)

    # Command to merge firmware and SPIFFS
    merge_command = (
        "esptool.py --chip esp32 merge_bin -o {combined_bin} "
        "--flash_mode dio --flash_freq 40m --flash_size 4MB "
        "0x10000 {firmware_bin} 0x310000 {spiffs_bin}"
    ).format(
        combined_bin=combined_bin,
        firmware_bin=firmware_bin,
        spiffs_bin=spiffs_bin
    )
    
    print("Executing merge command:", merge_command)
    env.Execute(merge_command)

    # Create firmware directory if it doesn't exist
    if not exists(firmware_dir):
        os.makedirs(firmware_dir)

    # Move combined binary to target location
    print(f"Moving combined binary to {target_firmware}")
    shutil.copy2(combined_bin, target_firmware)
    print(f"Binary successfully moved to {target_firmware}")

# Register the custom target with explicit dependencies
env.AddCustomTarget(
    name="combine_binaries",
    dependencies=["buildfs", "buildprog"],
    actions=[combine_binaries],
    title="Combine Firmware & SPIFFS",
    description="Combines firmware.bin and spiffs.bin into a single binary"
)