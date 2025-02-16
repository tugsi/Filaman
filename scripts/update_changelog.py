import os
import re
import subprocess
from datetime import datetime

def get_version():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    platformio_path = os.path.join(project_dir, 'platformio.ini')
    
    with open(platformio_path, 'r') as f:
        content = f.read()
        version_match = re.search(r'version\s*=\s*"([^"]+)"', content)
        return version_match.group(1) if version_match else None

def get_last_tag():
    try:
        result = subprocess.run(['git', 'describe', '--tags', '--abbrev=0'], 
                              capture_output=True, text=True)
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return None

def categorize_commit(commit_msg):
    """Categorize commit messages based on conventional commits"""
    lower_msg = commit_msg.lower()
    if any(x in lower_msg for x in ['feat', 'add', 'new']):
        return 'Added'
    elif any(x in lower_msg for x in ['fix', 'bug']):
        return 'Fixed'
    else:
        return 'Changed'

def get_changes_from_git():
    """Get changes from git commits since last tag"""
    changes = {
        'Added': [],
        'Changed': [],
        'Fixed': []
    }
    
    last_tag = get_last_tag()
    
    # Get commits since last tag
    git_log_command = ['git', 'log', '--pretty=format:%s']
    if last_tag:
        git_log_command.append(f'{last_tag}..HEAD')
    
    try:
        result = subprocess.run(git_log_command, capture_output=True, text=True)
        commits = result.stdout.strip().split('\n')
        
        # Filter out empty commits and categorize
        for commit in commits:
            if commit:
                category = categorize_commit(commit)
                # Clean up commit message
                clean_msg = re.sub(r'^(feat|fix|chore|docs|style|refactor|perf|test)(\(.*\))?:', '', commit).strip()
                changes[category].append(clean_msg)
                
    except subprocess.CalledProcessError:
        print("Error: Failed to get git commits")
        return None
    
    return changes

def push_changes(version):
    """Push changes to upstream"""
    try:
        # Stage the CHANGELOG.md
        subprocess.run(['git', 'add', 'CHANGELOG.md'], check=True)
        
        # Commit the changelog
        commit_msg = f"docs: update changelog for version {version}"
        subprocess.run(['git', 'commit', '-m', commit_msg], check=True)
        
        # Push to origin (local)
        subprocess.run(['git', 'push', 'origin'], check=True)
        print("Successfully pushed to origin")
        
        # Ask for upstream push
        response = input("Do you want to push to GitHub (upstream)? (y/n): ").lower()
        if response == 'y':
            subprocess.run(['git', 'push', 'upstream'], check=True)
            print("Successfully pushed to upstream")
            
    except subprocess.CalledProcessError as e:
        print(f"Error during git operations: {e}")
        return False
    return True

def update_changelog():
    version = get_version()
    today = datetime.now().strftime('%Y-%m-%d')
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    changelog_path = os.path.join(project_dir, 'CHANGELOG.md')
    
    # Get changes from git
    changes = get_changes_from_git()
    if not changes:
        print("No changes found or error occurred")
        return
    
    # Create changelog entry
    changelog_entry = f"## [{version}] - {today}\n"
    for section, entries in changes.items():
        if entries:  # Only add sections that have entries
            changelog_entry += f"### {section}\n"
            for entry in entries:
                changelog_entry += f"- {entry}\n"
            changelog_entry += "\n"
    
    if not os.path.exists(changelog_path):
        with open(changelog_path, 'w') as f:
            f.write(f"# Changelog\n\n{changelog_entry}")
        push_changes(version)
    else:
        with open(changelog_path, 'r') as f:
            content = f.read()
        
        if f"[{version}]" not in content:
            updated_content = content.replace("# Changelog\n", f"# Changelog\n\n{changelog_entry}")
            with open(changelog_path, 'w') as f:
                f.write(updated_content)
            push_changes(version)
        else:
            print(f"Version {version} already exists in changelog")

if __name__ == "__main__":
    update_changelog()