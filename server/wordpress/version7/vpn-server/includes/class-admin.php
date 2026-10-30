<?php
/**
 * Settings, VPN Server: endpoint, clients and access keys, server secret, storage and checks.
 *
 * @package VpnServer
 */

namespace Pilandrome\VpnServer;

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

/**
 * Administration screen. Every change goes through admin-post.php with a nonce and a capability check.
 */
final class Admin {

	const PAGE       = 'vpn-server';
	const CAPABILITY = 'manage_options';
	const NOTICE     = 'vpn-server-notice';
	const SELF_TEST  = 'vpn_server_self_test';

	/** Default request headers of the PHPVPN client, client/src/core/config/config.h. */
	const CLIENT_HTTP_HEADERS = "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0";

	/** First path segments that belong to WordPress itself. */
	const RESERVED_ENDPOINTS = array( 'wp-admin', 'wp-content', 'wp-includes', 'wp-json', 'feed', 'xmlrpc.php' );

	/** admin-post.php actions, without the vpn_server_ prefix, and their handlers. */
	const ACTIONS = array(
		'save'           => 'handle_save',
		'downloads'      => 'handle_downloads',
		'storage'        => 'handle_storage',
		'add_client'     => 'handle_add_client',
		'set_key'        => 'handle_set_key',
		'default_key'    => 'handle_default_key',
		'client_key'     => 'handle_client_key',
		'remove_client'  => 'handle_remove_client',
		'client_config'  => 'handle_client_config',
		'fast_route_on'  => 'handle_fast_route_on',
		'fast_route_off' => 'handle_fast_route_off',
		'self_test'      => 'handle_self_test',
	);

	/**
	 * Hooks the screen.
	 */
	public static function register(): void {
		add_action( 'admin_menu', array( self::class, 'menu' ) );
		add_action( 'admin_enqueue_scripts', array( self::class, 'enqueue' ) );
		add_filter( 'plugin_action_links_' . plugin_basename( PLUGIN_FILE ), array( self::class, 'action_links' ) );
		foreach ( self::ACTIONS as $action => $handler ) {
			add_action( 'admin_post_vpn_server_' . $action, array( self::class, $handler ) );
		}
	}

	/**
	 * Adds Settings, VPN Server.
	 */
	public static function menu(): void {
		add_options_page(
			__( 'VPN Server', 'vpn-server' ),
			__( 'VPN Server', 'vpn-server' ),
			self::CAPABILITY,
			self::PAGE,
			array( self::class, 'render' )
		);
	}

	/**
	 * Adds a Settings link to the plugin row.
	 *
	 * @param string[] $links Existing links.
	 * @return string[]
	 */
	public static function action_links( $links ): array {
		$links   = is_array( $links ) ? $links : array();
		$links[] = '<a href="' . esc_url( self::page_url() ) . '">' . esc_html__( 'Settings', 'vpn-server' ) . '</a>';
		return $links;
	}

	/**
	 * Loads the screen assets on this screen only.
	 *
	 * @param string $hook_suffix Current admin screen.
	 */
	public static function enqueue( $hook_suffix ): void {
		if ( 'settings_page_' . self::PAGE !== $hook_suffix ) {
			return;
		}
		// The file time as version reloads changed assets even when the plugin version stays.
		$assets = dirname( PLUGIN_FILE ) . '/assets/';
		wp_enqueue_style( 'vpn-server-admin', plugins_url( 'assets/admin.css', PLUGIN_FILE ), array(), (string) filemtime( $assets . 'admin.css' ) );
		wp_enqueue_script( 'vpn-server-admin', plugins_url( 'assets/admin.js', PLUGIN_FILE ), array(), (string) filemtime( $assets . 'admin.js' ), array( 'in_footer' => true ) );
		wp_localize_script(
			'vpn-server-admin',
			'vpnServerAdmin',
			array(
				'copied' => __( 'Copied', 'vpn-server' ),
			)
		);
	}

	/**
	 * URL of this screen.
	 */
	public static function page_url(): string {
		return admin_url( 'options-general.php?page=' . self::PAGE );
	}

	// ---------------------------------------------------------------------
	// Screen
	// ---------------------------------------------------------------------

	/**
	 * Renders the screen.
	 */
	public static function render(): void {
		if ( ! current_user_can( self::CAPABILITY ) ) {
			return;
		}
		require_once __DIR__ . '/server-core.php';
		$settings = Settings::get();
		?>
		<div class="wrap vpn-server">
			<h1><?php echo esc_html__( 'VPN Server', 'vpn-server' ); ?></h1>
			<?php self::render_notice(); ?>
			<p class="vpn-server-intro"><?php echo esc_html__( 'This site forwards encrypted traffic between PHPVPN clients. The VPN server never sees the payload: clients encrypt it with keys that only they know.', 'vpn-server' ); ?></p>
			<?php
			self::render_connection( $settings );
			self::render_clients( $settings );
			self::render_downloads( $settings );
			self::render_storage( $settings );
			self::render_fast_route();
			self::render_checks( $settings );
			?>
		</div>
		<?php
	}

	/**
	 * Result of the last admin-post action.
	 */
	private static function render_notice(): void {
		// Only a code from the redirect of a nonce checked action, mapped to fixed messages.
		// phpcs:ignore WordPress.Security.NonceVerification.Recommended
		$code     = isset( $_GET[ self::NOTICE ] ) ? sanitize_key( wp_unslash( $_GET[ self::NOTICE ] ) ) : '';
		$messages = array(
			'saved'              => array( 'success', __( 'Settings saved.', 'vpn-server' ) ),
			'unchanged'          => array( 'info', __( 'Nothing was changed.', 'vpn-server' ) ),
			'reconnect'          => array( 'success', __( 'Settings saved. The Server URL or the server secret changed, update it in every client.', 'vpn-server' ) ),
			'storage'            => array( 'success', __( 'The storage folder was changed.', 'vpn-server' ) ),
			'client_added'       => array( 'success', __( 'Client added. Download its configuration and import it in the client.', 'vpn-server' ) ),
			'client_key'         => array( 'success', __( 'A new access key was generated. Update accessKey in that client.', 'vpn-server' ) ),
			'key_saved'          => array( 'success', __( 'The access key was saved. Update accessKey in that client.', 'vpn-server' ) ),
			'default_key'        => array( 'success', __( 'The default access key was set. Update accessKey in that client.', 'vpn-server' ) ),
			'client_removed'     => array( 'success', __( 'Client removed.', 'vpn-server' ) ),
			'fast_route_on'      => array( 'success', __( 'The fast route is installed.', 'vpn-server' ) ),
			'fast_route_off'     => array( 'success', __( 'The fast route is removed.', 'vpn-server' ) ),
			'self_test'          => array( 'info', __( 'The self test finished, see Checks below.', 'vpn-server' ) ),
			'bad_endpoint'       => array( 'error', __( 'The path of the Server URL is empty or reserved by WordPress.', 'vpn-server' ) ),
			'bad_download_url'   => array( 'error', __( 'Each client download needs a name of at most 100 characters, an absolute HTTP or HTTPS URL, a type and a system from the lists. At most 20 downloads can be saved.', 'vpn-server' ) ),
			'bad_storage'        => array( 'error', __( 'The storage folder must be an absolute path to a dedicated folder that WordPress can create and write. It cannot be the site root, wp-content, the uploads folder itself, a folder that contains the site, or a folder inside the plugins, themes, wp-admin or wp-includes folders.', 'vpn-server' ) ),
			'network_only'       => array( 'error', __( 'On a multisite network only a super admin can install the fast route or move the storage folder outside the uploads folder.', 'vpn-server' ) ),
			'bad_secret'         => array( 'error', __( 'The server secret must have 16 to 256 printable ASCII characters without spaces.', 'vpn-server' ) ),
			'bad_address'        => array( 'error', __( 'The VPN address is invalid or already used.', 'vpn-server' ) ),
			'bad_key'            => array( 'error', __( 'The access key must have exactly 64 hexadecimal characters.', 'vpn-server' ) ),
			'duplicate_key'      => array( 'error', __( 'Another client already uses this access key.', 'vpn-server' ) ),
			'no_address'         => array( 'error', __( 'All VPN addresses are already used.', 'vpn-server' ) ),
			'unknown_client'     => array( 'error', __( 'The client does not exist.', 'vpn-server' ) ),
			'fast_route_failed'  => array( 'error', __( 'The fast route could not be installed. WordPress needs direct write access to wp-content/mu-plugins.', 'vpn-server' ) ),
		);
		if ( ! isset( $messages[ $code ] ) ) {
			return;
		}
		printf(
			'<div class="notice notice-%1$s is-dismissible"><p>%2$s</p></div>',
			esc_attr( $messages[ $code ][0] ),
			esc_html( $messages[ $code ][1] )
		);
	}

	/**
	 * Opens a form that posts to admin-post.php with a nonce.
	 *
	 * @param string $action  Action without the vpn_server_ prefix.
	 * @param string $classes Extra CSS classes.
	 * @param string $id      Form id, empty for none.
	 */
	private static function form_start( string $action, string $classes = '', string $id = '' ): void {
		printf(
			'<form method="post" action="%1$s" class="%2$s"%3$s>',
			esc_url( admin_url( 'admin-post.php' ) ),
			esc_attr( trim( 'vpn-server-form ' . $classes ) ),
			'' === $id ? '' : ' id="' . esc_attr( $id ) . '"'
		);
		printf( '<input type="hidden" name="action" value="%s">', esc_attr( 'vpn_server_' . $action ) );
		wp_nonce_field( 'vpn_server_' . $action );
	}

	/**
	 * Server URL, status page and server secret, saved together.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_connection( array $settings ): void {
		$url = Settings::endpoint_url( $settings );
		?>
		<?php self::form_start( 'save', 'vpn-server-settings vpn-server-track' ); ?>
		<section class="vpn-server-card">
			<h2><?php echo esc_html__( 'Connection', 'vpn-server' ); ?></h2>
			<div class="vpn-server-field">
				<label class="vpn-server-label" for="vpn-server-endpoint"><?php echo esc_html__( 'Server URL', 'vpn-server' ); ?></label>
				<div class="vpn-server-control">
					<div class="vpn-server-line">
						<div class="vpn-server-group">
							<span class="vpn-server-addon"><?php echo esc_html( home_url( '/' ) ); ?></span>
							<input type="text" id="vpn-server-endpoint" name="endpoint" class="code" value="<?php echo esc_attr( $settings['endpoint'] ); ?>" spellcheck="false">
							<span class="vpn-server-addon">/</span>
						</div>
						<button type="button" class="button hide-if-no-js" data-vpn-server-copy-text="<?php echo esc_attr( $url ); ?>" data-vpn-server-saved-url><?php echo esc_html__( 'Copy', 'vpn-server' ); ?></button>
						<a class="button" href="<?php echo esc_url( $url ); ?>" target="_blank" rel="noopener noreferrer" data-vpn-server-saved-url><?php echo esc_html__( 'Open', 'vpn-server' ); ?></a>
						<span class="vpn-server-unsaved" hidden><?php echo esc_html__( 'Save changes first.', 'vpn-server' ); ?></span>
					</div>
					<p class="description"><?php echo esc_html__( 'Enter this address as Server URL in the PHPVPN client. The path may contain letters, digits, hyphens, underscores and slashes. Changing it disconnects all clients until they use the new Server URL.', 'vpn-server' ); ?></p>
				</div>
			</div>
			<div class="vpn-server-field">
				<span class="vpn-server-label"><?php echo esc_html__( 'Status page', 'vpn-server' ); ?></span>
				<div class="vpn-server-control">
					<label><input type="checkbox" name="status_page" value="1" <?php checked( $settings['status_page'] ); ?>> <?php echo esc_html__( 'Show the status page when the Server URL is opened in a browser', 'vpn-server' ); ?></label>
					<p class="description"><?php echo esc_html__( 'When it is off, a browser gets 404. The clients are not affected.', 'vpn-server' ); ?></p>
				</div>
			</div>
		</section>

		<?php $weakness = Settings::secret_weakness( $settings['secret'] ); ?>
		<section class="vpn-server-card vpn-server-secret-card <?php echo esc_attr( '' === $weakness ? 'is-secure' : 'is-insecure' ); ?>">
			<h2>
				<?php echo esc_html__( 'Server secret', 'vpn-server' ); ?>
				<span class="vpn-server-secret-state" data-weakness="default" <?php echo 'default' === $weakness ? '' : 'hidden'; ?>><?php echo esc_html__( '(default server secret, insecure: generate a new one)', 'vpn-server' ); ?></span>
				<span class="vpn-server-secret-state" data-weakness="short" <?php echo 'short' === $weakness ? '' : 'hidden'; ?>><?php echo esc_html( sprintf( /* translators: %d: minimum number of characters. */ __( '(shorter than %d characters, insecure: generate a new one)', 'vpn-server' ), Settings::SECURE_SECRET_LENGTH ) ); ?></span>
			</h2>
			<p class="vpn-server-lead"><?php echo esc_html__( 'Shared by the server and all its clients. Changing it disconnects every client until it gets the new value.', 'vpn-server' ); ?></p>
			<div class="vpn-server-field">
				<label class="vpn-server-label" for="vpn-server-secret"><?php echo esc_html__( 'Server secret', 'vpn-server' ); ?></label>
				<div class="vpn-server-control">
					<div class="vpn-server-line">
						<input type="text" id="vpn-server-secret" name="secret" class="code vpn-server-wide" autocomplete="off" spellcheck="false" value="<?php echo esc_attr( $settings['secret'] ); ?>" data-vpn-server-default-secret="<?php echo esc_attr( Settings::DEFAULT_SECRET ); ?>" data-vpn-server-secure-length="<?php echo esc_attr( (string) Settings::SECURE_SECRET_LENGTH ); ?>">
						<button type="button" class="button hide-if-no-js" data-vpn-server-copy="vpn-server-secret"><?php echo esc_html__( 'Copy', 'vpn-server' ); ?></button>
						<button type="button" class="button hide-if-no-js" data-vpn-server-generate="vpn-server-secret"><?php echo esc_html__( 'Generate New Secret', 'vpn-server' ); ?></button>
					</div>
					<p class="description"><?php echo esc_html__( 'Keep the generated value, or enter the secret of the PHPVPN server you move from. Generate New Secret only fills in the field, the secret changes when you save.', 'vpn-server' ); ?></p>
				</div>
			</div>
		</section>
		<p class="vpn-server-submit">
			<button type="submit" class="button button-primary" data-vpn-server-confirm-change="<?php echo esc_attr__( 'The Server URL or the server secret changed. All clients stop working until they get the new value. Save the changes?', 'vpn-server' ); ?>"><?php echo esc_html__( 'Save Changes', 'vpn-server' ); ?></button>
		</p>
		</form>
		<?php
	}

	/**
	 * Clients with their VPN address and access key.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_clients( array $settings ): void {
		$storage = '' !== $settings['storage'] ? new \PhpVpn\DiskStorage( $settings['storage'] ) : null;
		$keys    = $settings['access_keys'];
		$free    = self::free_addresses( $settings );
		uksort( $keys, static fn( $a, $b ): int => \PhpVpn\Validators::addressSortKey( (string) $a ) <=> \PhpVpn\Validators::addressSortKey( (string) $b ) );
		$labels = array(
			'address' => __( 'VPN address', 'vpn-server' ),
			'key'     => __( 'Access key', 'vpn-server' ),
			'seen'    => __( 'Last seen', 'vpn-server' ),
			'actions' => __( 'Actions', 'vpn-server' ),
		);
		?>
		<section class="vpn-server-card">
			<div class="vpn-server-card-head">
				<h2><?php echo esc_html__( 'Clients', 'vpn-server' ); ?></h2>
				<?php if ( array() === $free ) : ?>
					<span class="vpn-server-muted"><?php echo esc_html__( 'All VPN addresses are already used.', 'vpn-server' ); ?></span>
				<?php else : ?>
					<?php self::form_start( 'add_client', 'vpn-server-line' ); ?>
						<label class="screen-reader-text" for="vpn-server-address"><?php echo esc_html__( 'VPN address', 'vpn-server' ); ?></label>
						<select id="vpn-server-address" name="address">
							<?php
							foreach ( $free as $address ) {
								printf( '<option value="%1$s">%1$s</option>', esc_attr( $address ) );
							}
							?>
						</select>
						<button type="submit" class="button button-primary"><?php echo esc_html__( 'Add Client', 'vpn-server' ); ?></button>
					</form>
				<?php endif; ?>
			</div>
			<p class="vpn-server-lead"><?php echo esc_html__( 'Every client has its own VPN address and access key. Communication contracts and payload keys are set up between the clients themselves, the server never knows them.', 'vpn-server' ); ?></p>
			<div class="vpn-server-table-wrap">
			<table class="vpn-server-table">
				<thead>
					<tr>
						<?php foreach ( $labels as $label ) : ?>
							<th scope="col"><?php echo esc_html( $label ); ?></th>
						<?php endforeach; ?>
					</tr>
				</thead>
				<tbody>
				<?php if ( array() === $keys ) : ?>
					<tr><td colspan="4" class="vpn-server-empty"><?php echo esc_html__( 'No clients yet. Add the first one with Add Client.', 'vpn-server' ); ?></td></tr>
				<?php endif; ?>
				<?php foreach ( $keys as $address => $key ) : ?>
					<?php
					$address  = (string) $address;
					$suffix   = str_replace( '.', '-', $address );
					$field_id = 'vpn-server-key-' . $suffix;
					$form_id  = 'vpn-server-set-key-' . $suffix;
					$seen     = null !== $storage ? $storage->readLastSeenMs( $address ) : null;
					?>
					<tr<?php echo Settings::default_access_key( $address ) === $key ? '' : ' class="is-custom-key"'; ?>>
						<td data-label="<?php echo esc_attr( $labels['address'] ); ?>"><span class="vpn-server-badge"><?php echo esc_html( $address ); ?></span></td>
						<td data-label="<?php echo esc_attr( $labels['key'] ); ?>">
							<input type="text" class="code vpn-server-key" id="<?php echo esc_attr( $field_id ); ?>" name="access_key" form="<?php echo esc_attr( $form_id ); ?>" value="<?php echo esc_attr( $key ); ?>" aria-label="<?php echo esc_attr( sprintf( /* translators: %s: VPN address of the client. */ __( 'Access key of %s', 'vpn-server' ), $address ) ); ?>" pattern="[0-9a-fA-F]{64}" title="<?php echo esc_attr__( 'Exactly 64 hexadecimal characters.', 'vpn-server' ); ?>" required readonly autocomplete="off" spellcheck="false">
						</td>
						<td data-label="<?php echo esc_attr( $labels['seen'] ); ?>" class="vpn-server-muted">
							<?php
							if ( null === $seen ) {
								echo esc_html__( 'Never', 'vpn-server' );
							} else {
								/* translators: %s: human readable time difference, for example "5 mins". */
								echo esc_html( sprintf( __( '%s ago', 'vpn-server' ), human_time_diff( intdiv( $seen, 1000 ) ) ) );
							}
							?>
						</td>
						<td data-label="<?php echo esc_attr( $labels['actions'] ); ?>">
							<div class="vpn-server-actions vpn-server-view">
								<button type="button" class="button button-small hide-if-no-js" data-vpn-server-copy="<?php echo esc_attr( $field_id ); ?>"><?php echo esc_html__( 'Copy', 'vpn-server' ); ?></button>
								<button type="button" class="button button-small hide-if-no-js" data-vpn-server-edit="<?php echo esc_attr( $field_id ); ?>"><?php echo esc_html__( 'Edit', 'vpn-server' ); ?></button>
								<?php self::form_start( 'default_key' ); ?>
									<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
									<button type="submit" class="button button-small" data-vpn-server-confirm="<?php echo esc_attr__( 'Set the default access key? It is predictable, anyone who knows the server secret can then act as this client.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Default Key', 'vpn-server' ); ?></button>
								</form>
								<?php self::form_start( 'client_key' ); ?>
									<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
									<button type="submit" class="button button-small" data-vpn-server-confirm="<?php echo esc_attr__( 'Generate a new access key? The client stops working until it gets the new key.', 'vpn-server' ); ?>"><?php echo esc_html__( 'New Key', 'vpn-server' ); ?></button>
								</form>
								<?php self::form_start( 'client_config' ); ?>
									<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
									<button type="submit" class="button button-small"><?php echo esc_html__( 'Download config.json', 'vpn-server' ); ?></button>
								</form>
								<?php self::form_start( 'remove_client' ); ?>
									<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
									<button type="submit" class="button button-small vpn-server-danger" data-vpn-server-confirm="<?php echo esc_attr__( 'Remove this client? It can no longer connect.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Remove', 'vpn-server' ); ?></button>
								</form>
							</div>
							<?php // The access key field joins this form through its form attribute. ?>
							<?php self::form_start( 'set_key', 'vpn-server-actions vpn-server-edit', $form_id ); ?>
								<input type="hidden" name="address" value="<?php echo esc_attr( $address ); ?>">
								<button type="submit" class="button button-small button-primary" data-vpn-server-confirm-change="<?php echo esc_attr__( 'Save the access key? The client stops working until it gets the new key.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Save', 'vpn-server' ); ?></button>
								<button type="button" class="button button-small" data-vpn-server-cancel="<?php echo esc_attr( $field_id ); ?>"><?php echo esc_html__( 'Cancel', 'vpn-server' ); ?></button>
							</form>
						</td>
					</tr>
				<?php endforeach; ?>
				</tbody>
			</table>
			</div>
		</section>
		<?php
	}

	/**
	 * Client downloads, in a separate section below the complete client list: the entries of the select
	 * box on the status page, in this order. Add Download copies the template row; without JavaScript
	 * one spare empty row is shown instead. A row with an empty name and URL is not saved.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_downloads( array $settings ): void {
		$labels = array(
			'name'    => __( 'Name', 'vpn-server' ),
			'url'     => __( 'URL', 'vpn-server' ),
			'type'    => __( 'Type', 'vpn-server' ),
			'os'      => __( 'Preselected for', 'vpn-server' ),
			'actions' => __( 'Actions', 'vpn-server' ),
		);
		$rows   = $settings['client_downloads'];
		?>
		<section class="vpn-server-card">
			<h2><?php echo esc_html__( 'Client downloads', 'vpn-server' ); ?></h2>
			<p class="vpn-server-lead"><?php echo esc_html__( 'These entries are offered in the select box of the public status page, in this order. A file shows a Download button, a link shows an Open button that opens the URL in a new tab. The status page preselects the entry for the operating system of the visitor. A row with an empty name and URL is not saved.', 'vpn-server' ); ?></p>
			<?php self::form_start( 'downloads', 'vpn-server-track' ); ?>
			<div class="vpn-server-table-wrap">
			<table class="vpn-server-table vpn-server-downloads">
				<thead>
					<tr>
						<?php foreach ( $labels as $label ) : ?>
							<th scope="col"><?php echo esc_html( $label ); ?></th>
						<?php endforeach; ?>
					</tr>
				</thead>
				<tbody id="vpn-server-downloads" data-vpn-server-max="<?php echo esc_attr( (string) Settings::MAX_CLIENT_DOWNLOADS ); ?>">
				<?php
				foreach ( $rows as $index => $download ) {
					self::render_download_row( (string) $index, $download, $labels, '' );
				}
				if ( count( $rows ) < Settings::MAX_CLIENT_DOWNLOADS ) {
					self::render_download_row( (string) count( $rows ), null, $labels, 'hide-if-js' );
				}
				?>
				</tbody>
			</table>
			</div>
			<template id="vpn-server-download-template"><?php self::render_download_row( '__index__', null, $labels, '' ); ?></template>
			<p class="vpn-server-submit">
				<button type="button" class="button hide-if-no-js" data-vpn-server-add-download><?php echo esc_html__( 'Add Download', 'vpn-server' ); ?></button>
				<button type="submit" class="button button-primary"><?php echo esc_html__( 'Save Changes', 'vpn-server' ); ?></button>
			</p>
			</form>
		</section>
		<?php
	}

	/**
	 * One row of the client downloads table. Saved rows are required, an empty row is optional.
	 *
	 * @param string                                                          $index    Index of the row in the posted list.
	 * @param array{name: string, url: string, type: string, os: string}|null $download Saved entry, null for an empty row.
	 * @param array<string, string>                                           $labels   Column labels.
	 * @param string                                                          $classes  CSS classes of the row.
	 */
	private static function render_download_row( string $index, ?array $download, array $labels, string $classes ): void {
		$field    = 'client_downloads[' . $index . ']';
		$required = null === $download ? '' : ' required';
		$types    = array(
			'file' => __( 'File (Download)', 'vpn-server' ),
			'link' => __( 'Link (Open)', 'vpn-server' ),
		);
		$systems  = array(
			''        => __( 'None', 'vpn-server' ),
			'windows' => 'Windows',
			'macos'   => 'macOS',
			'linux'   => 'Linux',
		);
		?>
		<tr<?php echo '' === $classes ? '' : ' class="' . esc_attr( $classes ) . '"'; ?>>
			<td data-label="<?php echo esc_attr( $labels['name'] ); ?>">
				<input type="text" name="<?php echo esc_attr( $field . '[name]' ); ?>" value="<?php echo esc_attr( $download['name'] ?? '' ); ?>" maxlength="<?php echo esc_attr( (string) Settings::MAX_DOWNLOAD_NAME_LENGTH ); ?>" aria-label="<?php echo esc_attr( $labels['name'] ); ?>"<?php echo $required; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped -- constant attribute. ?>>
			</td>
			<td data-label="<?php echo esc_attr( $labels['url'] ); ?>">
				<input type="url" class="code" name="<?php echo esc_attr( $field . '[url]' ); ?>" value="<?php echo esc_attr( $download['url'] ?? '' ); ?>" aria-label="<?php echo esc_attr( $labels['url'] ); ?>" spellcheck="false"<?php echo $required; // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped -- constant attribute. ?>>
			</td>
			<td data-label="<?php echo esc_attr( $labels['type'] ); ?>">
				<select name="<?php echo esc_attr( $field . '[type]' ); ?>" aria-label="<?php echo esc_attr( $labels['type'] ); ?>">
					<?php foreach ( $types as $value => $label ) : ?>
						<option value="<?php echo esc_attr( $value ); ?>"<?php selected( $download['type'] ?? 'file', $value ); ?>><?php echo esc_html( $label ); ?></option>
					<?php endforeach; ?>
				</select>
			</td>
			<td data-label="<?php echo esc_attr( $labels['os'] ); ?>">
				<select name="<?php echo esc_attr( $field . '[os]' ); ?>" aria-label="<?php echo esc_attr( $labels['os'] ); ?>">
					<?php foreach ( $systems as $value => $label ) : ?>
						<option value="<?php echo esc_attr( $value ); ?>"<?php selected( $download['os'] ?? '', $value ); ?>><?php echo esc_html( $label ); ?></option>
					<?php endforeach; ?>
				</select>
			</td>
			<td data-label="<?php echo esc_attr( $labels['actions'] ); ?>">
				<div class="vpn-server-actions hide-if-no-js">
					<button type="button" class="button button-small" data-vpn-server-move-download="-1" aria-label="<?php echo esc_attr__( 'Move up', 'vpn-server' ); ?>">&uarr;</button>
					<button type="button" class="button button-small" data-vpn-server-move-download="1" aria-label="<?php echo esc_attr__( 'Move down', 'vpn-server' ); ?>">&darr;</button>
					<button type="button" class="button button-small vpn-server-danger" data-vpn-server-remove-download><?php echo esc_html__( 'Remove', 'vpn-server' ); ?></button>
				</div>
			</td>
		</tr>
		<?php
	}

	/**
	 * Storage folder, its protection and the nginx rule.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_storage( array $settings ): void {
		$location = self::storage_url_path( $settings );
		?>
		<section class="vpn-server-card">
			<h2><?php echo esc_html__( 'Storage protection', 'vpn-server' ); ?></h2>
			<?php self::form_start( 'storage', 'vpn-server-track' ); ?>
			<div class="vpn-server-field">
				<label class="vpn-server-label" for="vpn-server-storage"><?php echo esc_html__( 'Storage folder', 'vpn-server' ); ?></label>
				<div class="vpn-server-control">
					<div class="vpn-server-line">
						<input type="text" id="vpn-server-storage" name="storage" class="code vpn-server-wide" spellcheck="false" value="<?php echo esc_attr( $settings['storage'] ); ?>">
						<button type="submit" class="button button-primary" data-vpn-server-confirm="<?php echo esc_attr__( 'Change the storage folder? Queued messages are not moved, and the folder that the plugin created is deleted.', 'vpn-server' ); ?>"><?php echo esc_html__( 'Change Folder', 'vpn-server' ); ?></button>
					</div>
					<p class="description"><?php echo esc_html__( 'Absolute path of the message queues. A folder outside the web root is the safest choice. Queued messages are not moved when the folder changes, they expire within 60 seconds.', 'vpn-server' ); ?></p>
				</div>
			</div>
			</form>
			<?php if ( null === $location ) : ?>
				<p class="vpn-server-lead"><?php echo esc_html__( 'The storage folder is outside the uploads folder. Make sure the web server does not serve it.', 'vpn-server' ); ?></p>
			<?php else : ?>
				<p class="vpn-server-lead"><?php echo esc_html__( 'The storage folder is inside the uploads folder. Apache and IIS are protected by the .htaccess and web.config files that the plugin writes there. On nginx add this rule to the server block and reload nginx:', 'vpn-server' ); ?></p>
				<pre class="vpn-server-snippet"><code><?php echo esc_html( 'location ^~ ' . $location . ' { deny all; return 404; }' ); ?></code></pre>
			<?php endif; ?>
		</section>
		<?php
	}

	/**
	 * Optional mu-plugin.
	 */
	private static function render_fast_route(): void {
		$installed = Fast_Route::is_installed();
		$current   = $installed && Fast_Route::is_current();
		if ( ! $installed ) {
			$status = array( 'warning', __( 'Not installed', 'vpn-server' ) );
		} elseif ( $current ) {
			$status = array( 'ok', __( 'Installed', 'vpn-server' ) );
		} else {
			$status = array( 'error', __( 'Installed for another version or location of this plugin, install it again', 'vpn-server' ) );
		}
		?>
		<section class="vpn-server-card">
			<div class="vpn-server-card-head">
				<h2><?php echo esc_html__( 'Fast route', 'vpn-server' ); ?></h2>
				<span class="vpn-server-pill vpn-server-pill-<?php echo esc_attr( $status[0] ); ?>"><?php echo esc_html( $status[1] ); ?></span>
			</div>
			<p class="vpn-server-lead"><?php echo esc_html__( 'Clients poll the server several times per second. By default a client request is answered right after all plugins are loaded, before the theme. The fast route adds a small must-use plugin that answers it before the other plugins load, which helps on sites with many plugins. Other requests are not affected. It is removed when this plugin is deactivated.', 'vpn-server' ); ?></p>
			<?php if ( ! self::can_manage_network_files() ) : ?>
				<p class="vpn-server-lead"><?php echo esc_html__( 'The fast route serves the whole network, only a super admin can install or remove it.', 'vpn-server' ); ?></p>
			<?php else : ?>
				<?php self::form_start( $current ? 'fast_route_off' : 'fast_route_on' ); ?>
				<button type="submit" class="button"><?php echo esc_html( $current ? __( 'Remove fast route', 'vpn-server' ) : __( 'Install fast route', 'vpn-server' ) ); ?></button>
				</form>
			<?php endif; ?>
		</section>
		<?php
	}

	/**
	 * Environment checks and the loopback self test.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function render_checks( array $settings ): void {
		$checks = self::checks( $settings );
		$test   = get_transient( self::SELF_TEST );
		$names  = array(
			'ok'      => __( 'OK', 'vpn-server' ),
			'warning' => __( 'Warning', 'vpn-server' ),
			'error'   => __( 'Error', 'vpn-server' ),
		);
		?>
		<section class="vpn-server-card">
			<div class="vpn-server-card-head">
				<h2><?php echo esc_html__( 'Checks', 'vpn-server' ); ?></h2>
				<?php self::form_start( 'self_test' ); ?>
				<button type="submit" class="button"><?php echo esc_html__( 'Run self test', 'vpn-server' ); ?></button>
				</form>
			</div>
			<ul class="vpn-server-checks">
				<?php foreach ( array_merge( $checks, is_array( $test ) ? $test : array() ) as $check ) : ?>
					<li>
						<span class="vpn-server-pill vpn-server-pill-<?php echo esc_attr( $check[0] ); ?>"><?php echo esc_html( $names[ $check[0] ] ?? $names['error'] ); ?></span>
						<span><?php echo esc_html( $check[1] ); ?></span>
					</li>
				<?php endforeach; ?>
			</ul>
			<p class="vpn-server-lead"><?php echo esc_html__( 'The self test sends requests from this server to the Server URL and to the storage folder, the same way a client and a visitor would.', 'vpn-server' ); ?></p>
		</section>
		<?php
	}

	/**
	 * Checks that need no request.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 * @return array<int, array{0: string, 1: string}> Level and message.
	 */
	private static function checks( array $settings ): array {
		$checks   = array();
		$checks[] = PHP_INT_SIZE >= 8
			? array( 'ok', __( 'PHP is 64-bit.', 'vpn-server' ) )
			: array( 'error', __( 'PHP is 32-bit. The VPN server requires 64-bit PHP.', 'vpn-server' ) );
		$checks[] = '' !== (string) get_option( 'permalink_structure' )
			? array( 'ok', __( 'Pretty permalinks are enabled.', 'vpn-server' ) )
			: array( 'error', __( 'Plain permalinks are used. The Server URL works only with pretty permalinks: choose another structure in Settings, Permalinks.', 'vpn-server' ) );
		$checks[] = 'https' === wp_parse_url( home_url(), PHP_URL_SCHEME )
			? array( 'ok', __( 'The site uses HTTPS.', 'vpn-server' ) )
			: array( 'warning', __( 'The site does not use HTTPS. The traffic stays encrypted, but HTTPS also hides addresses and timing from the network.', 'vpn-server' ) );
		if ( ini_get( 'zlib.output_compression' ) ) {
			$checks[] = array( 'warning', __( 'zlib.output_compression is enabled in PHP. Disable it for this site if the clients report damaged responses.', 'vpn-server' ) );
		}
		if ( url_to_postid( Settings::endpoint_url( $settings ) ) > 0 ) {
			$checks[] = array( 'warning', __( 'A page or post uses the same address as the Server URL. Visitors get the VPN server instead of that content, choose another path.', 'vpn-server' ) );
		}
		if ( array() === $settings['access_keys'] ) {
			$checks[] = array( 'warning', __( 'No client is configured yet.', 'vpn-server' ) );
		}
		$storage = (string) $settings['storage'];
		if ( '' === $storage ) {
			$checks[] = array( 'error', __( 'No storage folder is set.', 'vpn-server' ) );
		} elseif ( is_dir( $storage ) ) {
			$checks[] = wp_is_writable( $storage )
				? array( 'ok', __( 'The storage folder is writable.', 'vpn-server' ) )
				: array( 'error', __( 'The storage folder is not writable by PHP.', 'vpn-server' ) );
		} else {
			$checks[] = array( 'ok', __( 'The storage folder is created with the first client request.', 'vpn-server' ) );
		}
		return $checks;
	}

	// ---------------------------------------------------------------------
	// Actions
	// ---------------------------------------------------------------------

	/**
	 * Capability and nonce check of an admin-post action.
	 *
	 * @param string $action Action without the vpn_server_ prefix.
	 */
	private static function verify( string $action ): void {
		if ( ! current_user_can( self::CAPABILITY ) ) {
			wp_die( esc_html__( 'Sorry, you are not allowed to manage VPN Server.', 'vpn-server' ), '', array( 'response' => 403 ) );
		}
		check_admin_referer( 'vpn_server_' . $action );
	}

	/**
	 * Returns to the screen with a result code.
	 *
	 * @param string $notice Key of render_notice().
	 * @return never
	 */
	private static function done( string $notice ) {
		wp_safe_redirect( add_query_arg( self::NOTICE, $notice, self::page_url() ) );
		exit;
	}

	/**
	 * Text of a posted field, unslashed and sanitized.
	 *
	 * @param string $name Field name.
	 */
	private static function posted( string $name ): string {
		// Nonces are checked in verify() before any field is read.
		// phpcs:ignore WordPress.Security.NonceVerification.Missing
		return isset( $_POST[ $name ] ) && is_string( $_POST[ $name ] ) ? sanitize_text_field( wp_unslash( $_POST[ $name ] ) ) : '';
	}

	/**
	 * Saves the endpoint, the status page and the server secret together.
	 */
	public static function handle_save(): void {
		self::verify( 'save' );
		$settings = Settings::get();

		$endpoint = Settings::sanitize_endpoint( self::posted( 'endpoint' ) );
		$first    = explode( '/', $endpoint )[0];
		if ( '' === $endpoint || in_array( $first, self::RESERVED_ENDPOINTS, true ) ) {
			self::done( 'bad_endpoint' );
		}

		// The exact bytes of the secret enter the key derivation, chapter 3.2. Only printable ASCII
		// without spaces is accepted, so a validated value never needs sanitizing.
		// phpcs:ignore WordPress.Security.NonceVerification.Missing, WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- validated by the pattern below.
		$secret = isset( $_POST['secret'] ) && is_string( $_POST['secret'] ) ? trim( wp_unslash( $_POST['secret'] ) ) : '';
		if ( 1 !== preg_match( '/^[\x21-\x7e]{16,256}$/', $secret ) ) {
			self::done( 'bad_secret' );
		}

		$reconnect               = $endpoint !== $settings['endpoint'] || $secret !== $settings['secret'];
		$settings['endpoint']    = $endpoint;
		$settings['secret']      = $secret;
		$settings['status_page'] = '1' === self::posted( 'status_page' );
		Settings::save( $settings );
		delete_transient( self::SELF_TEST );
		self::done( $reconnect ? 'reconnect' : 'saved' );
	}

	/**
	 * Saves the public client download links without changing the connection or clients.
	 */
	public static function handle_downloads(): void {
		self::verify( 'downloads' );
		$settings  = Settings::get();
		$downloads = array();
		// phpcs:ignore WordPress.Security.NonceVerification.Missing, WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- nonce checked above, every entry sanitized below.
		$posted = isset( $_POST['client_downloads'] ) && is_array( $_POST['client_downloads'] ) ? wp_unslash( $_POST['client_downloads'] ) : array();
		foreach ( $posted as $entry ) {
			if ( ! is_array( $entry ) ) {
				self::done( 'bad_download_url' );
			}
			// An empty row, the spare row without JavaScript or a cleared one, is not an entry.
			$name = isset( $entry['name'] ) && is_string( $entry['name'] ) ? trim( $entry['name'] ) : '';
			$url  = isset( $entry['url'] ) && is_string( $entry['url'] ) ? trim( $entry['url'] ) : '';
			if ( '' === $name && '' === $url ) {
				continue;
			}
			$download = Settings::sanitize_download( $entry );
			if ( null === $download || count( $downloads ) >= Settings::MAX_CLIENT_DOWNLOADS ) {
				self::done( 'bad_download_url' );
			}
			$downloads[] = $download;
		}
		$settings['client_downloads'] = $downloads;
		Settings::save( $settings );
		self::done( 'saved' );
	}

	/**
	 * Moves the storage folder.
	 */
	public static function handle_storage(): void {
		self::verify( 'storage' );
		$settings = Settings::get();
		$storage  = untrailingslashit( wp_normalize_path( self::posted( 'storage' ) ) );
		if ( $storage === $settings['storage'] ) {
			self::done( 'unchanged' );
		}
		if ( ! self::is_allowed_storage( $storage, $settings ) ) {
			self::done( 'bad_storage' );
		}
		// On multisite only a super admin may place the queues outside the uploads folder.
		if ( is_multisite() && ! is_super_admin() && null === self::storage_url( array( 'storage' => $storage ) ) ) {
			self::done( 'network_only' );
		}
		require_once __DIR__ . '/server-core.php';
		$disk = new \PhpVpn\DiskStorage( $storage );
		if ( ! $disk->isUsable() || ! wp_is_writable( $storage ) ) {
			self::done( 'bad_storage' );
		}
		// isUsable() writes the guards only into a folder it creates itself.
		$disk->writeGuards();
		if ( Settings::is_managed_storage( $settings ) ) {
			self::delete_folder( $settings['storage'] );
		}
		$settings['storage']         = $storage;
		$settings['storage_managed'] = false;
		Settings::save( $settings );
		delete_transient( self::SELF_TEST );
		self::done( 'storage' );
	}

	/**
	 * Adds a client with the chosen or the next free address and its default access key.
	 */
	public static function handle_add_client(): void {
		self::verify( 'add_client' );
		require_once __DIR__ . '/server-core.php';
		$settings = Settings::get();
		$free     = self::free_addresses( $settings );
		$address  = self::posted( 'address' );
		if ( '' === $address ) {
			if ( array() === $free ) {
				self::done( 'no_address' );
			}
			$address = $free[0];
		} elseif ( ! in_array( $address, $free, true ) ) {
			self::done( 'bad_address' );
		}
		$settings['access_keys'][ $address ] = Settings::default_access_key( $address );
		Settings::save( $settings );
		self::done( 'client_added' );
	}

	/**
	 * Sets the default access key of a client.
	 */
	public static function handle_default_key(): void {
		self::verify( 'default_key' );
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		$settings['access_keys'][ $address ] = Settings::default_access_key( $address );
		Settings::save( $settings );
		self::done( 'default_key' );
	}

	/**
	 * Saves an access key entered for a client, for example the key of a client moved from another server.
	 */
	public static function handle_set_key(): void {
		self::verify( 'set_key' );
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		$key = strtolower( self::posted( 'access_key' ) );
		if ( 1 !== preg_match( '/^[0-9a-f]{64}$/', $key ) ) {
			self::done( 'bad_key' );
		}
		if ( $key === $settings['access_keys'][ $address ] ) {
			self::done( 'unchanged' );
		}
		// Whoever knows an access key can sign requests of every address that uses it.
		foreach ( $settings['access_keys'] as $other => $other_key ) {
			if ( (string) $other !== $address && $other_key === $key ) {
				self::done( 'duplicate_key' );
			}
		}
		$settings['access_keys'][ $address ] = $key;
		Settings::save( $settings );
		self::done( 'key_saved' );
	}

	/**
	 * Generates a new access key for a client.
	 */
	public static function handle_client_key(): void {
		self::verify( 'client_key' );
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		$settings['access_keys'][ $address ] = Settings::random_hex( 32 );
		Settings::save( $settings );
		self::done( 'client_key' );
	}

	/**
	 * Removes a client.
	 */
	public static function handle_remove_client(): void {
		self::verify( 'remove_client' );
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		unset( $settings['access_keys'][ $address ] );
		Settings::save( $settings );
		self::done( 'client_removed' );
	}

	/**
	 * Sends config.json for one client: the masterSetup part of the PHPVPN client configuration.
	 */
	public static function handle_client_config(): void {
		self::verify( 'client_config' );
		require_once __DIR__ . '/server-core.php';
		$settings = Settings::get();
		$address  = self::posted( 'address' );
		if ( ! isset( $settings['access_keys'][ $address ] ) ) {
			self::done( 'unknown_client' );
		}
		$config = array(
			'masterSetup'            => array(
				'serverSecret'                   => $settings['secret'],
				'serverUrl'                      => Settings::endpoint_url( $settings ),
				'httpHeaders'                    => self::CLIENT_HTTP_HEADERS,
				'trustAnyTlsCertificate'         => false,
				'vpnCidr'                        => \PhpVpn\VPN_ADDRESS_PREFIX . '0/24',
				'virtualIp'                      => $address,
				'accessKey'                      => $settings['access_keys'][ $address ],
				'useDefaultServerSecretInWizard' => false,
				'autoDetectIpAddressInWizard'    => false,
				'useDefaultAccessKeyInWizard'    => false,
			),
			'communicationContracts' => new \stdClass(),
		);
		nocache_headers();
		header( 'Content-Disposition: attachment; filename="phpvpn-' . str_replace( '.', '-', $address ) . '.json"' );
		wp_send_json( $config, 200, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES );
	}

	/**
	 * Installs the fast route.
	 */
	public static function handle_fast_route_on(): void {
		self::verify( 'fast_route_on' );
		if ( ! self::can_manage_network_files() ) {
			self::done( 'network_only' );
		}
		self::done( true === Fast_Route::install() ? 'fast_route_on' : 'fast_route_failed' );
	}

	/**
	 * Removes the fast route.
	 */
	public static function handle_fast_route_off(): void {
		self::verify( 'fast_route_off' );
		if ( ! self::can_manage_network_files() ) {
			self::done( 'network_only' );
		}
		Fast_Route::uninstall();
		self::done( 'fast_route_off' );
	}

	/**
	 * Loopback requests to the endpoint and to the storage folder. The result is kept for an hour.
	 */
	public static function handle_self_test(): void {
		self::verify( 'self_test' );
		require_once __DIR__ . '/server-core.php';
		$settings = Settings::get();
		$results  = array();
		$url      = Settings::endpoint_url( $settings );

		// An empty JSON body must reach the VPN server, which answers 400 bad_request with JSON.
		$response = wp_remote_post(
			add_query_arg( 'action', 'health', $url ),
			array(
				'timeout'   => 10,
				'headers'   => array( 'Content-Type' => 'application/json' ),
				'body'      => '{}',
				// Same rule as the loopback test of Site Health, staging sites often use own certificates.
				'sslverify' => apply_filters( 'https_local_ssl_verify', false ), // phpcs:ignore WordPress.NamingConventions.PrefixAllGlobals.NonPrefixedHooknameFound -- core filter.
			)
		);
		$body = is_wp_error( $response ) ? '' : (string) wp_remote_retrieve_body( $response );
		if ( is_wp_error( $response ) ) {
			/* translators: %s: error message. */
			$results[] = array( 'error', sprintf( __( 'The Server URL cannot be reached from this server: %s. A server with a single PHP worker cannot answer its own request, test the Server URL from a client then.', 'vpn-server' ), $response->get_error_message() ) );
		} elseif ( str_starts_with( $body, '{"type":"error"' ) ) {
			$results[] = array( 'ok', __( 'The Server URL answers protocol requests.', 'vpn-server' ) );
		} else {
			/* translators: %d: HTTP status code. */
			$results[] = array( 'error', sprintf( __( 'The Server URL returned HTTP %d without a protocol answer. Check the permalinks and the web server rewrite rules.', 'vpn-server' ), (int) wp_remote_retrieve_response_code( $response ) ) );
		}

		$results[] = self::probe_storage( $settings );
		set_transient( self::SELF_TEST, $results, HOUR_IN_SECONDS );
		self::done( 'self_test' );
	}

	/**
	 * Writes a random file into the storage folder and requests it over HTTP.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 * @return array{0: string, 1: string} Level and message.
	 */
	private static function probe_storage( array $settings ): array {
		$location = self::storage_url( $settings );
		if ( null === $location ) {
			return array( 'ok', __( 'The storage folder is outside the uploads folder, no web request was made.', 'vpn-server' ) );
		}
		$storage = new \PhpVpn\DiskStorage( $settings['storage'] );
		if ( ! $storage->isUsable() ) {
			return array( 'error', __( 'The storage folder cannot be created.', 'vpn-server' ) );
		}
		require_once ABSPATH . 'wp-admin/includes/file.php';
		if ( 'direct' !== get_filesystem_method() || ! WP_Filesystem() ) {
			return array( 'warning', __( 'The storage protection was not tested, WordPress cannot write files directly.', 'vpn-server' ) );
		}
		global $wp_filesystem;
		$name    = 'vpn-server-probe-' . Settings::random_hex( 8 ) . '.txt';
		$content = Settings::random_hex( 16 );
		$path    = trailingslashit( $settings['storage'] ) . $name;
		if ( ! $wp_filesystem->put_contents( $path, $content, FS_CHMOD_FILE ) ) {
			return array( 'error', __( 'The storage folder is not writable.', 'vpn-server' ) );
		}
		$response = wp_remote_get(
			$location . $name,
			array(
				'timeout'   => 10,
				'sslverify' => apply_filters( 'https_local_ssl_verify', false ), // phpcs:ignore WordPress.NamingConventions.PrefixAllGlobals.NonPrefixedHooknameFound -- core filter.
			)
		);
		$wp_filesystem->delete( $path );
		if ( ! is_wp_error( $response ) && trim( (string) wp_remote_retrieve_body( $response ) ) === $content ) {
			return array( 'error', __( 'The storage folder can be downloaded from the web. Add the web server rule shown under Storage protection, or move the folder outside the web root.', 'vpn-server' ) );
		}
		return array( 'ok', __( 'The storage folder cannot be downloaded from the web.', 'vpn-server' ) );
	}

	// ---------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------

	/**
	 * True for a dedicated storage folder. The folder receives deny-all .htaccess and web.config
	 * files, so it must not be the site root, a WordPress folder, an ancestor of the site, or
	 * inside the code folders. The current managed folder cannot contain the new one, it is deleted.
	 *
	 * @param string               $storage  Normalized absolute path without a trailing slash.
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function is_allowed_storage( string $storage, array $settings ): bool {
		// path_is_absolute() rejects normalized Windows paths such as D:/data, the path here is
		// already normalized: a drive letter, a root slash or a UNC share (//server).
		if ( 1 !== preg_match( '#^([a-zA-Z]:/|/)#', $storage ) || str_contains( $storage, '..' ) ) {
			return false;
		}
		$fold    = static fn( string $path ): string => '\\' === DIRECTORY_SEPARATOR ? strtolower( $path ) : $path;
		$path    = $fold( trailingslashit( $storage ) );
		$uploads = wp_upload_dir( null, false );
		$exact   = array( ABSPATH, WP_CONTENT_DIR, (string) $uploads['basedir'] );
		$inside  = array( WP_PLUGIN_DIR, WPMU_PLUGIN_DIR, get_theme_root(), ABSPATH . 'wp-admin', ABSPATH . WPINC );
		if ( Settings::is_managed_storage( $settings ) ) {
			$inside[] = (string) $settings['storage'];
		}
		foreach ( array_merge( $exact, $inside ) as $folder ) {
			$folder = $fold( trailingslashit( wp_normalize_path( $folder ) ) );
			// The folder itself, or an ancestor of it.
			if ( str_starts_with( $folder, $path ) ) {
				return false;
			}
		}
		foreach ( $inside as $folder ) {
			if ( str_starts_with( $path, $fold( trailingslashit( wp_normalize_path( $folder ) ) ) ) ) {
				return false;
			}
		}
		return true;
	}

	/**
	 * Deletes a storage folder the plugin created, used when the administrator moves the queues.
	 *
	 * @param string $folder Absolute path of the managed folder.
	 */
	private static function delete_folder( string $folder ): void {
		require_once ABSPATH . 'wp-admin/includes/file.php';
		if ( 'direct' === get_filesystem_method() && WP_Filesystem() ) {
			global $wp_filesystem;
			$wp_filesystem->delete( $folder, true );
		}
	}

	/**
	 * True when the current user may change things shared by the whole network on multisite.
	 */
	private static function can_manage_network_files(): bool {
		return ! is_multisite() || is_super_admin();
	}

	/**
	 * Client addresses that are still free, in numeric order.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 * @return string[]
	 */
	private static function free_addresses( array $settings ): array {
		require_once __DIR__ . '/server-core.php';
		$free = array();
		for ( $host = \PhpVpn\VPN_HOST_MIN; $host <= \PhpVpn\VPN_HOST_MAX; $host++ ) {
			$address = \PhpVpn\VPN_ADDRESS_PREFIX . $host;
			if ( ! isset( $settings['access_keys'][ $address ] ) ) {
				$free[] = $address;
			}
		}
		return $free;
	}

	/**
	 * URL of the storage folder with a trailing slash, null when it is outside the uploads folder.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function storage_url( array $settings ): ?string {
		$uploads = wp_upload_dir( null, false );
		$storage = wp_normalize_path( (string) $settings['storage'] );
		$base    = trailingslashit( wp_normalize_path( (string) $uploads['basedir'] ) );
		if ( '' === $storage || ! empty( $uploads['error'] ) || ! str_starts_with( trailingslashit( $storage ), $base ) ) {
			return null;
		}
		return trailingslashit( (string) $uploads['baseurl'] ) . substr( trailingslashit( $storage ), strlen( $base ) );
	}

	/**
	 * Path part of the storage URL for a web server rule, null when it is outside the uploads folder.
	 *
	 * @param array<string, mixed> $settings Current settings.
	 */
	private static function storage_url_path( array $settings ): ?string {
		$url  = self::storage_url( $settings );
		$path = null === $url ? null : wp_parse_url( $url, PHP_URL_PATH );
		return is_string( $path ) ? $path : null;
	}
}
