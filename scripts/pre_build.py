Import("env")
import os

def replace_version(source, target, env):
    # Get version from common section
    version = env.GetProjectConfig().get("common", "version").strip('"')
    header_file = "./html/header.html"
    
    with open(header_file, 'r') as file:
        content = file.read()
    
    # Replace version in header.html using string manipulation instead of regex
    search = '<h1>FilaMan<span class="version">v'
    end = '</span>'
    start_pos = content.find(search)
    if start_pos != -1:
        start_pos += len(search)
        end_pos = content.find(end, start_pos)
        if end_pos != -1:
            content = content[:start_pos] + version + content[end_pos:]
    
    with open(header_file, 'w') as file:
        file.write(content)

env.AddPreAction("buildfs", replace_version)
