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

# Push to origin (local git repo)
echo "Pushing to origin..."
git push origin
if [ $? -ne 0 ]; then
    echo "Error: Failed to push to origin"
    exit 1
fi

git tag "v$VERSION"
if [ $? -ne 0 ]; then
    echo "Error: Failed to create tag v$VERSION"
    exit 1
fi

git push origin "v$VERSION"
if [ $? -ne 0 ]; then
    echo "Error: Failed to push tag v$VERSION to origin"
    git tag -d "v$VERSION"
    exit 1
fi

# Ask for upstream push
read -p "Do you want to push to GitHub (upstream)? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Pushing to upstream (GitHub)..."
    git push upstream
    if [ $? -ne 0 ]; then
        echo "Error: Failed to push to upstream"
        exit 1
    fi
    
    git push upstream "v$VERSION"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to push tag v$VERSION to upstream"
        exit 1
    fi
    echo "Successfully pushed to GitHub"
fi

echo "Successfully created and pushed tag v$VERSION"
echo "All operations completed"