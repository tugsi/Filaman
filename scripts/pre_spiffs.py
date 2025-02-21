Import("env")

board_config = env.BoardConfig()

# Calculate SPIFFS size based on partition table
SPIFFS_START = 0x310000  # From partitions.csv
SPIFFS_SIZE = 0xE0000   # From partitions.csv
SPIFFS_PAGE = 256
SPIFFS_BLOCK = 4096

env.Replace(
    MKSPIFFSTOOL="mkspiffs",
    SPIFFSBLOCKSZ=SPIFFS_BLOCK,
    SPIFFSBLOCKSIZE=SPIFFS_BLOCK,
    SPIFFSSTART=SPIFFS_START,
    SPIFFSEND=SPIFFS_START + SPIFFS_SIZE,
    SPIFFSPAGESZ=SPIFFS_PAGE,
    SPIFFSSIZE=SPIFFS_SIZE
)

# Wiederverwendung der replace_version Funktion
exec(open("./scripts/pre_build.py").read())

# Bind to SPIFFS build
env.AddPreAction("buildfs", replace_version)

import os
import shutil
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Format SPIFFS partition before uploading new files
spiffs_dir = os.path.join(env.subst("$BUILD_DIR"), "spiffs")
if os.path.exists(spiffs_dir):
    shutil.rmtree(spiffs_dir)
os.makedirs(spiffs_dir)

print("SPIFFS partition formatted.")