#!/usr/bin/env python3
"""
Script to generate release data for the Workshop Computer website.
Scans the releases directory and extracts information about each program card.
"""

import os
import json
import yaml
import glob
from pathlib import Path

def find_files(release_dir, patterns):
    """Find files matching any of the given patterns in the release directory."""
    found_files = []
    for pattern in patterns:
        found_files.extend(glob.glob(os.path.join(release_dir, "**", pattern), recursive=True))
    return found_files

def extract_release_info(release_path):
    """Extract information from a single release directory."""
    release_name = os.path.basename(release_path)
    
    # Parse the release number and name
    if "_" in release_name:
        parts = release_name.split("_", 1)
        release_number = parts[0]
        release_title = parts[1].replace("_", " ").title()
    else:
        release_number = release_name
        release_title = release_name
    
    # Load info.yaml if it exists
    info_file = os.path.join(release_path, "info.yaml")
    info = {}
    if os.path.exists(info_file):
        try:
            with open(info_file, 'r', encoding='utf-8') as f:
                info = yaml.safe_load(f) or {}
        except Exception as e:
            print(f"Warning: Could not parse {info_file}: {e}")
    
    # Find documentation PDFs
    pdf_patterns = ["*.pdf", "**/*.pdf"]
    pdf_files = find_files(release_path, pdf_patterns)
    
    # Find UF2 files
    uf2_patterns = ["*.uf2", "**/*.uf2"]
    uf2_files = find_files(release_path, uf2_patterns)
    
    # Read README if it exists
    readme_file = os.path.join(release_path, "README.md")
    readme_content = ""
    if os.path.exists(readme_file):
        try:
            with open(readme_file, 'r', encoding='utf-8') as f:
                readme_content = f.read()
        except Exception as e:
            print(f"Warning: Could not read {readme_file}: {e}")
    
    # Convert file paths to relative paths for web use
    def make_relative_path(file_path):
        return os.path.relpath(file_path, start=os.path.dirname(os.path.dirname(release_path))).replace("\\", "/")
    
    return {
        "id": release_name,
        "number": release_number,
        "title": release_title,
        "description": info.get("Description", ""),
        "language": info.get("Language", ""),
        "creator": info.get("Creator", ""),
        "version": str(info.get("Version", "")),
        "status": info.get("Status", ""),
        "pdf_files": [make_relative_path(pdf) for pdf in pdf_files],
        "uf2_files": [make_relative_path(uf2) for uf2 in uf2_files],
        "readme": readme_content,
        "has_documentation": len(pdf_files) > 0,
        "has_firmware": len(uf2_files) > 0
    }

def main():
    # Get the script directory and find the releases directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    releases_dir = os.path.join(repo_root, "releases")
    
    if not os.path.exists(releases_dir):
        print(f"Error: Releases directory not found at {releases_dir}")
        return
    
    print(f"Scanning releases in: {releases_dir}")
    
    releases = []
    
    # Scan each subdirectory in releases
    for item in os.listdir(releases_dir):
        item_path = os.path.join(releases_dir, item)
        
        # Skip files and hidden directories
        if not os.path.isdir(item_path) or item.startswith('.'):
            continue
            
        print(f"Processing: {item}")
        
        try:
            release_info = extract_release_info(item_path)
            releases.append(release_info)
        except Exception as e:
            print(f"Error processing {item}: {e}")
    
    # Sort releases by number
    def sort_key(release):
        try:
            return int(release["number"])
        except ValueError:
            return 999  # Put non-numeric releases at the end
    
    releases.sort(key=sort_key)
    
    # Write the data to a JSON file for the website
    output_file = os.path.join(script_dir, "releases.json")
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(releases, f, indent=2, ensure_ascii=False)
    
    print(f"\nGenerated data for {len(releases)} releases")
    print(f"Output written to: {output_file}")
    
    # Print summary
    print(f"\nSummary:")
    for release in releases:
        docs = "DOCS" if release["has_documentation"] else "----"
        firmware = "UF2 " if release["has_firmware"] else "----"
        print(f"  {release['number']:>2}: {release['title']:<30} {docs} {firmware} - {release['status']}")

if __name__ == "__main__":
    main()
