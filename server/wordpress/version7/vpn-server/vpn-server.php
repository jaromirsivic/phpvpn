<?php
/**
 * Plugin Name:       VPN Server
 * Plugin URI:        https://www.pilandrome.com
 * Description:       Runs a VPN server on this site, enabling individuals and small businesses to quickly set up a lightweight virtual private network. This is an implementation of PHPVPN (Packet Handoff Protocol VPN) for WordPress. To connect, please install the PHPVPN client for Windows, macOS, or Linux.
 * Version:           1.0.1
 * Requires at least: 6.6
 * Requires PHP:      8.2
 * Author:            pilandrome.com
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

const VERSION = '1.0.1';
const PLUGIN_FILE = __FILE__;

require_once __DIR__ . '/includes/class-settings.php';
require_once __DIR__ . '/includes/class-relay.php';
require_once __DIR__ . '/includes/class-fast-route.php';

// Relay requests are answered before the theme, init and the main query run.
add_action( 'plugins_loaded', array( Relay::class, 'maybe_serve' ), PHP_INT_MIN );

register_activation_hook( __FILE__, array( Settings::class, 'activate' ) );
register_deactivation_hook( __FILE__, array( Fast_Route::class, 'deactivate' ) );

if ( is_admin() ) {
	require_once __DIR__ . '/includes/class-admin.php';
	Admin::register();
}
