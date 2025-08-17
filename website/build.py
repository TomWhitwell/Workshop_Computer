#!/usr/bin/env python3
"""
Build script for the Workshop Computer website.
Generates the releases data and prepares the site for deployment.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

def main():
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    
    # Check if we're in GitHub Actions
    is_github_actions = os.environ.get('GITHUB_ACTIONS') == 'true'
    
    if is_github_actions:
        print("Building Workshop Computer Website for GitHub Actions")
        print("=" * 60)
    else:
        print("🖥️  Building Workshop Computer Website")
        print("=" * 50)
    
    # Step 1: Generate releases data
    print("📊 Generating releases data...")
    try:
        # Set environment to handle Unicode output properly
        env = os.environ.copy()
        env['PYTHONIOENCODING'] = 'utf-8'
        
        result = subprocess.run([sys.executable, "generate_data.py"], 
                              cwd=script_dir, capture_output=True, text=True, env=env)
        if result.returncode != 0:
            print(f"❌ Error generating data: {result.stderr}")
            return False
        print("✅ Releases data generated successfully")
    except Exception as e:
        print(f"❌ Error running data generation: {e}")
        return False
    
    # Step 2: Check required files
    required_files = [
        "index.html",
        "release.html", 
        "style.css",
        "script.js",
        "releases.json"
    ]
    
    print("🔍 Checking required files...")
    missing_files = []
    for file in required_files:
        file_path = script_dir / file
        if not file_path.exists():
            missing_files.append(file)
        else:
            print(f"✅ {file}")
    
    if missing_files:
        print(f"❌ Missing files: {', '.join(missing_files)}")
        return False
    
    # Step 3: Validate releases.json
    print("🔧 Validating releases data...")
    try:
        import json
        with open(script_dir / "releases.json", 'r') as f:
            releases = json.load(f)
        print(f"✅ Found {len(releases)} releases in data file")
    except Exception as e:
        print(f"❌ Error validating releases.json: {e}")
        return False
    
    # Step 4: Report on available files
    print("📋 Release Summary:")
    releases_with_docs = sum(1 for r in releases if r.get('has_documentation'))
    releases_with_firmware = sum(1 for r in releases if r.get('has_firmware'))
    
    print(f"   📄 Releases with documentation: {releases_with_docs}")
    print(f"   💾 Releases with firmware: {releases_with_firmware}")
    print(f"   📦 Total releases: {len(releases)}")
    
    print()
    print("🎉 Build completed successfully!")
    print()
    
    if is_github_actions:
        print("📁 Website will be deployed to GitHub Pages")
        print(f"   Repository: {os.environ.get('GITHUB_REPOSITORY', 'Unknown')}")
        print(f"   Branch: {os.environ.get('GITHUB_REF_NAME', 'Unknown')}")
        print(f"   Commit: {os.environ.get('GITHUB_SHA', 'Unknown')[:8]}")
    else:
        print("📁 Website files are ready in the 'website' directory:")
        print(f"   {script_dir}")
        print()
        print("🚀 To serve the website locally, run:")
        print(f"   cd \"{script_dir}\"")
        print("   python -m http.server 8000")
        print("   # Then open http://localhost:8000 in your browser")
        print()
        print("🌐 To deploy, upload all files in the 'website' directory")
        print("   to your web server, ensuring the 'releases' directory")
        print("   from the repository root is also accessible.")
    
    return True

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
