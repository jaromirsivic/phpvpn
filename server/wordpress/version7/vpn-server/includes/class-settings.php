<?php
/**
 * Plugin settings, stored in one autoloaded option so a relay request needs no extra query.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Reads, validates and writes the vpn_server_settings option.
 */
final class Settings {

	const OPTION           = 'vpn_server_settings';
	const DEFAULT_ENDPOINT = 'vpn-relay';

	/** Storage folders the plugin creates itself, the only ones uninstall removes. */
	const MANAGED_STORAGE_PATTERN = '/^vpn-server-[0-9a-f]{16}$/';

	/**
	 * Current settings with defaults for missing or malformed members.
	 *
	 * @return array{secret: string, access_keys: array<string, string>, endpoint: string, storage: string, storage_managed: bool, status_page: bool}
	 */
	public static function get(): array {
		$stored = get_option( self::OPTION );
		return self::normalize( is_array( $stored ) ? $stored : array() );
	}

	/**
	 * Saves complete settings.
	 *
	 * @param array<string, mixed> $values Settings as returned by get().
	 */
	public static function save( array $values ): void {
		update_option( self::OPTION, self::normalize( $values ), true );
	}

	/**
	 * Keeps only known members of the expected types.
	 *
	 * @param array<mixed, mixed> $values Raw option value.
	 * @return array{secret: string, access_keys: array<string, string>, endpoint: string, storage: string, storage_managed: bool, status_page: bool}
	 */
	public static function normalize( array $values ): array {
		$keys = array();
		if ( isset( $values['access_keys'] ) && is_array( $values['access_keys'] ) ) {
			foreach ( $values['access_keys'] as $address => $key ) {
				if ( is_string( $address ) && is_string( $key ) ) {
					$keys[ $address ] = $key;
				}
			}
		}
		$endpoint = isset( $values['endpoint'] ) && is_string( $values['endpoint'] ) ? self::sanitize_endpoint( $values['endpoint'] ) : '';
		return array(
			'secret'          => isset( $values['secret'] ) && is_string( $values['secret'] ) ? $values['secret'] : '',
			'access_keys'     => $keys,
			'endpoint'        => '' === $endpoint ? self::DEFAULT_ENDPOINT : $endpoint,
			'storage'         => isset( $values['storage'] ) && is_string( $values['storage'] ) ? $values['storage'] : '',
			'storage_managed' => ! empty( $values['storage_managed'] ),
			'status_page'     => ! isset( $values['status_page'] ) || (bool) $values['status_page'],
		);
	}

	/**
	 * Endpoint path below the site address: lowercase segments of letters, digits, '-' and '_'.
	 *
	 * @param string $value Raw input.
	 */
	public static function sanitize_endpoint( string $value ): string {
		$segments = array();
		foreach ( explode( '/', strtolower( $value ) ) as $segment ) {
			$segment = preg_replace( '/[^a-z0-9_-]/', '', $segment );
			if ( '' !== $segment ) {
				$segments[] = $segment;
			}
		}
		return implode( '/', $segments );
	}

	/**
	 * Server URL the clients use. The client appends ?action=..., so it never carries a query.
	 *
	 * @param array{endpoint: string} $settings Current settings.
	 */
	public static function endpoint_url( array $settings ): string {
		return home_url( '/' . $settings['endpoint'] . '/' );
	}

	/**
	 * Lowercase hexadecimal text of random bytes.
	 *
	 * @param int $bytes Number of random bytes.
	 */
	public static function random_hex( int $bytes ): string {
		return bin2hex( random_bytes( $bytes ) );
	}

	/**
	 * Default storage folder: a random, unguessable name inside the uploads folder.
	 *
	 * @return string Absolute path, empty when the uploads folder is unavailable.
	 */
	public static function default_storage(): string {
		$uploads = wp_upload_dir( null, false );
		if ( ! empty( $uploads['error'] ) || empty( $uploads['basedir'] ) ) {
			return '';
		}
		return trailingslashit( wp_normalize_path( $uploads['basedir'] ) ) . 'vpn-server-' . self::random_hex( 8 );
	}

	/**
	 * True when the storage folder was created by the plugin inside the uploads folder.
	 *
	 * @param array{storage: string, storage_managed: bool} $settings Current settings.
	 */
	public static function is_managed_storage( array $settings ): bool {
		return $settings['storage_managed'] && 1 === preg_match( self::MANAGED_STORAGE_PATTERN, basename( $settings['storage'] ) );
	}

	/**
	 * Activation: refuses 32-bit PHP and creates the settings once, existing values stay.
	 *
	 * @param bool $network_wide True for a network activation on multisite.
	 */
	public static function activate( $network_wide = false ): void {
		if ( PHP_INT_SIZE < 8 ) {
			deactivate_plugins( plugin_basename( PLUGIN_FILE ) );
			wp_die( esc_html__( 'VPN Server requires 64-bit PHP.', 'vpn-server' ) );
		}
		if ( $network_wide ) {
			deactivate_plugins( plugin_basename( PLUGIN_FILE ), true, true );
			wp_die( esc_html__( 'VPN Server cannot be network activated. Activate it on each site that should run a relay.', 'vpn-server' ) );
		}
		if ( false !== get_option( self::OPTION ) ) {
			return;
		}
		$storage = self::default_storage();
		add_option(
			self::OPTION,
			self::normalize(
				array(
					'secret'          => self::random_hex( 32 ),
					'access_keys'     => array(),
					'endpoint'        => self::DEFAULT_ENDPOINT,
					'storage'         => $storage,
					'storage_managed' => '' !== $storage,
					'status_page'     => true,
				)
			),
			'',
			true
		);
	}
}
