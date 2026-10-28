<?php
/**
 * Status page of the relay endpoint, protocol.md chapter 5.1.
 *
 * The page is a standalone HTML document: relay requests end before the theme loads.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Renders the status page with the key generator and the Show HTTP Headers dialog.
 *
 * The texts are plain English like the protocol defined heading: the page is rendered before
 * WordPress may load translations (after_setup_theme).
 */
final class Status_Page {

	/**
	 * Header names left out of the dialog: names the client manages itself, which its headers
	 * dialog rejects, and credentials and proxy details that a public page must not echo back.
	 */
	const OMITTED_HEADERS = '/^(host|connection|content-(type|length)|transfer-encoding|cookie|authorization|proxy-authorization|forwarded|x-forwarded-.*|x-real-ip|x-client-ip|true-client-ip|cf-.*)$/i';

	/**
	 * Writes the page through the protocol response writer, which adds the protocol headers.
	 *
	 * @param \PhpVpn\ResponseWriter $writer      Response writer of the request.
	 * @param bool                   $insecure    True when the server secret is predictable.
	 * @param string                 $plugin_file Main plugin file, used for the script URL.
	 */
	public static function render( \PhpVpn\ResponseWriter $writer, bool $insecure, string $plugin_file ): void {
		ob_start();
		self::output( $insecure, self::header_lines(), $plugin_file );
		$html = (string) ob_get_clean();
		$writer->begin( 200, \PhpVpn\Http::HTML, array( 'Content-Length' => (string) strlen( $html ) ) );
		$writer->write( $html );
	}

	/**
	 * Request headers as received, one "Name: value" line each, without OMITTED_HEADERS.
	 *
	 * @return string[]
	 */
	private static function header_lines(): array {
		$lines = array();
		foreach ( self::received_headers() as $name => $value ) {
			if ( ! preg_match( self::OMITTED_HEADERS, $name ) ) {
				$lines[] = $name . ': ' . $value;
			}
		}
		return $lines;
	}

	/**
	 * Request headers. getallheaders() keeps the original names, the fallback rebuilds them.
	 *
	 * @return array<string, string>
	 */
	private static function received_headers(): array {
		$headers = array();
		if ( function_exists( 'getallheaders' ) ) {
			$all = getallheaders();
			if ( is_array( $all ) ) {
				foreach ( $all as $name => $value ) {
					$headers[ (string) $name ] = (string) $value;
				}
				return $headers;
			}
		}
		// The values are only shown back to the requester, escaped on output in output().
		// phpcs:ignore WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- headers are displayed verbatim, escaped late.
		foreach ( wp_unslash( $_SERVER ) as $name => $value ) {
			if ( is_string( $name ) && str_starts_with( $name, 'HTTP_' ) && is_string( $value ) ) {
				$headers[ str_replace( ' ', '-', ucwords( strtolower( strtr( substr( $name, 5 ), '_', ' ' ) ) ) ) ] = $value;
			}
		}
		return $headers;
	}

	/**
	 * Prints the document.
	 *
	 * @param bool     $insecure    True when the server secret is predictable.
	 * @param string[] $lines       Request header lines.
	 * @param string   $plugin_file Main plugin file.
	 */
	private static function output( bool $insecure, array $lines, string $plugin_file ): void {
		$script  = plugins_url( 'assets/status-page.js', $plugin_file );
		$version = (string) filemtime( dirname( $plugin_file ) . '/assets/status-page.js' );
		?>
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width">
<meta name="robots" content="noindex">
<title><?php echo esc_html( 'PHPVPN server is running' ); ?></title>
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;font:16px Arial,sans-serif}main{width:48rem;max-width:100%;padding:1.5rem;background:#e8e8e8;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#c00;color:#fff;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}#vpn-server-row:not([hidden]){display:flex;gap:.5rem}#vpn-server-key{min-width:0;flex:1;font-family:monospace}dialog{width:44rem;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#vpn-server-close{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#vpn-server-headers{width:100%;height:14rem;font-family:monospace;white-space:pre}#vpn-server-buttons{text-align:right}
</style>
</head>
<body>
<main>
<h1>PHPVPN server is running</h1>
		<?php if ( $insecure ) : ?>
<aside><b>Warning:</b> <?php echo esc_html( 'The server secret is predictable. Generate a new one in Settings, VPN Server immediately.' ); ?></aside>
		<?php endif; ?>
<p><?php echo esc_html( 'Generate a secure hexadecimal key:' ); ?> <button id="vpn-server-generate"><?php echo esc_html( 'Generate key' ); ?></button></p>
<div id="vpn-server-row" hidden><input id="vpn-server-key" aria-label="<?php echo esc_attr( 'Generated key' ); ?>" readonly><button id="vpn-server-copy-key"><?php echo esc_html( 'Copy key' ); ?></button></div>
<p><?php echo esc_html( 'View HTTP headers for the Configuration Wizard:' ); ?> <button id="vpn-server-show"><?php echo esc_html( 'Show HTTP Headers' ); ?></button></p>
<noscript><?php echo esc_html( 'JavaScript must be enabled to generate and copy a key or to show HTTP headers.' ); ?></noscript>
</main>
<dialog id="vpn-server-dialog"><form method="dialog"><button id="vpn-server-close" aria-label="<?php echo esc_attr( 'Close' ); ?>">&times;</button>
<h2><?php echo esc_html( 'HTTP headers' ); ?></h2>
<p><?php echo esc_html( 'Headers seen by this server may be incomplete. For exact values, press F12 and copy the request headers from the Network tab.' ); ?></p>
<textarea id="vpn-server-headers" aria-label="<?php echo esc_attr( 'HTTP headers' ); ?>" readonly><?php echo esc_textarea( implode( "\n", $lines ) ); ?></textarea>
<p id="vpn-server-buttons"><button id="vpn-server-copy-headers" type="button"><?php echo esc_html( 'Copy' ); ?></button> <button><?php echo esc_html( 'OK' ); ?></button></p>
</form></dialog>
		<?php wp_print_script_tag( array( 'src' => add_query_arg( 'ver', $version, $script ) ) ); ?>
</body>
</html>
		<?php
	}
}
