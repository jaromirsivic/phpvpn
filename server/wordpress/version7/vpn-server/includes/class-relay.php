<?php
/**
 * Answers PHPVPN protocol requests on the relay endpoint.
 *
 * Loaded on every request, so it stays small: the protocol core is required only when the
 * request targets the endpoint. The fast route mu-plugin calls maybe_serve() as well.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

require_once __DIR__ . '/class-settings.php';

/**
 * Routing and request handling of the relay endpoint.
 */
final class Relay {

	/** Actions of the wire protocol, protocol.md chapter 4. */
	const ACTIONS = array( 'health', 'info', 'data' );

	/**
	 * Serves the request and exits when it targets the relay endpoint, otherwise returns at once.
	 *
	 * @param string $plugin_file Main plugin file. The fast route passes it because it runs before
	 *                            the main file has been loaded. Empty means PLUGIN_FILE.
	 */
	public static function maybe_serve( $plugin_file = '' ): void {
		if ( ! isset( $_SERVER['REQUEST_URI'] ) ) {
			return;
		}
		$settings = Settings::get();
		$uri      = sanitize_url( wp_unslash( $_SERVER['REQUEST_URI'] ) );
		if ( ! self::is_endpoint( $uri, $settings['endpoint'] ) ) {
			return;
		}
		self::serve( $settings, is_string( $plugin_file ) && '' !== $plugin_file ? $plugin_file : PLUGIN_FILE );
		exit;
	}

	/**
	 * True when the path of the request URI is the endpoint, with or without a trailing slash.
	 *
	 * @param string $uri      Request URI.
	 * @param string $endpoint Endpoint path below the site address.
	 */
	public static function is_endpoint( string $uri, string $endpoint ): bool {
		$path = wp_parse_url( $uri, PHP_URL_PATH );
		$home = wp_parse_url( home_url( '/' ), PHP_URL_PATH );
		if ( ! is_string( $path ) ) {
			return false;
		}
		$base = rtrim( is_string( $home ) ? $home : '', '/' ) . '/' . $endpoint;
		return $path === $base || $path === $base . '/';
	}

	/**
	 * Answers one protocol request. Nothing outside this request is changed.
	 *
	 * @param array{secret: string, access_keys: array<string, string>, storage: string, status_page: bool} $settings Current settings.
	 * @param string $plugin_file Main plugin file, used for asset URLs.
	 */
	private static function serve( array $settings, string $plugin_file ): void {
		require_once __DIR__ . '/relay-core.php';
		require_once __DIR__ . '/class-status-page.php';

		// A notice or warning printed into the body would break the HMAC protected response,
		// chapter 4.1. The handler is active only while this request is answered.
		set_error_handler( array( self::class, 'ignore_error' ) ); // phpcs:ignore WordPress.PHP.DevelopmentFunctions.error_log_set_error_handler -- restored in finally below.
		// A client that disconnects must not interrupt a claimed queue batch half way.
		ignore_user_abort( true );

		$request = self::request();
		$writer  = new \PhpVpn\HttpResponseWriter( 'HEAD' === $request->method );
		try {
			if ( null === $request->action || '' === $request->action ) {
				$page = $settings['status_page']
					? static function () use ( $writer, $settings, $plugin_file ): void {
						Status_Page::render( $writer, \PhpVpn\Application::isSecretInsecure( $settings['secret'] ), $plugin_file );
					}
					: null;
				\PhpVpn\Application::handleRoot( $request, $writer, $page );
			} else {
				( new \PhpVpn\Application() )->handleAction(
					$request,
					$writer,
					static function () use ( $settings ): \PhpVpn\Config {
						if ( '' === $settings['storage'] ) {
							throw new \PhpVpn\ConfigException( 'storage' );
						}
						return \PhpVpn\Config::fromValues( $settings['secret'], $settings['access_keys'], $settings['storage'] );
					}
				);
			}
		} catch ( \Throwable $error ) {
			if ( ! $writer->isStarted() ) {
				\PhpVpn\Http::error( $writer, 500, 'internal_error' );
			}
		} finally {
			restore_error_handler();
		}

		// Hands the response to the client before shutdown hooks of other plugins run.
		if ( function_exists( 'fastcgi_finish_request' ) ) {
			fastcgi_finish_request();
		}
	}

	/**
	 * Error handler of a relay request: swallows the error, the response reports failures itself.
	 *
	 * @return bool Always true, the PHP handler does not run.
	 */
	public static function ignore_error(): bool {
		return true;
	}

	/**
	 * The protocol request. The body is read later from php://input by the protocol core.
	 */
	private static function request(): \PhpVpn\Request {
		$method = isset( $_SERVER['REQUEST_METHOD'] ) ? strtoupper( sanitize_text_field( wp_unslash( $_SERVER['REQUEST_METHOD'] ) ) ) : 'GET';

		// Protocol requests authenticate with an HMAC in the body, chapter 8, not with a nonce.
		// phpcs:disable WordPress.Security.NonceVerification.Recommended
		$action = null;
		if ( isset( $_GET['action'] ) ) {
			// Only the exact action names are valid, chapter 5: "data " or "Health" must answer
			// 404 not_found, so the value is compared unchanged against the allowlist instead of
			// being trimmed by a sanitizer first. Nothing else is ever used.
			// phpcs:ignore WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- exact allowlist below.
			$raw    = is_string( $_GET['action'] ) ? wp_unslash( $_GET['action'] ) : 'invalid';
			$action = '' === $raw || in_array( $raw, self::ACTIONS, true ) ? $raw : 'invalid';
		}
		// phpcs:enable WordPress.Security.NonceVerification.Recommended

		$type   = isset( $_SERVER['CONTENT_TYPE'] ) ? sanitize_text_field( wp_unslash( $_SERVER['CONTENT_TYPE'] ) ) : null;
		$length = isset( $_SERVER['CONTENT_LENGTH'] ) ? sanitize_text_field( wp_unslash( $_SERVER['CONTENT_LENGTH'] ) ) : '';

		return new \PhpVpn\Request(
			$method,
			$action,
			$type,
			ctype_digit( $length ) ? (int) $length : null,
			new \PhpVpn\PhpInput()
		);
	}
}
