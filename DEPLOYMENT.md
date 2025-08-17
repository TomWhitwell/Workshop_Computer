# Workshop Computer Website - Deployment Guide

This guide explains how to deploy the Workshop Computer website to various hosting platforms.

## 🚀 Quick Deploy Options

### GitHub Pages (Recommended)

1. **Enable GitHub Pages**:
   - Go to your repository settings
   - Scroll to "Pages" section
   - Select "Deploy from a branch"
   - Choose `main` branch and `/ (root)` folder
   - Save

2. **Access your site**:
   - Your site will be available at: `https://[username].github.io/Workshop_Computer/`
   - The redirect page will automatically forward visitors to `website/`

### Netlify

1. **Connect Repository**:
   - Sign up at [netlify.com](https://netlify.com)
   - Click "New site from Git"
   - Connect your GitHub repository

2. **Configure Build**:
   - Build command: `cd website && python build.py`
   - Publish directory: `/` (root)
   - Advanced: Add environment variable `PYTHON_VERSION` = `3.11`

3. **Deploy**:
   - Netlify will automatically build and deploy
   - Your site will be available at a generated `.netlify.app` URL

### Vercel

1. **Import Project**:
   - Sign up at [vercel.com](https://vercel.com)
   - Click "New Project"
   - Import from GitHub

2. **Configure**:
   - Framework Preset: "Other"
   - Root Directory: `/`
   - Build Command: `cd website && python3 build.py`
   - Output Directory: `/`

3. **Deploy**:
   - Vercel will automatically deploy
   - Your site will be available at a generated `.vercel.app` URL

## 🔧 Manual Deployment

For any static web hosting service:

1. **Build the site**:
   ```bash
   cd website
   python build.py
   ```

2. **Upload files**:
   - Upload ALL files from the repository root to your web server
   - Ensure directory structure is preserved
   - Make sure `releases/` directory is accessible

3. **Configure web server**:
   - Set `index.html` as the default document
   - Configure MIME types for `.uf2` files (optional)
   - Enable HTTPS (recommended)

## 📁 Required File Structure

Your deployed site should have this structure:

```
website-root/
├── index.html              # Redirect page
├── website/                # Main website
│   ├── index.html         # Website homepage
│   ├── release.html       # Release detail page
│   ├── style.css          # Styling
│   ├── script.js          # JavaScript
│   ├── releases.json      # Release data
│   └── README.md          # Website docs
└── releases/              # Release files
    ├── 00_Simple_MIDI/
    │   ├── Documentation/
    │   │   └── *.pdf
    │   └── uf2 Installer/
    │       └── *.uf2
    └── [other releases]/
```

## ⚙️ Configuration Options

### Custom Domain

For custom domains (like `computer.yourdomain.com`):

1. **GitHub Pages**: Add a `CNAME` file to repository root
2. **Netlify**: Configure in site settings → Domain management
3. **Vercel**: Configure in project settings → Domains

### Analytics

To add analytics, edit `website/index.html` and `website/release.html` to include your tracking code.

### SEO Optimization

The website includes basic SEO:
- Semantic HTML structure
- Meta descriptions
- Open Graph tags (can be added to `<head>`)

## 🔍 Testing Your Deployment

1. **Check main page**: Visit your site URL
2. **Test filtering**: Use the filter dropdowns on the main page
3. **Test release pages**: Click "View Details" on any release with documentation
4. **Test downloads**: Click "Download" buttons to ensure files are accessible
5. **Mobile testing**: Check responsiveness on mobile devices

## 🐛 Troubleshooting

### Common Issues

**"No releases found"**:
- Check that `releases.json` was generated and uploaded
- Verify the file contains valid JSON

**PDFs not loading**:
- Ensure PDF files are uploaded to the correct paths
- Check browser console for 404 errors
- Verify relative paths in `releases.json`

**Downloads not working**:
- Check that UF2 files are accessible
- Verify web server serves `.uf2` files correctly
- Check CORS settings if serving from different domains

**Styling issues**:
- Ensure `style.css` is uploaded and accessible
- Check browser console for CSS loading errors

### File Permissions

If using traditional web hosting, ensure files have correct permissions:
```bash
find . -type f -name "*.html" -exec chmod 644 {} \;
find . -type f -name "*.css" -exec chmod 644 {} \;
find . -type f -name "*.js" -exec chmod 644 {} \;
find . -type f -name "*.json" -exec chmod 644 {} \;
find . -type f -name "*.pdf" -exec chmod 644 {} \;
find . -type f -name "*.uf2" -exec chmod 644 {} \;
```

## 🔄 Updating the Site

When new releases are added:

1. **Update repository**: Commit new releases to the `releases/` directory
2. **Regenerate data**: Run `python website/generate_data.py`
3. **Deploy**: Commit and push changes (auto-deploy) or manually upload files

For GitHub Pages/Netlify/Vercel, the site will update automatically when you push to the main branch.

## 📞 Support

If you encounter issues:

1. Check the browser console for JavaScript errors
2. Verify all files are uploaded correctly
3. Test with a simple local server: `python -m http.server 8000`
4. Check the Workshop Computer repository issues on GitHub

---

Happy deploying! 🎉
