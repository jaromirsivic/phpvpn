<?php
/**
 * Plugin Name:       VPN Server
 * Plugin URI:        https://www.pilandrome.com/projects/phpvpn/index.html
 * Description:       Connect your own devices in a private IPv4 network using a self-hosted PHPVPN relay and the PHPVPN desktop clients.
 * Version:           1.0.4
 * Requires at least: 6.6
 * Requires PHP:      8.2
 * Author:            Jaromír Šivic
 * Author URI:        https://www.pilandrome.com
 * License:           MIT
 * License URI:       https://opensource.org/license/mit
 * Text Domain:       vpn-server
 * Domain Path:       /languages
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

const VERSION = '1.0.4';
const PLUGIN_FILE = __FILE__;

require_once __DIR__ . '/includes/class-settings.php';
require_once __DIR__ . '/includes/class-server.php';
require_once __DIR__ . '/includes/class-fast-route.php';

// Client requests are answered before the theme, init and the main query run.
add_action( 'plugins_loaded', array( Server::class, 'maybe_serve' ), PHP_INT_MIN );

register_activation_hook( __FILE__, array( Settings::class, 'activate' ) );
register_deactivation_hook( __FILE__, array( Fast_Route::class, 'deactivate' ) );

if ( is_admin() ) {
	require_once __DIR__ . '/includes/class-admin.php';
	Admin::register();
}
