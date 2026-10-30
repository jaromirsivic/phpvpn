/**
 * Settings, VPN Server: copy, open, generate and edit buttons, the rows of the client downloads
 * table, and confirmations of actions that disconnect clients. Without JavaScript the fields stay
 * editable and every form still works.
 */
( function () {
	'use strict';

	var endpoint = document.getElementById( 'vpn-server-endpoint' );
	var downloads = document.getElementById( 'vpn-server-downloads' );
	// Rows added by Add Download get indexes after every row printed by the server.
	var nextDownload = downloads ? downloads.rows.length : 0;

	function changed( field ) {
		return !! field && field.value !== field.defaultValue;
	}

	function flash( button ) {
		var label = button.textContent;
		button.textContent = window.vpnServerAdmin.copied;
		window.setTimeout( function () {
			button.textContent = label;
		}, 1500 );
	}

	// navigator.clipboard needs HTTPS or localhost, execCommand covers plain HTTP admin screens.
	function copyText( text, button ) {
		var fallback = function () {
			var area = document.createElement( 'textarea' );
			area.value = text;
			area.setAttribute( 'readonly', '' );
			area.style.position = 'fixed';
			area.style.opacity = '0';
			document.body.appendChild( area );
			area.select();
			document.execCommand( 'copy' );
			document.body.removeChild( area );
			flash( button );
		};
		if ( navigator.clipboard && window.isSecureContext ) {
			navigator.clipboard.writeText( text ).then( function () {
				flash( button );
			}, fallback );
		} else {
			fallback();
		}
	}

	function randomHex( bytes ) {
		return Array.prototype.map.call( crypto.getRandomValues( new Uint8Array( bytes ) ), function ( byte ) {
			return ( '0' + byte.toString( 16 ) ).slice( -2 );
		} ).join( '' );
	}

	function setEditing( field, editing ) {
		field.closest( 'tr' ).classList.toggle( 'is-editing', editing );
		field.readOnly = ! editing;
		if ( editing ) {
			field.focus();
			field.select();
		} else {
			field.value = field.defaultValue;
		}
	}

	function selectChanged( field ) {
		var initial = Array.prototype.filter.call( field.options, function ( option ) {
			return option.defaultSelected;
		} )[ 0 ] || field.options[ 0 ];
		return field.selectedIndex !== initial.index;
	}

	// Save Changes and Change Folder stay disabled until a field of their form differs from the saved
	// value or a client download row was added, removed or moved. Without JavaScript they stay enabled.
	function trackChanges( form ) {
		var dirty = form.hasAttribute( 'data-vpn-server-changed' ) || Array.prototype.some.call( form.elements, function ( field ) {
			if ( 'checkbox' === field.type ) {
				return field.checked !== field.defaultChecked;
			}
			if ( 'select-one' === field.type ) {
				return selectChanged( field );
			}
			return ( 'text' === field.type || 'url' === field.type ) && changed( field );
		} );
		form.querySelectorAll( '[type="submit"]' ).forEach( function ( button ) {
			button.disabled = ! dirty;
		} );
	}

	function trackAll() {
		document.querySelectorAll( 'form.vpn-server-track' ).forEach( trackChanges );
	}

	[ 'input', 'change' ].forEach( function ( type ) {
		document.addEventListener( type, function ( event ) {
			var form = event.target.form;
			if ( form && form.classList.contains( 'vpn-server-track' ) ) {
				trackChanges( form );
			}
		} );
	} );
	// The browser may restore typed values on reload or from the back-forward cache.
	window.addEventListener( 'pageshow', trackAll );
	trackAll();

	// Client downloads: the spare empty row printed for browsers without JavaScript is hidden and skipped.
	function downloadRows() {
		return Array.prototype.filter.call( downloads.rows, function ( row ) {
			return ! row.classList.contains( 'hide-if-js' );
		} );
	}

	function updateDownloads( form ) {
		var rows = downloadRows();
		document.querySelector( '[data-vpn-server-add-download]' ).disabled =
			rows.length >= Number( downloads.getAttribute( 'data-vpn-server-max' ) );
		rows.forEach( function ( row, index ) {
			row.querySelector( '[data-vpn-server-move-download="-1"]' ).disabled = 0 === index;
			row.querySelector( '[data-vpn-server-move-download="1"]' ).disabled = rows.length - 1 === index;
		} );
		if ( form ) {
			form.setAttribute( 'data-vpn-server-changed', '' );
			trackChanges( form );
		}
	}

	if ( downloads ) {
		updateDownloads( null );
	}

	// The secret card turns red for the default, an all zero or a short secret, as Settings::secret_weakness().
	var secret = document.getElementById( 'vpn-server-secret' );
	if ( secret ) {
		secret.addEventListener( 'input', function () {
			var value = secret.value;
			var weakness = '';
			if ( value === secret.getAttribute( 'data-vpn-server-default-secret' ) || /^0*$/.test( value ) ) {
				weakness = 'default';
			} else if ( value.length < Number( secret.getAttribute( 'data-vpn-server-secure-length' ) ) ) {
				weakness = 'short';
			}
			var card = secret.closest( '.vpn-server-secret-card' );
			card.classList.toggle( 'is-insecure', '' !== weakness );
			card.classList.toggle( 'is-secure', '' === weakness );
			card.querySelectorAll( '.vpn-server-secret-state' ).forEach( function ( state ) {
				state.hidden = state.getAttribute( 'data-weakness' ) !== weakness;
			} );
		} );
	}

	// Copy and Open use the saved Server URL, an edited path is not live before it is saved.
	if ( endpoint ) {
		endpoint.addEventListener( 'input', function () {
			var dirty = changed( endpoint );
			document.querySelectorAll( '[data-vpn-server-saved-url]' ).forEach( function ( element ) {
				element.classList.toggle( 'disabled', dirty );
				element.setAttribute( 'aria-disabled', dirty ? 'true' : 'false' );
			} );
			document.querySelector( '.vpn-server-unsaved' ).hidden = ! dirty;
		} );
	}

	document.addEventListener( 'click', function ( event ) {
		var target = event.target;
		var element;

		if ( target.closest( '[data-vpn-server-saved-url]' ) && changed( endpoint ) ) {
			event.preventDefault();
			return;
		}

		element = target.closest( '[data-vpn-server-copy-text]' );
		if ( element ) {
			copyText( element.getAttribute( 'data-vpn-server-copy-text' ), element );
			return;
		}

		element = target.closest( '[data-vpn-server-copy]' );
		if ( element ) {
			copyText( document.getElementById( element.getAttribute( 'data-vpn-server-copy' ) ).value, element );
			return;
		}

		element = target.closest( '[data-vpn-server-generate]' );
		if ( element ) {
			var secret = document.getElementById( element.getAttribute( 'data-vpn-server-generate' ) );
			secret.value = randomHex( 32 );
			secret.dispatchEvent( new Event( 'input', { bubbles: true } ) );
			return;
		}

		element = target.closest( '[data-vpn-server-edit]' );
		if ( element ) {
			setEditing( document.getElementById( element.getAttribute( 'data-vpn-server-edit' ) ), true );
			return;
		}

		element = target.closest( '[data-vpn-server-cancel]' );
		if ( element ) {
			setEditing( document.getElementById( element.getAttribute( 'data-vpn-server-cancel' ) ), false );
			return;
		}

		element = target.closest( '[data-vpn-server-add-download]' );
		if ( element ) {
			var template = document.getElementById( 'vpn-server-download-template' );
			downloads.insertAdjacentHTML( 'beforeend', template.innerHTML.replace( /__index__/g, String( nextDownload++ ) ) );
			updateDownloads( element.form );
			downloads.lastElementChild.querySelector( 'input' ).focus();
			return;
		}

		element = target.closest( '[data-vpn-server-remove-download]' );
		if ( element ) {
			var form = element.form;
			element.closest( 'tr' ).remove();
			updateDownloads( form );
			return;
		}

		element = target.closest( '[data-vpn-server-move-download]' );
		if ( element ) {
			var rows = downloadRows();
			var row = element.closest( 'tr' );
			var other = rows[ rows.indexOf( row ) + Number( element.getAttribute( 'data-vpn-server-move-download' ) ) ];
			if ( other ) {
				// The posted list keeps the order of the rows in the document.
				downloads.insertBefore( row, Number( element.getAttribute( 'data-vpn-server-move-download' ) ) < 0 ? other : other.nextSibling );
				updateDownloads( element.form );
				element.focus();
			}
			return;
		}

		element = target.closest( '[data-vpn-server-confirm]' );
		if ( element && ! window.confirm( element.getAttribute( 'data-vpn-server-confirm' ) ) ) {
			event.preventDefault();
		}
	} );

	document.addEventListener( 'keydown', function ( event ) {
		var field = event.target;
		if ( 'Escape' === event.key && field.classList && field.classList.contains( 'vpn-server-key' ) && ! field.readOnly ) {
			setEditing( field, false );
		}
	} );

	document.addEventListener( 'submit', function ( event ) {
		var form = event.target;

		var save = form.querySelector( '[data-vpn-server-confirm-change]' );
		var dirty;

		if ( form.classList.contains( 'vpn-server-edit' ) ) {
			// Enter in an access key field that is not being edited must not submit its form.
			var key = document.querySelector( '[form="' + form.id + '"]' );
			if ( key.readOnly ) {
				event.preventDefault();
				return;
			}
			dirty = changed( key );
		} else {
			// A changed Server URL or secret disconnects every client, the status page alone does not.
			dirty = changed( endpoint ) || changed( document.getElementById( 'vpn-server-secret' ) );
		}
		if ( save && dirty && ! window.confirm( save.getAttribute( 'data-vpn-server-confirm-change' ) ) ) {
			event.preventDefault();
		}
	} );
} )();
