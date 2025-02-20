Import("env")

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