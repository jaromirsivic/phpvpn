/**
 * Status page of the VPN Server: key generator and Show HTTP Headers dialog.
 */
( function () {
	'use strict';

	function byId( id ) {
		return document.getElementById( id );
	}

	function copy( field ) {
		field.select();
		if ( navigator.clipboard ) {
			navigator.clipboard.writeText( field.value ).catch( function () {
				document.execCommand( 'copy' );
			} );
		} else {
			document.execCommand( 'copy' );
		}
	}

	byId( 'vpn-server-generate' ).addEventListener( 'click', function () {
		var bytes = crypto.getRandomValues( new Uint8Array( 32 ) );
		byId( 'vpn-server-key' ).value = Array.prototype.map
			.call( bytes, function ( value ) {
				return value.toString( 16 ).padStart( 2, '0' );
			} )
			.join( '' );
		byId( 'vpn-server-row' ).hidden = false;
	} );

	byId( 'vpn-server-copy-key' ).addEventListener( 'click', function () {
		copy( byId( 'vpn-server-key' ) );
	} );

	byId( 'vpn-server-show' ).addEventListener( 'click', function () {
		byId( 'vpn-server-dialog' ).showModal();
		byId( 'vpn-server-headers' ).select();
	} );

	byId( 'vpn-server-copy-headers' ).addEventListener( 'click', function () {
		copy( byId( 'vpn-server-headers' ) );
	} );
} )();
