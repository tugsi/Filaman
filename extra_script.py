Import("env")

# Hook in die Build-Prozesse
env.AddPreAction("uploadfs", env.VerboseAction("$PROJECT_DIR/scripts/buildfs.sh", "Building Filesystem Image..."))

env.AddPreAction("upload", env.VerboseAction("$PROJECT_DIR/scripts/uploadfs.sh", "Uploading Filesystem Image..."))