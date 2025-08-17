# GitHub Pages Setup Guide

This guide will help you set up automatic deployment of the Workshop Computer website to GitHub Pages using GitHub Actions.

## 🚀 Quick Setup (5 minutes)

### Step 1: Enable GitHub Pages

1. Go to your repository on GitHub
2. Click **Settings** (in the repository, not your account)
3. Scroll down to **Pages** in the left sidebar
4. Under **Source**, select **GitHub Actions** (not "Deploy from a branch")
5. Click **Save**

### Step 2: Commit the Workflow Files

The repository already includes the necessary GitHub Actions workflows:
- `.github/workflows/deploy.yml` - Builds and deploys the website
- `.github/workflows/health-check.yml` - Weekly health checks
- `.nojekyll` - Tells GitHub Pages to skip Jekyll processing

Simply commit and push these files to trigger the deployment:

```bash
git add .github/ .nojekyll
git commit -m "Add GitHub Actions workflow for automatic website deployment"
git push
```

### Step 3: Wait for Deployment

1. Go to the **Actions** tab in your repository
2. You should see a workflow running called "Build and Deploy Workshop Computer Website"
3. Wait for it to complete (usually 2-3 minutes)
4. Your site will be available at: `https://[username].github.io/Workshop_Computer/`

## 🔧 How It Works

### Automatic Triggers

The website will automatically rebuild and redeploy when:
- **New releases are added** to the `releases/` directory
- **Documentation is updated** (PDF files added/changed)
- **Firmware files are updated** (UF2 files added/changed)
- **Any changes are pushed** to the main branch

### Build Process

When triggered, GitHub Actions will:

1. **Check out the repository** with all files
2. **Set up Python** and install dependencies (PyYAML)
3. **Generate release data** by scanning the `releases/` directory
4. **Build the website** and validate all files
5. **Deploy to GitHub Pages** with the complete file structure

### File Structure on GitHub Pages

Your deployed site will have this structure:
```
https://[username].github.io/Workshop_Computer/
├── index.html                    # Redirect to website/
├── website/                      # Main website
│   ├── index.html               # Release listing page
│   ├── release.html             # Individual release pages
│   ├── style.css                # Styling
│   ├── script.js                # JavaScript functionality
│   └── releases.json            # Auto-generated release data
└── releases/                     # Release files for download
    ├── 00_Simple_MIDI/
    │   ├── Documentation/
    │   │   └── Simple_MIDI_0-1.pdf
    │   └── uf2 Installer/
    │       └── Simple_Midi_0_5_0.ino.uf2
    └── [other releases]/
```

## 🎯 Customization Options

### Custom Domain

To use a custom domain (like `computer.yourdomain.com`):

1. Add a `CNAME` file to the repository root containing your domain
2. Configure DNS to point to `[username].github.io`
3. Enable **Enforce HTTPS** in GitHub Pages settings

### Build Customization

To modify the build process, edit `.github/workflows/deploy.yml`:

- **Change Python version**: Modify the `python-version` setting
- **Add build steps**: Add additional steps before the "Upload artifact" step
- **Change triggers**: Modify the `on:` section to change when builds occur

### Analytics

To add Google Analytics or other tracking:

1. Edit `website/index.html` and `website/release.html`
2. Add your tracking code to the `<head>` section
3. Commit and push - the site will automatically rebuild

## 🔍 Monitoring & Maintenance

### Build Status

Monitor your deployments:
- **Actions tab**: See current and past build statuses
- **Environments**: View deployment history under repository settings
- **Pages settings**: See current deployment status and URL

### Weekly Health Checks

The included health check workflow runs every Monday and:
- Validates all release data
- Checks for missing info.yaml files
- Generates a summary report
- Alerts you to any issues

### Manual Deployment

If you need to manually trigger a rebuild:
1. Go to **Actions** tab
2. Click **Build and Deploy Workshop Computer Website**
3. Click **Run workflow**
4. Select the branch and click **Run workflow**

## 🐛 Troubleshooting

### Common Issues

**Build failing?**
- Check the Actions tab for error details
- Ensure all required files are committed
- Verify `releases/` directory structure is correct

**Site not updating?**
- GitHub Pages can take 5-10 minutes to update after deployment
- Try a hard refresh (Ctrl+F5 or Cmd+Shift+R)
- Check that the workflow completed successfully

**Missing files?**
- Ensure PDF and UF2 files are committed to git
- Check that file paths in the repository match the generated JSON
- Verify files aren't being ignored by `.gitignore`

**PDF viewing not working?**
- Some browsers block PDF embedding from different domains
- Users can still download PDFs directly
- Consider hosting large PDFs elsewhere and linking to them

### Getting Help

If you encounter issues:
1. Check the [Actions logs](../../actions) for detailed error messages
2. Look at recent [Issues](../../issues) in the repository
3. Create a new issue with details about the problem

## 🎉 You're All Set!

Once set up, your Workshop Computer website will:
- ✅ **Automatically update** when you add new releases
- ✅ **Stay synchronized** with your repository
- ✅ **Provide professional presentation** of all program cards
- ✅ **Enable easy downloads** of documentation and firmware
- ✅ **Work on all devices** with responsive design

Your site will be live at: `https://[username].github.io/Workshop_Computer/`

Happy releasing! 🖥️🎵
