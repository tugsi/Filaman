Import("env")

# Wiederverwendung der replace_version Funktion
exec(open("./scripts/pre_build.py").read())

# Bind to SPIFFS build
env.AddPreAction("buildfs", replace_version)