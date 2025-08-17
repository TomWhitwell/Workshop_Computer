// Workshop Computer Website JavaScript
class WorkshopComputerSite {
    constructor() {
        this.releases = [];
        this.filteredReleases = [];
        this.init();
    }

    async init() {
        await this.loadReleases();
        this.setupFilters();
        this.renderReleases();
    }

    async loadReleases() {
        try {
            const response = await fetch('releases.json');
            this.releases = await response.json();
            this.filteredReleases = [...this.releases];
        } catch (error) {
            console.error('Error loading releases:', error);
            this.showError('Failed to load release data. Please try again later.');
        }
    }

    setupFilters() {
        const statusFilter = document.getElementById('status-filter');
        const languageFilter = document.getElementById('language-filter');

        if (statusFilter) {
            statusFilter.addEventListener('change', () => this.applyFilters());
        }

        if (languageFilter) {
            languageFilter.addEventListener('change', () => this.applyFilters());
            this.populateLanguageFilter(languageFilter);
        }
    }

    populateLanguageFilter(languageFilter) {
        const languages = [...new Set(this.releases.map(r => r.language).filter(l => l))].sort();
        
        languages.forEach(language => {
            const option = document.createElement('option');
            option.value = language;
            option.textContent = language;
            languageFilter.appendChild(option);
        });
    }

    applyFilters() {
        const statusFilter = document.getElementById('status-filter')?.value || '';
        const languageFilter = document.getElementById('language-filter')?.value || '';

        this.filteredReleases = this.releases.filter(release => {
            const statusMatch = !statusFilter || this.normalizeStatus(release.status).includes(statusFilter.toLowerCase());
            const languageMatch = !languageFilter || release.language === languageFilter;
            
            return statusMatch && languageMatch;
        });

        this.renderReleases();
    }

    normalizeStatus(status) {
        return status.toLowerCase();
    }

    getStatusClass(status) {
        const normalized = this.normalizeStatus(status);
        
        if (normalized.includes('released') || normalized.includes('ready')) return 'released';
        if (normalized.includes('working')) return 'working';
        if (normalized.includes('wip') || normalized.includes('progress')) return 'wip';
        if (normalized.includes('beta')) return 'beta';
        if (normalized.includes('proof')) return 'proof-of-concept';
        
        return 'wip'; // default
    }

    renderReleases() {
        const grid = document.getElementById('releases-grid');
        if (!grid) return;

        if (this.filteredReleases.length === 0) {
            grid.innerHTML = '<div class="no-results">No releases match the current filters.</div>';
            return;
        }

        grid.innerHTML = this.filteredReleases.map(release => this.createReleaseCard(release)).join('');
    }

    createReleaseCard(release) {
        const statusClass = this.getStatusClass(release.status);
        const hasDownloads = release.has_documentation || release.has_firmware;
        
        return `
            <div class="release-card" data-status="${this.normalizeStatus(release.status)}" data-language="${release.language}">
                <div class="release-header">
                    <div class="release-number">#${release.number}</div>
                    <div class="release-title">${this.escapeHtml(release.title)}</div>
                </div>
                <div class="release-content">
                    <div class="release-description">
                        ${this.escapeHtml(release.description) || 'No description available.'}
                    </div>
                    <div class="release-meta">
                        <div class="meta-item">
                            <span class="meta-label">Creator:</span>
                            <span>${this.escapeHtml(release.creator) || 'Unknown'}</span>
                        </div>
                        <div class="meta-item">
                            <span class="meta-label">Language:</span>
                            <span>${this.escapeHtml(release.language) || 'Not specified'}</span>
                        </div>
                        <div class="meta-item">
                            <span class="meta-label">Version:</span>
                            <span>${this.escapeHtml(release.version) || 'N/A'}</span>
                        </div>
                        <div class="meta-item">
                            <span class="meta-label">Status:</span>
                            <span class="status ${statusClass}">${this.escapeHtml(release.status) || 'Unknown'}</span>
                        </div>
                    </div>
                    ${hasDownloads ? `
                        <div class="release-actions">
                            ${release.has_documentation ? `
                                <a href="release.html?id=${release.id}" class="btn btn-primary">
                                    📄 View Details
                                </a>
                            ` : ''}
                            ${release.has_firmware ? `
                                <button onclick="site.showDownloadOptions('${release.id}')" class="btn btn-secondary">
                                    💾 Download
                                </button>
                            ` : ''}
                        </div>
                    ` : `
                        <div class="release-actions">
                            <button class="btn btn-secondary" disabled>
                                No downloads available
                            </button>
                        </div>
                    `}
                </div>
            </div>
        `;
    }

    showDownloadOptions(releaseId) {
        const release = this.releases.find(r => r.id === releaseId);
        if (!release || !release.uf2_files.length) {
            alert('No firmware files available for this release.');
            return;
        }

        if (release.uf2_files.length === 1) {
            // Direct download for single file
            this.downloadFile(release.uf2_files[0]);
        } else {
            // Show options for multiple files
            const options = release.uf2_files.map((file, index) => 
                `${index + 1}. ${file.split('/').pop()}`
            ).join('\\n');
            
            const choice = prompt(`Multiple firmware files available:\\n${options}\\n\\nEnter the number of the file you want to download (1-${release.uf2_files.length}):`);
            
            if (choice) {
                const index = parseInt(choice) - 1;
                if (index >= 0 && index < release.uf2_files.length) {
                    this.downloadFile(release.uf2_files[index]);
                } else {
                    alert('Invalid selection.');
                }
            }
        }
    }

    downloadFile(filePath) {
        // Create a temporary link and trigger download
        const link = document.createElement('a');
        link.href = '../' + filePath; // Go up one level from website directory
        link.download = filePath.split('/').pop();
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
    }

    showError(message) {
        const grid = document.getElementById('releases-grid');
        if (grid) {
            grid.innerHTML = `<div class="error">${message}</div>`;
        }
    }

    escapeHtml(text) {
        if (!text) return '';
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}

// Release detail page functionality
class ReleaseDetailPage {
    constructor() {
        this.init();
    }

    async init() {
        const urlParams = new URLSearchParams(window.location.search);
        const releaseId = urlParams.get('id');
        
        if (!releaseId) {
            this.showError('No release ID specified.');
            return;
        }

        await this.loadReleaseDetail(releaseId);
    }

    async loadReleaseDetail(releaseId) {
        try {
            const response = await fetch('releases.json');
            const releases = await response.json();
            const release = releases.find(r => r.id === releaseId);
            
            if (!release) {
                this.showError('Release not found.');
                return;
            }

            this.renderReleaseDetail(release);
        } catch (error) {
            console.error('Error loading release detail:', error);
            this.showError('Failed to load release details.');
        }
    }

    renderReleaseDetail(release) {
        document.title = `${release.title} - Workshop Computer`;
        
        const container = document.querySelector('.container');
        if (!container) return;

        const statusClass = this.getStatusClass(release.status);
        
        container.innerHTML = `
            <div class="back-button">
                <a href="index.html" class="btn btn-secondary">← Back to Releases</a>
            </div>
            
            <div class="release-detail">
                <div class="release-detail-header">
                    <h1>#${release.number}: ${this.escapeHtml(release.title)}</h1>
                    <p class="subtitle">${this.escapeHtml(release.description)}</p>
                    <div class="release-meta">
                        <p><strong>Creator:</strong> ${this.escapeHtml(release.creator)}</p>
                        <p><strong>Language:</strong> ${this.escapeHtml(release.language)}</p>
                        <p><strong>Version:</strong> ${this.escapeHtml(release.version)}</p>
                        <p><strong>Status:</strong> <span class="status ${statusClass}">${this.escapeHtml(release.status)}</span></p>
                    </div>
                </div>
                
                <div class="release-detail-content">
                    ${release.has_firmware ? `
                        <div class="download-section">
                            <h3>Downloads</h3>
                            ${release.uf2_files.map(file => `
                                <a href="../${file}" download class="btn btn-primary">
                                    💾 Download ${file.split('/').pop()}
                                </a>
                            `).join(' ')}
                        </div>
                    ` : ''}
                    
                    ${release.has_documentation ? `
                        <div class="documentation-section">
                            <h3>Documentation</h3>
                            ${release.pdf_files.map(file => `
                                <div class="pdf-container">
                                    <h4>${file.split('/').pop()}</h4>
                                    <iframe src="../${file}" class="pdf-viewer" type="application/pdf">
                                        <p>Your browser doesn't support PDF viewing. 
                                        <a href="../${file}" target="_blank">Click here to download the PDF</a></p>
                                    </iframe>
                                </div>
                            `).join('')}
                        </div>
                    ` : '<p>No documentation available for this release.</p>'}
                    
                    ${release.readme ? `
                        <div class="readme-section">
                            <h3>README</h3>
                            <pre>${this.escapeHtml(release.readme)}</pre>
                        </div>
                    ` : ''}
                </div>
            </div>
        `;
    }

    getStatusClass(status) {
        const normalized = status.toLowerCase();
        
        if (normalized.includes('released') || normalized.includes('ready')) return 'released';
        if (normalized.includes('working')) return 'working';
        if (normalized.includes('wip') || normalized.includes('progress')) return 'wip';
        if (normalized.includes('beta')) return 'beta';
        if (normalized.includes('proof')) return 'proof-of-concept';
        
        return 'wip';
    }

    showError(message) {
        const container = document.querySelector('.container');
        if (container) {
            container.innerHTML = `
                <div class="back-button">
                    <a href="index.html" class="btn btn-secondary">← Back to Releases</a>
                </div>
                <div class="error">${message}</div>
            `;
        }
    }

    escapeHtml(text) {
        if (!text) return '';
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }
}

// Initialize the appropriate class based on the page
let site;
document.addEventListener('DOMContentLoaded', () => {
    if (window.location.pathname.includes('release.html')) {
        site = new ReleaseDetailPage();
    } else {
        site = new WorkshopComputerSite();
    }
});
