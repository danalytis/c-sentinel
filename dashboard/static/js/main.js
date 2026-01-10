/**
 * C-Sentinel Dashboard JavaScript
 * Version: 0.6.0
 * 
 * Core functionality for the C-Sentinel web interface.
 */

/* ═══════════════════════════════════════════════════════════════════════════
   THEME MANAGER
   ═══════════════════════════════════════════════════════════════════════════ */

const ThemeManager = {
    STORAGE_KEY: 'c-sentinel-theme',
    
    /**
     * Initialize theme from storage or default to dark
     */
    init() {
        // Apply stored theme immediately (before DOMContentLoaded)
        const stored = localStorage.getItem(this.STORAGE_KEY);
        if (stored === 'light') {
            document.documentElement.setAttribute('data-theme', 'light');
        }
        // Dark is default in HTML, no action needed
        
        // Bind toggle buttons after DOM loads
        document.addEventListener('DOMContentLoaded', () => {
            this.bindToggles();
            this.watchSystemPreference();
        });
    },
    
    /**
     * Bind click handlers to all theme toggle buttons
     */
    bindToggles() {
        document.querySelectorAll('.theme-toggle').forEach(toggle => {
            toggle.addEventListener('click', () => this.toggle());
            toggle.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                    this.toggle();
                }
            });
        });
    },
    
    /**
     * Toggle between light and dark themes
     */
    toggle() {
        const current = this.get();
        const next = current === 'dark' ? 'light' : 'dark';
        document.documentElement.setAttribute('data-theme', next);
        localStorage.setItem(this.STORAGE_KEY, next);
    },
    
    /**
     * Get current theme
     * @returns {string} 'dark' or 'light'
     */
    get() {
        return document.documentElement.getAttribute('data-theme') || 'dark';
    },
    
    /**
     * Watch for system preference changes
     */
    watchSystemPreference() {
        window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
            // Only apply if user hasn't set a preference
            if (!localStorage.getItem(this.STORAGE_KEY)) {
                document.documentElement.setAttribute('data-theme', e.matches ? 'dark' : 'light');
            }
        });
    }
};

// Initialize immediately (before DOM ready)
ThemeManager.init();


/* ═══════════════════════════════════════════════════════════════════════════
   MOBILE MENU
   ═══════════════════════════════════════════════════════════════════════════ */

const MobileMenu = {
    overlay: null,
    menu: null,
    
    init() {
        this.overlay = document.getElementById('mobile-menu-overlay');
        this.menu = document.getElementById('mobile-menu');
        
        if (!this.overlay || !this.menu) return;
        
        // Close on overlay click
        this.overlay.addEventListener('click', () => this.close());
        
        // Close on escape key
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape' && this.isOpen()) {
                this.close();
            }
        });
    },
    
    open() {
        if (!this.overlay || !this.menu) return;
        this.overlay.classList.add('is-open');
        this.menu.classList.add('is-open');
        document.body.style.overflow = 'hidden';
    },
    
    close() {
        if (!this.overlay || !this.menu) return;
        this.overlay.classList.remove('is-open');
        this.menu.classList.remove('is-open');
        document.body.style.overflow = '';
    },
    
    isOpen() {
        return this.menu?.classList.contains('is-open') || false;
    }
};


/* ═══════════════════════════════════════════════════════════════════════════
   TOAST NOTIFICATIONS
   ═══════════════════════════════════════════════════════════════════════════ */

const Toast = {
    container: null,
    
    /**
     * Initialize toast container
     */
    init() {
        this.container = document.getElementById('toast-container');
        if (!this.container) {
            this.container = document.createElement('div');
            this.container.id = 'toast-container';
            this.container.className = 'toast-container';
            document.body.appendChild(this.container);
        }
    },
    
    /**
     * Show a toast notification
     * @param {string} message - Message to display
     * @param {string} type - 'success', 'warning', 'danger', or 'info'
     * @param {number} duration - Duration in ms (0 = persistent)
     * @returns {HTMLElement} The toast element
     */
    show(message, type = 'info', duration = 4000) {
        if (!this.container) this.init();
        
        const toast = document.createElement('div');
        toast.className = `toast toast--${type}`;
        
        const icon = this.getIcon(type);
        toast.innerHTML = `
            <svg><use href="#icon-${icon}"></use></svg>
            <span class="toast__message">${this.escapeHtml(message)}</span>
            <button class="toast__close" aria-label="Dismiss">
                <svg><use href="#icon-x"></use></svg>
            </button>
        `;
        
        // Bind close button
        toast.querySelector('.toast__close').addEventListener('click', () => {
            this.dismiss(toast);
        });
        
        this.container.appendChild(toast);
        
        // Re-initialize icons if using Lucide
        if (window.lucide) {
            lucide.createIcons();
        }
        
        // Auto-dismiss after duration
        if (duration > 0) {
            setTimeout(() => this.dismiss(toast), duration);
        }
        
        return toast;
    },
    
    /**
     * Dismiss a toast
     * @param {HTMLElement} toast - The toast element to dismiss
     */
    dismiss(toast) {
        if (!toast || !toast.parentElement) return;
        toast.classList.add('toast--exiting');
        setTimeout(() => toast.remove(), 300);
    },
    
    /**
     * Get icon name for toast type
     * @param {string} type 
     * @returns {string} Icon name
     */
    getIcon(type) {
        const icons = {
            success: 'check-circle',
            warning: 'alert-triangle',
            danger: 'alert-circle',
            info: 'info'
        };
        return icons[type] || 'info';
    },
    
    /**
     * Escape HTML entities
     * @param {string} text 
     * @returns {string} Escaped text
     */
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    },
    
    // Convenience methods
    success(message, duration) { return this.show(message, 'success', duration); },
    warning(message, duration) { return this.show(message, 'warning', duration); },
    danger(message, duration)  { return this.show(message, 'danger', duration); },
    error(message, duration)   { return this.show(message, 'danger', duration); },
    info(message, duration)    { return this.show(message, 'info', duration); }
};


/* ═══════════════════════════════════════════════════════════════════════════
   MODAL / CONFIRM DIALOG
   ═══════════════════════════════════════════════════════════════════════════ */

const Modal = {
    /**
     * Show a confirmation dialog
     * @param {string} message - Confirmation message
     * @param {Function} onConfirm - Callback when confirmed
     * @param {Function} onCancel - Callback when cancelled
     */
    confirm(message, onConfirm, onCancel) {
        const backdrop = document.createElement('div');
        backdrop.className = 'modal-backdrop is-open';
        backdrop.innerHTML = `
            <div class="modal">
                <div class="modal__body">
                    <p>${Toast.escapeHtml(message)}</p>
                </div>
                <div class="modal__footer">
                    <button class="btn btn--secondary" data-action="cancel">Cancel</button>
                    <button class="btn btn--danger" data-action="confirm">Confirm</button>
                </div>
            </div>
        `;
        
        const close = (confirmed) => {
            backdrop.classList.remove('is-open');
            setTimeout(() => backdrop.remove(), 300);
            if (confirmed && onConfirm) onConfirm();
            if (!confirmed && onCancel) onCancel();
        };
        
        backdrop.querySelector('[data-action="cancel"]').addEventListener('click', () => close(false));
        backdrop.querySelector('[data-action="confirm"]').addEventListener('click', () => close(true));
        backdrop.addEventListener('click', (e) => {
            if (e.target === backdrop) close(false);
        });
        
        document.body.appendChild(backdrop);
        
        // Focus confirm button
        backdrop.querySelector('[data-action="confirm"]').focus();
    },
    
    /**
     * Show a custom modal
     * @param {string} title - Modal title
     * @param {string|HTMLElement} content - Modal content
     * @param {Object} options - Modal options
     */
    show(title, content, options = {}) {
        const backdrop = document.createElement('div');
        backdrop.className = 'modal-backdrop is-open';
        backdrop.id = options.id || 'modal-' + Date.now();
        
        const contentHtml = typeof content === 'string' ? content : '';
        
        backdrop.innerHTML = `
            <div class="modal">
                <div class="modal__header">
                    <h3 class="modal__title">${Toast.escapeHtml(title)}</h3>
                    <button class="modal__close" aria-label="Close">
                        <i data-lucide="x"></i>
                    </button>
                </div>
                <div class="modal__body">${contentHtml}</div>
                ${options.footer ? `<div class="modal__footer">${options.footer}</div>` : ''}
            </div>
        `;
        
        // If content is an element, append it
        if (content instanceof HTMLElement) {
            backdrop.querySelector('.modal__body').appendChild(content);
        }
        
        const close = () => {
            backdrop.classList.remove('is-open');
            setTimeout(() => backdrop.remove(), 300);
            if (options.onClose) options.onClose();
        };
        
        backdrop.querySelector('.modal__close').addEventListener('click', close);
        backdrop.addEventListener('click', (e) => {
            if (e.target === backdrop) close();
        });
        
        document.body.appendChild(backdrop);
        
        // Initialize icons
        if (window.lucide) {
            lucide.createIcons();
        }
        
        return {
            element: backdrop,
            close: close
        };
    }
};


/* ═══════════════════════════════════════════════════════════════════════════
   UTILITY FUNCTIONS
   ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Format a date as relative time
 * @param {string|Date} dateInput - Date to format
 * @returns {string} Relative time string
 */
function timeAgo(dateInput) {
    const date = new Date(dateInput);
    const now = new Date();
    const seconds = Math.floor((now - date) / 1000);
    
    if (seconds < 60) return 'just now';
    if (seconds < 3600) return Math.floor(seconds / 60) + 'm ago';
    if (seconds < 86400) return Math.floor(seconds / 3600) + 'h ago';
    return Math.floor(seconds / 86400) + 'd ago';
}

/**
 * Format a number with thousand separators
 * @param {number} num 
 * @returns {string}
 */
function formatNumber(num) {
    return new Intl.NumberFormat().format(num);
}

/**
 * Format bytes to human-readable size
 * @param {number} bytes 
 * @returns {string}
 */
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

/**
 * Debounce a function
 * @param {Function} func - Function to debounce
 * @param {number} wait - Wait time in ms
 * @returns {Function} Debounced function
 */
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

/**
 * Make a fetch request with error handling
 * @param {string} url - URL to fetch
 * @param {Object} options - Fetch options
 * @returns {Promise<any>} Response data
 */
async function api(url, options = {}) {
    try {
        const response = await fetch(url, {
            headers: {
                'Content-Type': 'application/json',
                ...options.headers
            },
            ...options
        });
        
        if (response.status === 401) {
            window.location.href = '/login';
            return null;
        }
        
        if (!response.ok) {
            const error = await response.json().catch(() => ({ error: 'Request failed' }));
            throw new Error(error.error || `HTTP ${response.status}`);
        }
        
        return response.json();
    } catch (error) {
        console.error('API Error:', error);
        throw error;
    }
}

/**
 * Update the "last updated" timestamp display
 */
function updateLastUpdated() {
    const el = document.getElementById('last-updated');
    if (el) {
        el.textContent = new Date().toLocaleTimeString();
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
   INITIALIZATION
   ═══════════════════════════════════════════════════════════════════════════ */

document.addEventListener('DOMContentLoaded', () => {
    // Initialize components
    MobileMenu.init();
    Toast.init();
    
    // Initialize Lucide icons if available
    if (window.lucide) {
        lucide.createIcons();
    }
    
    // Update last updated timestamp
    updateLastUpdated();
});


/* ═══════════════════════════════════════════════════════════════════════════
   EXPOSE GLOBAL FUNCTIONS
   ═══════════════════════════════════════════════════════════════════════════ */

// These are exposed globally for use in inline event handlers
window.openMobileMenu = () => MobileMenu.open();
window.closeMobileMenu = () => MobileMenu.close();
window.Toast = Toast;
window.Modal = Modal;
window.showConfirm = Modal.confirm;
window.timeAgo = timeAgo;
window.formatNumber = formatNumber;
window.formatBytes = formatBytes;
window.api = api;
window.updateLastUpdated = updateLastUpdated;
