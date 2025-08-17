# Workshop Computer Website

A static website showcasing all the program cards and releases for the Music Thing Workshop Computer.

## Features

- 📋 **Table of Contents**: Browse all available program cards with filtering options
- 🔍 **Filtering**: Filter releases by status (Released, Working, WIP, etc.) and programming language
- 📄 **Documentation Viewer**: View PDF documentation directly in the browser
- 💾 **Direct Downloads**: Download .uf2 firmware files directly
- 📱 **Responsive Design**: Works on desktop, tablet, and mobile devices

## Quick Start

### Option 1: Local Development Server

1. Generate the releases data:
   ```bash
   cd website
   python generate_data.py
   ```

2. Start a local web server:
   ```bash
   python -m http.server 8000
   ```

3. Open your browser to: http://localhost:8000

### Option 2: Build and Deploy

1. Run the build script:
   ```bash
   cd website
   python build.py
   ```

2. Upload all files in the `website` directory to your web server

3. Ensure the `releases` directory from the repository root is accessible from your web server (for file downloads)

## File Structure

```
website/
├── index.html          # Main page with table of contents
├── release.html        # Individual release detail page
├── style.css          # All styling
├── script.js          # JavaScript functionality
├── releases.json      # Generated data file (auto-created)
├── generate_data.py   # Script to extract release information
├── build.py          # Build and validation script
└── README.md         # This file
```

## How It Works

### Data Generation

The `generate_data.py` script scans the `releases/` directory and extracts information from:

- `info.yaml` files (metadata like description, creator, version, status)
- PDF documentation files
- UF2 firmware files  
- README.md files

This data is compiled into `releases.json` which powers the website.

### File Paths

The website expects this structure:
```
repository-root/
├── releases/           # Release directories with docs and firmware
│   ├── 00_Simple_MIDI/
│   ├── 03_Turing_Machine/
│   └── ...
└── website/           # Website files
    ├── index.html
    ├── releases.json
    └── ...
```

When deployed, the website should be able to access `../releases/` to serve downloadable files.

## Customization

### Adding New Releases

1. Add your release directory to `releases/`
2. Include an `info.yaml` file with metadata
3. Add documentation PDFs and UF2 firmware files
4. Run `python generate_data.py` to update the website data

### Styling

Edit `style.css` to customize the appearance. The design uses:
- CSS Grid for responsive layouts
- CSS Custom Properties for easy theming
- Modern CSS features for enhanced UX

### Functionality

Edit `script.js` to modify filtering, add new features, or change behavior.

## Browser Compatibility

- ✅ Modern browsers (Chrome, Firefox, Safari, Edge)
- ✅ Mobile browsers (iOS Safari, Chrome Mobile)
- ⚠️ PDF viewing requires browser PDF support (most modern browsers)
- ⚠️ File downloads require modern JavaScript support

## Dependencies

- Python 3.6+ (for build scripts)
- PyYAML library (`pip install pyyaml`)
- Modern web browser

## Deployment Notes

### Static Hosting (Recommended)

This is a static website that can be deployed to:
- GitHub Pages
- Netlify  
- Vercel
- Any static web hosting service

### File Downloads

For PDF viewing and UF2 downloads to work, your web server needs to:
1. Serve the `releases/` directory contents
2. Have proper MIME types configured for `.pdf` and `.uf2` files
3. Allow CORS for cross-origin requests (if serving from different domains)

### HTTPS

For security and modern browser compatibility, deploy over HTTPS.

## Contributing

To contribute to the website:

1. Fork the repository
2. Make your changes to the website files
3. Test locally using the build script
4. Submit a pull request

## License

This website code is part of the Workshop Computer project. Check the main repository for license information.

---

Made with ❤️ for the Music Thing Workshop Computer community
