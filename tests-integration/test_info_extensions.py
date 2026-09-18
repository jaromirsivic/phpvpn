"""Full-auth publication, independent canonical vectors and requester-only privacy."""
import hashlib
import base64
import hmac
import json
import pytest
from phpvpn_client import PhpVpnClient

def test_golden_mask_and_extension():
    assert hashlib.sha256(("10.99.99.2_10.99.99.10_"+"01"*32+"_").encode()).hexdigest() == "0c7a3dd333644ac165ce31c512e2e473c32909310f79c8cb7c492d84db14e459"
    assert hashlib.sha256(b'{"includeMaskedCommunicationContracts":true}').hexdigest() == "225ecf0db7d2c44e191004a5afbbd9969fa7875097d1081d958f2d7d29f7ecf3"
    canonical = b"PHPVPN-AUTH-FULL\nPOST\ninfo\n10.99.99.2\n2026-09-13T12:00:00.000Z\n8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30\n225ecf0db7d2c44e191004a5afbbd9969fa7875097d1081d958f2d7d29f7ecf3"
    assert base64.urlsafe_b64encode(hmac.new(bytes(32), canonical, hashlib.sha256).digest()).rstrip(b"=") == b"NrLbf7RH9URds5iv6S382huu-Msw1zZpLMvjox8gcmQ"

def test_private_four_client_publication(client: PhpVpnClient):
    a,b,c,d=client.configuration.addresses[:4]
    for src, pairs in [(a,[(b,"a")]),(b,[(a,"a"),(c,"b")]),(c,[(d,"c")]),(d,[])]:
        response,_=client.extended_info(src,{"maskedCommunicationContracts":[{"peer":p,"hash":h*64} for p,h in pairs]})
        assert response.status_code==200
    response,rid=client.extended_info(a,{"includeMaskedCommunicationContracts":True})
    assert response.status_code==200
    info=client.decrypt_info(response.json(),rid)
    exact=client.decrypt_info_bytes(response.json(),rid)
    assert exact.startswith(b'{\n    "serverVersion":') and len(exact) <= 262144
    assert json.loads(exact) == info
    rows={row["address"]:row for row in info["clients"]}
    assert rows[a]["maskedCommunicationContracts"]==[]
    assert rows[b]["maskedCommunicationContracts"]==[{"peer":a,"hash":"a"*64}]
    assert rows[c]["maskedCommunicationContracts"]==[]
    assert rows[d]["maskedCommunicationContracts"]==[]
    assert "b"*64 not in json.dumps(info) and "c"*64 not in json.dumps(info)
    standard,_=client.info_plaintext()
    assert all(set(row)=={"address","lastSeen"} for row in standard["clients"])
    response,_=client.extended_info(b,{"maskedCommunicationContracts":[]})
    assert response.status_code==200
    response,rid=client.extended_info(a,{"includeMaskedCommunicationContracts":True})
    assert all(not row["maskedCommunicationContracts"] for row in client.decrypt_info(response.json(),rid)["clients"])

@pytest.mark.parametrize("field,value",[("includeMaskedCommunicationContracts",False),("src","10.99.99.3"),("timestamp","2000-01-01T00:00:00.000Z")])
def test_modified_signed_fields(client: PhpVpnClient,field,value):
    a=client.configuration.addresses[0]
    body,rid=client.build_info_full(a,{"includeMaskedCommunicationContracts":True})
    payload=json.loads(body);payload[field]=value
    response,_=client.info(body=json.dumps(payload).encode(),request_id=rid)
    assert response.status_code==401

def test_wrong_access_and_cross_domain(client: PhpVpnClient):
    a=client.configuration.addresses[0]
    body,rid=client.build_info_full(a,{"includeMaskedCommunicationContracts":False},access_key=bytes(32))
    assert client.info(body=body,request_id=rid)[0].status_code==401
    body,rid=client.build_info_full(a,{"includeMaskedCommunicationContracts":True})
    payload=json.loads(body)
    line,_,_=client.build_auth_full_line(a,b"",request_id=rid,timestamp=payload["timestamp"])
    payload["tag"]=json.loads(line)["tag"]
    assert client.info(body=json.dumps(payload).encode(),request_id=rid)[0].status_code==401

def test_replay_window_and_oversize(client: PhpVpnClient):
    a=client.configuration.addresses[0]
    body,rid=client.build_info_full(a,{"maskedCommunicationContracts":[]})
    for _ in range(2): assert client.info(body=body,request_id=rid)[0].status_code==200
    assert client.info(body=body.ljust(65536),request_id=rid)[0].status_code==200
    assert client.info(body=body.ljust(65537),request_id=rid)[0].status_code==413
