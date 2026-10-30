/**
 * Status page of the VPN Server: key generator and Show HTTP Headers dialog.
 */
( function () {
	'use strict';

	function byId( id ) {
		return document.getElementById( id );
	}

	async function copy( field ) {
		field.select();
		try {
			if ( navigator.clipboard ) {
				await navigator.clipboard.writeText( field.value );
				return true;
			}
		} catch ( error ) {
			// Fall back when clipboard permission is unavailable.
		}
		try {
			field.select();
			return document.execCommand( 'copy' );
		} catch ( error ) {
			return false;
		}
	}

	byId( 'vpn-server-url' ).textContent = location.href;

	if ( byId( 'vpn-server-generate' ) ) {
		byId( 'vpn-server-generate' ).addEventListener( 'click', function () {
			var bytes = crypto.getRandomValues( new Uint8Array( 32 ) );
			byId( 'vpn-server-key' ).value = Array.prototype.map
				.call( bytes, function ( value ) {
					return value.toString( 16 ).padStart( 2, '0' );
				} )
				.join( '' );
			byId( 'vpn-server-row' ).hidden = false;
		} );

		byId( 'vpn-server-copy-key' ).addEventListener( 'click', async function () {
			if ( await copy( byId( 'vpn-server-key' ) ) ) {
				byId( 'vpn-server-key-dialog' ).showModal();
			}
		} );
	}

	byId( 'vpn-server-show' ).addEventListener( 'click', function () {
		byId( 'vpn-server-dialog' ).showModal();
		byId( 'vpn-server-headers' ).select();
	} );

	byId( 'vpn-server-copy-headers' ).addEventListener( 'click', function () {
		copy( byId( 'vpn-server-headers' ) );
	} );

	// Client select box: a file entry shows Download, a link entry Open in a new tab. The entry for
	// the visitor's system is preselected; phones, tablets and ChromeOS keep the first entry.
	var downloads = byId( 'vpn-server-downloads' );
	if ( downloads ) {
		var button = byId( 'vpn-server-download' );
		var update = function () {
			var option = downloads.selectedOptions[ 0 ];
			var link = 'link' === option.getAttribute( 'data-type' );
			button.href = option.value;
			button.textContent = link ? 'Open' : 'Download';
			if ( link ) {
				button.target = '_blank';
			} else {
				button.removeAttribute( 'target' );
			}
		};
		var platform = ( navigator.userAgentData && navigator.userAgentData.platform ) || navigator.userAgent;
		var system = '';
		if ( ! /Android|iPhone|iPad|iPod|CrOS|Chrome OS|Chromium OS/.test( platform ) && ! ( /Mac/i.test( platform ) && navigator.maxTouchPoints > 1 ) ) {
			if ( /Win/.test( platform ) ) {
				system = 'windows';
			} else if ( /Mac/i.test( platform ) ) {
				system = 'macos';
			} else if ( /Linux|X11/.test( platform ) ) {
				system = 'linux';
			}
		}
		Array.prototype.some.call( downloads.options, function ( option ) {
			if ( '' !== system && option.getAttribute( 'data-os' ) === system ) {
				downloads.selectedIndex = option.index;
				return true;
			}
			return false;
		} );
		update();
		downloads.addEventListener( 'change', update );
	}
} )();
