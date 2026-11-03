<?php
/** Run with wp eval-file in a disposable, activated VPN Server test installation. */

use Pilandrome\VpnServer\Admin;
use Pilandrome\VpnServer\Fast_Route;
use Pilandrome\VpnServer\Settings;

require_once WP_PLUGIN_DIR . '/vpn-server/includes/class-admin.php';
require_once WP_PLUGIN_DIR . '/vpn-server/includes/server-core.php';

if ( ! get_option( 'vpn_review_disposable_fixture' ) || ! str_starts_with( home_url(), 'http://127.0.0.1:' ) ) {
	throw new RuntimeException( 'Run these checks only in an explicitly marked disposable loopback fixture.' );
}

$GLOBALS['vpn_review_passed'] = 0;
function vpn_review_assert( bool $condition, string $message ): void {
	if ( ! $condition ) {
		throw new RuntimeException( $message );
	}
	++$GLOBALS['vpn_review_passed'];
	echo "PASS: $message\n";
}

$original = Settings::get();
$restore = get_option( Settings::OPTION );
try {
	delete_option( Settings::OPTION );
	Settings::activate();
	$fresh = Settings::get();
	vpn_review_assert( 64 === strlen( $fresh['secret'] ) && ctype_xdigit( $fresh['secret'] ), 'Fresh activation generates a 256-bit random server secret.' );
	vpn_review_assert( true === $fresh['status_page'], 'Fresh activation keeps the status page enabled.' );
	vpn_review_assert( 14 === count( $fresh['access_keys'] ), 'Fresh activation creates the fourteen agreed default clients.' );
	foreach ( $fresh['access_keys'] as $address => $key ) {
		vpn_review_assert( Settings::default_access_key( $address ) === $key, "Default access key preserved for $address." );
	}
	Settings::activate();
	vpn_review_assert( $fresh === Settings::get(), 'Reactivation preserves all existing settings and keys.' );
} finally {
	update_option( Settings::OPTION, $restore, true );
}
$probe    = new ReflectionMethod( Admin::class, 'probe_storage' );
$uploads  = wp_upload_dir();
$folder   = $uploads['basedir'] . '/vpn-server-review-probe-' . bin2hex( random_bytes( 8 ) );
$settings = $original;
$settings['storage'] = $folder;
$test_response = null;
$mock = static function ( $pre, $args, $url ) use ( &$test_response ) {
	return $test_response;
};
add_filter( 'pre_http_request', $mock, 10, 3 );
try {
	$test_response = new WP_Error( 'http_request_failed', 'Deliberate test failure' );
	vpn_review_assert( 'warning' === $probe->invoke( null, $settings )[0], 'Unreachable storage probe is inconclusive, never secure.' );
	foreach ( array( 200, 301, 401, 429, 500, 503 ) as $status ) {
		$test_response = array( 'response' => array( 'code' => $status ), 'body' => 'Unrelated response' );
		vpn_review_assert( 'warning' === $probe->invoke( null, $settings )[0], "HTTP $status without the probe is inconclusive." );
	}
	foreach ( array( 403, 404 ) as $status ) {
		$test_response = array( 'response' => array( 'code' => $status ), 'body' => '' );
		vpn_review_assert( 'ok' === $probe->invoke( null, $settings )[0], "HTTP $status reports a denied or missing probe." );
	}
	remove_filter( 'pre_http_request', $mock, 10 );
	$leak = static function ( $pre, $args, $url ) use ( $folder ) {
		return array( 'response' => array( 'code' => 200 ), 'body' => file_get_contents( $folder . '/' . basename( $url ) ) );
	};
	add_filter( 'pre_http_request', $leak, 10, 3 );
	vpn_review_assert( 'error' === $probe->invoke( null, $settings )[0], 'A publicly readable probe is an error.' );
	remove_filter( 'pre_http_request', $leak, 10 );
	$settings['storage'] = dirname( ABSPATH ) . '/outside-uploads';
	vpn_review_assert( 'warning' === $probe->invoke( null, $settings )[0], 'Outside uploads does not prove outside the web root.' );
	vpn_review_assert( array() === glob( $folder . '/vpn-server-probe-*.txt' ), 'Probe files are cleaned up after every result.' );
} finally {
	remove_filter( 'pre_http_request', $mock, 10 );
	require_once ABSPATH . 'wp-admin/includes/file.php';
	WP_Filesystem();
	global $wp_filesystem;
	$wp_filesystem->delete( $folder, true );
}

$fast_path = Fast_Route::path();
vpn_review_assert( ! file_exists( $fast_path ), 'Fixture has no pre-existing fast route.' );
wp_mkdir_p( dirname( $fast_path ) );
$sentinel = "<?php // foreign file used by regression test\n";
file_put_contents( $fast_path, $sentinel );
try {
	vpn_review_assert( is_wp_error( Fast_Route::install() ), 'Fast route refuses to overwrite another owner.' );
	vpn_review_assert( $sentinel === file_get_contents( $fast_path ), 'Foreign fast route file remains byte-identical.' );
	Fast_Route::uninstall();
	vpn_review_assert( file_exists( $fast_path ), 'Uninstall preserves a foreign fast route file.' );
} finally {
	unlink( $fast_path );
}
vpn_review_assert( true === Fast_Route::install(), 'Managed fast route installs.' );
vpn_review_assert( Fast_Route::is_current(), 'Managed fast route matches current plugin.' );
Fast_Route::deactivate();
vpn_review_assert( ! file_exists( $fast_path ), 'Deactivation removes managed fast route.' );
vpn_review_assert( $original === Settings::get(), 'Checks preserve existing settings.' );
echo 'Passed ' . $GLOBALS['vpn_review_passed'] . " assertions.\n";
