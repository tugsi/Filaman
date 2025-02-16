import os
import re
from datetime import datetime

def get_version():
    # Get the script's directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Get the project root directory (one level up)
    project_dir = os.path.dirname(script_dir)
    
    platformio_path = os.path.join(project_dir, 'platformio.ini')
    
    with open(platformio_path, 'r') as f:
        content = f.read()
        version_match = re.search(r'version\s*=\s*"([^"]+)"', content)
        return version_match.group(1) if version_match else None

def update_changelog():
    version = get_version()
    today = datetime.now().strftime('%Y-%m-%d')
    
    # Get absolute paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    changelog_path = os.path.join(project_dir, 'CHANGELOG.md')
    
    changelog_template = f"""## [{version}] - {today}
### Added
- 

### Changed
-

### Fixed
-
"""
    
    if not os.path.exists(changelog_path):
        # Create new changelog if it doesn't exist
        with open(changelog_path, 'w') as f:
            f.write(f"# Changelog\n\n{changelog_template}")
    else:
        # Update existing changelog
        with open(changelog_path, 'r') as f:
            content = f.read()
        
        if f"[{version}]" not in content:
            # Only add new version if it doesn't exist
            updated_content = content.replace("# Changelog\n", f"# Changelog\n\n{changelog_template}")
            
            with open(changelog_path, 'w') as f:
                f.write(updated_content)

if __name__ == "__main__":
    update_changelog()