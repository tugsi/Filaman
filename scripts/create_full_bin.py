import os
import shutil
from platformio import util

Import("env")

def create_full_bin(source, target, env):
    # Get paths
    firmware_path = str(target[0])
    build_dir = os.path.dirname(firmware_path)
    project_dir = env.get("PROJECT_DIR")
    
    # Create full binary
    firmware_name = os.path.basename(firmware_path)
    spiffs_bin = os.path.join(build_dir, "spiffs.bin")
    full_bin = os.path.join(build_dir, "full.bin")
    
    print("Creating full.bin...")
    
    # Check if files exist
    if not os.path.exists(firmware_path):
        print("Error: Firmware binary not found!")
        return
    if not os.path.exists(spiffs_bin):
        print("Error: SPIFFS binary not found!")
        return
    
    # Calculate padding size
    firmware_size = os.path.getsize(firmware_path)
    padding_size = 0x3D0000 - firmware_size  # SPIFFS start address - firmware size
    
    # Create full binary
    with open(full_bin, 'wb') as outfile:
        # Copy firmware
        with open(firmware_path, 'rb') as f:
            outfile.write(f.read())
        
        # Add padding
        outfile.write(b'\xFF' * padding_size)
        
        # Copy SPIFFS
        with open(spiffs_bin, 'rb') as f:
            outfile.write(f.read())
    
    print(f"Created full.bin ({os.path.getsize(full_bin)} bytes)")

# Register the callback
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", create_full_bin)