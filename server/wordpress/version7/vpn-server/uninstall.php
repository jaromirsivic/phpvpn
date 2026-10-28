<?php
/**
 * Removes the settings, the fast route and the storage folders the plugin created.
 *
 * A storage folder the administrator entered is left in place, it may hold other files.
 * On multisite the plugin is activated per site, so every site is cleaned.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'WP_UNINSTALL_PLUGIN' ) ) {
	exit;
}

const PLUGIN_FILE = __DIR__ . '/vpn-server.php';

require_once __DIR__ . '/includes/class-settings.php';
require_once __DIR__ . '/includes/class-fast-route.php';
require_once ABSPATH . 'wp-admin/includes/file.php';

/**
 * Removes the data of the current site.
 */
function uninstall_site(): void {
	$settings = Settings::get();
	delete_option( Settings::OPTION );
	delete_transient( 'vpn_server_self_test' );
	if ( ! Settings::is_managed_storage( $settings ) ) {
		return;
	}
	global $wp_filesystem;
	if ( 'direct' === get_filesystem_method() && WP_Filesystem() ) {
		$wp_filesystem->delete( $settings['storage'], true );
	}
}

Fast_Route::uninstall();

if ( is_multisite() ) {
	foreach ( get_sites( array( 'fields' => 'ids', 'number' => 0 ) ) as $vpn_server_site ) {
		switch_to_blog( (int) $vpn_server_site );
		uninstall_site();
		restore_current_blog();
	}
} else {
	uninstall_site();
}
