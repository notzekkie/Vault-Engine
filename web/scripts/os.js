window.onload = function() {
    const osSpecificDownloads = document.getElementById("os-specific-downloads");

    const userOS = navigator.platform.toLowerCase();

    let downloadLinks = '';

    if (userOS.includes('win')) {
        // Windows OS detected, show the Windows installer link
        downloadLinks = `
            <h3>Windows Installer</h3>
            <a href="https://github.com/notzekkie/Vault-Engine-Installer/raw/main/dist/Vault%20Engine%20Installer.exe" class="download-link" target="_blank">Download Vault Engine for Windows</a>
        `;
    } else if (userOS.includes('linux')) {
        // Linux OS detected, show a message or Linux installer (you can replace this link later)
        downloadLinks = `
            <h3>Linux Installer</h3>
            <p>Will compile the Linux version later...</p>
        `;
    } else {
        // If the OS is not Windows or Linux, show a message about unsupported OS
        downloadLinks = `
            <p>Your operating system isn't supported by Vault Engine. :(</p>
        `;
    }

    osSpecificDownloads.innerHTML = downloadLinks;
};
