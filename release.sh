#!/bin/bash

# Get version from platformio.ini
VERSION=$(grep '^version = ' platformio.ini | sed 's/version = "\(.*\)"/\1/')

if [ -z "$VERSION" ]; then
    echo "Error: Could not find version in platformio.ini"
    exit 1
fi

echo "Creating release for version $VERSION"

# Check if working directory is clean
if [ -n "$(git status --porcelain)" ]; then
    echo "Error: Working directory is not clean. Please commit all changes first."
    exit 1
fi

# Create and push tag
git tag "v$VERSION"
if [ $? -ne 0 ]; then
    echo "Error: Failed to create tag v$VERSION"
    exit 1
fi

git push origin "v$VERSION"
if [ $? -ne 0 ]; then
    echo "Error: Failed to push tag v$VERSION"
    git tag -d "v$VERSION"
    exit 1
fi

echo "Successfully created and pushed tag v$VERSION"
echo "GitHub Actions workflow will now create the release"