'use strict';
const confirmErase = document.getElementById('confirm-erase');
const installButton = document.getElementById('install');
confirmErase.addEventListener('change', () => { installButton.disabled = !confirmErase.checked; });
// Avoid carrying erase consent through browser back/forward navigation.
window.addEventListener('pageshow', () => { confirmErase.checked = false; installButton.disabled = true; });
