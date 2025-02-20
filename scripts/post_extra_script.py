Import("env")
import os

def create_ota_data_initial(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    ota_data_path = os.path.join(build_dir, "ota_data_initial.bin")
    
    # WLED-style OTA data initialization
    # First 32 bytes are for the first OTA slot
    # Next 32 bytes are for the second OTA slot
    # Pattern: 0x00 for the running partition, 0x55 for others
    ota_data = bytearray([0x00] * 32 + [0x55] * 32)
    
    # Fill the rest with 0xFF
    ota_data.extend([0xFF] * (0x2000 - len(ota_data)))
    
    with open(ota_data_path, 'wb') as f:
        f.write(ota_data)
    
    print(f"Created ota_data_initial.bin ({len(ota_data)} bytes)")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", create_ota_data_initial)