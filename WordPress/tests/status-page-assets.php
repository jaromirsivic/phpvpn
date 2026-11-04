<?php
/**
 * Render the source status page with real WordPress URL helpers, without saving settings.
 * Run: wp --skip-plugins --skip-themes eval-file WordPress/tests/status-page-assets.php
 */

use Pilandrome\VpnServer\Status_Page;

$plugin = dirname( __DIR__, 2 ) . '/server/wordpress/version7/vpn-server/vpn-server.php';
require_once dirname( $plugin ) . '/includes/class-settings.php';
require_once dirname( $plugin ) . '/includes/class-status-page.php';

$cases = array(
	// The browser may reach localhost's installation through a LAN address and another port.
	array( 'LAN alias', 'http://localhost:5080', 'http://localhost:5080/wp-content/plugins/vpn-server/', '/wp-content/plugins/vpn-server/' ),
	array( 'Subdirectory and custom content path', 'https://example.test/blog', 'https://example.test/blog/wordpress/custom/plugins/vpn-server/', '/blog/wordpress/custom/plugins/vpn-server/' ),
	array( 'HTTPS', 'https://example.test', 'https://example.test/wp-content/plugins/vpn-server/', '/wp-content/plugins/vpn-server/' ),
	array( 'IPv6', 'http://[::1]:5080', 'http://[::1]:5080/wp-content/plugins/vpn-server/', '/wp-content/plugins/vpn-server/' ),
	array( 'Case insensitive hostname', 'https://EXAMPLE.test', 'https://example.test/content/vpn-server/', '/content/vpn-server/' ),
	array( 'External CDN', 'https://example.test', 'https://cdn.example.test/custom/vpn-server/', 'https://cdn.example.test/custom/vpn-server/' ),
	array( 'Separate WordPress core host', 'https://example.test', 'https://wordpress.example.test/content/vpn-server/', 'https://wordpress.example.test/content/vpn-server/' ),
	array( 'Separate asset port', 'http://localhost:5080', 'http://localhost:5081/content/vpn-server/', 'http://localhost:5081/content/vpn-server/' ),
	array( 'Separate asset scheme', 'http://example.test', 'https://example.test/content/vpn-server/', 'https://example.test/content/vpn-server/' ),
	array( 'Protocol relative CDN', 'https://example.test', '//cdn.example.test/content/vpn-server/', '//cdn.example.test/content/vpn-server/' ),
	array( 'Already relative', 'https://example.test', '/custom/vpn-server/', '/custom/vpn-server/' ),
);
$render = new ReflectionMethod( Status_Page::class, 'output' );
$version = (string) filemtime( dirname( $plugin ) . '/assets/status-page.js' );
$passed = 0;
foreach ( $cases as list( $name, $home, $base, $expected_base ) ) {
	foreach ( array( '', '?cache=a%2Fb&lang=en#asset' ) as $suffix ) {
		$home_filter = static fn() => $home;
		$asset_filter = static fn( $url, $path ) => $base . $path . $suffix;
		add_filter( 'home_url', $home_filter );
		add_filter( 'plugins_url', $asset_filter, 10, 2 );
		ob_start();
		try {
			$render->invoke( null, false, array(), $plugin );
			$html = ob_get_contents();
		} finally {
			ob_end_clean();
			remove_filter( 'home_url', $home_filter );
			remove_filter( 'plugins_url', $asset_filter );
		}
		// Inspect the actual rendered src attributes, including escaping and script versioning.
		preg_match( '/<img\b[^>]*\bsrc="([^"]+)"/', $html, $image );
		preg_match( '/<script\b[^>]*\bsrc="([^"]+)"/', $html, $script );
		$actual_image = html_entity_decode( $image[1] ?? '', ENT_QUOTES, 'UTF-8' );
		$actual_script = html_entity_decode( $script[1] ?? '', ENT_QUOTES, 'UTF-8' );
		$expected_image = $expected_base . 'assets/tutorial.png' . $suffix;
		$expected_script = add_query_arg( 'ver', $version, $expected_base . 'assets/status-page.js' . $suffix );
		if ( $expected_image !== $actual_image || $expected_script !== $actual_script ) {
			throw new RuntimeException( "$name: unexpected asset URLs\nImage: $actual_image\nScript: $actual_script" );
		}
		++$passed;
		echo "PASS: $name" . ( $suffix ? ' with query and fragment' : '' ) . "\n";
	}
}
echo "Passed $passed rendered-page asset cases.\n";
