<?php
/**
 * Status page of the VPN server endpoint, protocol.md chapter 5.1.
 *
 * The page is a standalone HTML document: client requests end before the theme loads.
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
		$settings = Settings::get();
		$tutorial = plugins_url( 'assets/tutorial.png', $plugin_file );
		$script   = plugins_url( 'assets/status-page.js', $plugin_file );
		$version  = (string) filemtime( dirname( $plugin_file ) . '/assets/status-page.js' );
		?>
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width">
<meta name="robots" content="noindex">
<title>VPN server is running</title>
<style>
*{box-sizing:border-box}body{margin:0;padding:2rem 1rem;min-height:100vh;display:flex;flex-direction:column;align-items:center;background:#eee;font:16px Arial,sans-serif}main{width:920px;max-width:100%;padding:1.5rem;background:#fff;border-radius:1rem;box-shadow:0 .4rem 1.5rem #0003}h1,h2{margin-top:0}aside{padding:1rem;background:#fee;color:#900;border:1px solid #e00;border-radius:.6rem}button{padding:.6rem;border:0;border-radius:.5rem;background:#06c;color:#fff;cursor:pointer}aside button{background:#e00;color:#fff}hr{border:0;border-top:1px solid #ddd;margin:1.5rem 0}.setup{display:grid;grid-template-columns:minmax(0,1fr) 240px;gap:1.5rem;align-items:start}.setup img{width:100%;max-width:240px;height:auto;border:1px solid #bbb}.setup p:first-child{margin-top:0}.setup li{margin:.75rem 0}#vpn-server-url{display:block;margin:.5rem 0;padding:.75rem;background:#eee;border-radius:.4rem;font-family:monospace;overflow-wrap:anywhere}@media(max-width:640px){.setup{grid-template-columns:1fr}.downloads{flex-direction:column;align-items:flex-start}}.downloads{display:flex;flex-wrap:wrap;gap:.75rem}.downloads select{min-width:0;max-width:100%;padding:.55rem;border:1px solid #bbb;border-radius:.5rem;background:#fff;font:inherit}.downloads a{padding:.6rem 1rem;border-radius:.5rem;background:#06c;color:#fff;text-decoration:none}#vpn-server-row:not([hidden]){display:flex;gap:.5rem}#vpn-server-key{min-width:0;flex:1;font-family:monospace}dialog{width:900px;max-width:calc(100% - 2rem);border:0;border-radius:1rem;padding:1.5rem}dialog::backdrop{background:#0006}#vpn-server-close,#vpn-server-key-close{float:right;padding:0 .5rem;background:none;color:#000;font-size:1.5rem}#vpn-server-headers{width:100%;height:14rem;font-family:monospace;white-space:pre}#vpn-server-buttons,#vpn-server-key-buttons{text-align:right}#vpn-server-footer{color:#777;font-size:.9rem}#vpn-server-footer a{color:inherit}
</style>
</head>
<body>
<main>
<h1>VPN server is running</h1>
<p>This server implements the Packet Handoff Protocol for virtual private networks (PHPVPN). The client application can be configured by following the instructions below.</p><?php if ( $insecure ) : ?>
<aside><b>Warning:</b> The server secret is predictable. A secure value must be saved by an administrator in Settings, VPN Server immediately. A secure value can be created using the Generate key button below.<p>Secure hexadecimal key: <button id="vpn-server-generate">Generate key</button>
</p>
<div id="vpn-server-row" hidden><input id="vpn-server-key" aria-label="Generated key" readonly><button id="vpn-server-copy-key">Copy key</button>
</div>
</aside>
<?php endif; ?>
<hr>
<section class=setup>
<div>
<p><strong>Step 1 - Download and install the client app</strong>
</p>
<p>To connect to this VPN server, the client application for your operating system must first be selected in the list below, downloaded and installed. The source code can be opened from the same list.</p>
<?php if ( array() !== $settings['client_downloads'] ) : ?>
<?php $first = $settings['client_downloads'][0]; ?>
<div class=downloads><select id="vpn-server-downloads" aria-label="Client application"><?php foreach ( $settings['client_downloads'] as $download ) : ?><option value="<?php echo esc_url( $download['url'] ); ?>" data-type="<?php echo esc_attr( $download['type'] ); ?>"<?php echo '' === $download['os'] ? '' : ' data-os="' . esc_attr( $download['os'] ) . '"'; ?>><?php echo esc_html( $download['name'] ); ?></option><?php endforeach; ?></select><a id="vpn-server-download" href="<?php echo esc_url( $first['url'] ); ?>"<?php echo 'link' === $first['type'] ? ' target="_blank"' : ''; ?> rel="noopener"><?php echo 'link' === $first['type'] ? 'Open' : 'Download'; ?></a>
</div>
<?php endif; ?>
<p>After installation, the PHPVPN client app should be launched. On Windows, it can be opened from the system tray or located by searching for phpvpn.exe in the Start menu. A similar procedure can be followed on macOS and Linux.</p>
</div><img src="<?php echo esc_url( $tutorial ); ?>" alt="Windows system tray menu with the Configuration Wizard and Edit Config options highlighted" width=240 height=180>
</section>
<hr>
<section>
<p><strong>Step 2A - Configure the client app</strong>
</p>
<p>If a configuration file has been provided by your administrator, its contents can be pasted into the configuration form accessed through <b>Edit Config</b>.</p>
<p><strong>Step 2B - Use the Configuration Wizard</strong>
</p>
<p>If no configuration file has been provided, the <b>Configuration Wizard</b> can be opened from the client menu. The following steps should then be completed:</p>
<ul>
<li>The VPN server URL should be set to the address of this page:<code id="vpn-server-url"></code>
</li>
<li>The connection to the server can be checked by clicking the &quot;Verify &amp; continue&quot; button in the Configuration Wizard. If the connection fails, the HTTP request headers can be viewed using <button id="vpn-server-show">Show HTTP Headers</button> and then copied into the Configuration Wizard.</li>
<li>The correct Server Secret should be entered, or the default used if applicable.</li>
<li>The correct IP Address and the corresponding Access Token should be entered, or the defaults used if applicable.</li>
<li>The default Communication Contracts should be generated.</li>
<li>The Server Secret and, where required, the IP Address and Access Token should be provided by your administrator.</li>
</ul>
</section>
<noscript>JavaScript must be enabled so that a key can be generated and copied and HTTP headers can be viewed.</noscript>
</main>
<p id="vpn-server-footer">Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p>
<dialog id="vpn-server-dialog">
<form method=dialog><button id="vpn-server-close" aria-label=Close>&times;</button>
<h2>HTTP headers</h2>
<p>The HTTP request headers received by this server may be incomplete. For exact values, your browser&apos;s developer tools can be opened with F12, and the request headers can be copied from the Network tab.</p><textarea id="vpn-server-headers" aria-label="HTTP headers" readonly><?php echo esc_textarea( implode( "\n", $lines ) ); ?></textarea>
<p id="vpn-server-buttons"><button id="vpn-server-copy-headers" type=button>Copy</button> <button>OK</button>
</p>
</form>
</dialog><?php if ( $insecure ) : ?>
<dialog id="vpn-server-key-dialog" aria-labelledby="vpn-server-key-title">
<form method=dialog><button id="vpn-server-key-close" aria-label=Close>&times;</button>
<h2 id="vpn-server-key-title">SERVER_SECRET copied successfully</h2>
<p>The key generated by JavaScript in the web browser has been copied to the clipboard. If you administer this WordPress site, replace the Server secret value in Settings, VPN Server with this key and click Save Changes to improve the security of the VPN server. Otherwise, ask your administrator to change the server secret. Generating or copying a key on this page does not change the server settings.</p>
<p id="vpn-server-key-buttons"><button>OK</button>
</p>
</form>
</dialog>
<?php endif; ?>
		<?php wp_print_script_tag( array( 'src' => add_query_arg( 'ver', $version, $script ) ) ); ?>
</body>
</html>
		<?php
	}
}
