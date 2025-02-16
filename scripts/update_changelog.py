import os
import re
from datetime import datetime

def get_version():
    with open('../platformio.ini', 'r') as f:
        content = f.read()
        version_match = re.search(r'version\s*=\s*"([^"]+)"', content)
        return version_match.group(1) if version_match else None

def update_changelog():
    version = get_version()
    today = datetime.now().strftime('%Y-%m-%d')
    
    changelog_template = f"""## [{version}] - {today}
### Added
- 

### Changed
-

### Fixed
-
"""
    
    with open('../CHANGELOG.md', 'r') as f:
        content = f.read()
    
    # Insert new version template after the header
    updated_content = content.replace("# Changelog\n", f"# Changelog\n\n{changelog_template}")
    
    with open('../CHANGELOG.md', 'w') as f:
        f.write(updated_content)

if __name__ == "__main__":
    update_changelog()