PHPVPN - installation and startup

- Package layout: client/ contains VPN clients, server/ contains alternative
  relay servers, and documentation/ contains this guide and, when supplied,
  the English protocol specification. Choose one server implementation.
  Paths below are relative to the extracted distribution.

- Windows client (x64): extract the complete client/Windows/x64 folder to a
  writable location outside Program Files\phpvpn. Run install.bat and accept
  UAC, or use install-phpvpn.exe. The installer verifies the signed package,
  installs under %ProgramFiles%\phpvpn and starts the service and tray.
  Open Configuration Wizard from the tray, enter the server URL and matching
  secrets/communication contracts, then Connect. Configuration is stored in
  %ProgramData%\phpvpn\config.json. Keep the extracted folder for uninstall.bat
  or uninstall-phpvpn.exe. This build uses a development signing certificate,
  not a publicly trusted publisher certificate.

- Linux client (x64): copy the complete client/Linux/x64 folder to Fedora 44+
  or Ubuntu 24.04+, open a terminal there and run sh install.sh --verify-only.
  Run sudo sh install.sh for a desktop, or sudo sh install.sh --headless for
  a server. Use --with-tray to request the desktop tray explicitly.
  The installer verifies the signed package, installs RPM/DEB dependencies
  from the OS repositories and starts phpvpn.service. Use the tray's
  Configuration Wizard, or phpvpn set-config ./config.json followed by
  phpvpn connect. Check phpvpn status. Configuration is /etc/phpvpn/config.json.
  See client/Linux/x64/README.md for tray startup, GNOME and uninstall details.

- macOS client: a version is being prepared. No installable client is included.

- PHP server: copy server/PHP/phpvpn.php to a PHP 8.2+ web server's document
  root or chosen web directory. Edit its configuration header: replace
  SERVER_SECRET and ACCESS_KEYS, and set a writable BUFFER_DIRECTORY outside
  the document root. If it is inside, deny all direct web access to it.
  No Composer install or separate assets are needed. Open the deployed URL,
  for example https://vpn.example.org/phpvpn.php, and use that URL in clients.

- Node.js server: install Node.js 24 LTS. Copy server/Node.js to a writable
  deployment directory outside the web document root. No npm install or build
  is needed. Create a private configuration JSON as described in server-node.md,
  replacing SERVER_SECRET and ACCESS_KEYS and setting BUFFER_DIRECTORY.
  Run node phpvpn.js --check-config --config /path/to/server.json, then
  node phpvpn.js --config /path/to/server.json from the deployment directory.
  Default URL: http://127.0.0.1:8080/phpvpn.php. Stop with Ctrl+C.

- Python server: copy the complete server/Python folder to a writable deployment
  directory outside the web document root. Edit the configuration at the top
  of phpvpn.py: replace SERVER_SECRET and ACCESS_KEYS and set BUFFER_DIRECTORY.
  On Windows run init.bat, then start.bat. On Linux/macOS run sh init.sh, then
  sh start.sh. Init requires Internet access and prepares uv, Python 3.13 and
  the locked dependencies in a local .venv. Default URL:
  http://127.0.0.1:8000/phpvpn.php. Stop with stop.bat or sh stop.sh.
  Run one application process/worker. See server-python.md for configuration.

- Node.js and Python listen on loopback by default. Configure the listen
  address or a reverse proxy for remote clients. For HTTPS, use a reverse
  proxy that preserves request/response bytes and does not cache responses.
  The supplied launchers do not install an OS service or boot-time autostart.
  Keep private configuration and queue data out of publicly served folders.
  Default access keys are public examples and must be replaced for private use.
  Server URL and credentials must match the client configuration. Payload keys
  belong to clients only. Do not share queue directories between implementations.
