// Workshop Computer Website JavaScript

// Seven-segment display rendering (inline SVG)
function renderSevenSegment(numberStr) {
    const digits = String(numberStr).split('');
    return `
        <span class="seven-seg" role="img" aria-label="${numberStr}">
            ${digits.map(d => renderSevenSegDigit(d)).join('')}
        </span>
    `;
}

function renderSevenSegDigit(d) {
    const onColor = '#a18922ff';
    const offColor = '#a18a2234';
    const W = 36, H = 64, t = 8, m = 4;
    const p = 1; // inner padding per segment (inset)
        const hInset = 4; // extra horizontal inset for A, G, D to shorten bars
    const vLen = (H - 3*m - 2*t) / 2;
    const segs = {
        '0': ['A','B','C','D','E','F'],
        '1': ['B','C'],
        '2': ['A','B','G','E','D'],
        '3': ['A','B','C','D','G'],
        '4': ['F','G','B','C'],
        '5': ['A','F','G','C','D'],
        '6': ['A','F','G','E','C','D'],
        '7': ['A','B','C'],
        '8': ['A','B','C','D','E','F','G'],
        '9': ['A','B','C','D','F','G']
    };
    const on = new Set(segs[d] || []);
    const rect = (name, x, y, w, h) => {
        const fill = on.has(name) ? onColor : offColor;
        const x2 = x + p, y2 = y + p;
        const w2 = Math.max(0, w - 2*p), h2 = Math.max(0, h - 2*p);
        return `<rect x="${x2}" y="${y2}" width="${w2}" height="${h2}" rx="${t/2}" ry="${t/2}" fill="${fill}" />`;
    };
    const parts = [];
    // A (top), G (middle), D (bottom)
        // A (top), G (middle), D (bottom) with shortened width
        parts.push(rect('A', m + hInset, m, W - 2*m - 2*hInset, t));
        parts.push(rect('G', m + hInset, H/2 - t/2, W - 2*m - 2*hInset, t));
        parts.push(rect('D', m + hInset, H - m - t, W - 2*m - 2*hInset, t));
    // F/E left, B/C right
    parts.push(rect('F', m, m + t, t, vLen));
    parts.push(rect('E', m, m + t + vLen + t, t, vLen));
    parts.push(rect('B', W - m - t, m + t, t, vLen));
    parts.push(rect('C', W - m - t, m + t + vLen + t, t, vLen));
    return `<svg class="seven-seg-digit" viewBox="0 0 ${W} ${H}" aria-hidden="true">${parts.join('')}</svg>`;
}
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
        
        return `
            <div class="release-card" data-status="${this.normalizeStatus(release.status)}" data-language="${release.language}">
                <div class="release-header">
                    <div class="release-title">${this.escapeHtml(release.title)}</div>
                    <div class="release-number">${renderSevenSegment(String(release.number).padStart(2,'0'))}</div>
                </div>
                <div class="release-content">
                    <div class="release-description">
                        ${this.escapeHtml(release.description) || 'No description available.'}
                    </div>
                    <div class="release-meta">
                        <ul class="meta-list">
                            <li><span class="meta-key">Creator</span><span class="meta-value">${this.escapeHtml(release.creator) || 'Unknown'}</span></li>
                            <li><span class="meta-key">Language</span><span class="meta-value">${this.escapeHtml(release.language) || 'Not specified'}</span></li>
                            <li><span class="meta-key">Version</span><span class="meta-value">${this.escapeHtml(release.version) || 'N/A'}</span></li>
                            <li><span class="meta-key">Status</span><span class="meta-value"><span class="status ${statusClass}">${this.escapeHtml(release.status) || 'Unknown'}</span></span></li>
                        </ul>
                    </div>
                    <div class="release-actions">
                        <a href="release.html?id=${release.id}" class="btn btn-primary">
                            📄 View Details
                        </a>
                        ${release.has_firmware ? `
                            <button onclick="site.showDownloadOptions('${release.id}')" class="btn btn-secondary">
                                💾 Download
                            </button>
                        ` : ''}
                    </div>
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
        
    // Target the main page container, not the header's inner container
    const container = document.querySelector('main.container');
        if (!container) return;

        const statusClass = this.getStatusClass(release.status);
        
        container.innerHTML = `
            <div class="back-button">
                <a href="index.html" class="btn btn-secondary">← Back to Releases</a>
            </div>
            
            <div class="release-detail">
                <div class="release-detail-header">
                    <h1>${renderSevenSegment(String(release.number).padStart(2,'0'))} <span class="detail-title">${this.escapeHtml(release.title)}</span></h1>
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
                                    \ud83d\udcbe Download ${file.split('/').pop()}
                                </a>
                            `).join(' ')}
                        </div>
                    ` : ''}

                    ${release.readme_html ? `
                        <div class="readme-section">
                            <h3>README</h3>
                            <div class="readme-html">${release.readme_html}</div>
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
                </div>
            </div>

            <div class="back-button back-bottom">
                <a href="index.html" class="btn btn-secondary">← Back to Releases</a>
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
    // Target the main page container for error rendering
    const container = document.querySelector('main.container');
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
