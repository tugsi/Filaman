Import("env")
import os

def combine_html_files(source, target, env):
    html_dir = "./html"
    header_file = os.path.join(html_dir, "header.html")
    
    # Read header content
    with open(header_file, 'r') as f:
        header_content = f.read()
    
    # Process all HTML files except header.html
    for filename in os.listdir(html_dir):
        if filename.endswith('.html') and filename != 'header.html':
            file_path = os.path.join(html_dir, filename)
            
            # Read content
            with open(file_path, 'r') as f:
                content = f.read()
            
            # Replace placeholder with header content
            if '{{header}}' in content:
                new_content = content.replace('{{header}}', header_content)
                
                # Write back combined content
                with open(file_path, 'w') as f:
                    f.write(new_content)
                print(f"Combined header with {filename}")

# Register the script to run before building SPIFFS
env.AddPreAction("buildfs", combine_html_files)