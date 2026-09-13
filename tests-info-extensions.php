<?php
namespace PhpVpn\Tests;
// Runs inside tests.php's harness. Expected canonical bytes are independent of the server helper.
use PhpVpn\{Authenticator, Base64Url, KeyDerivation, AesGcm, Validators, JsonWire};
function extendedInfoBody(Harness $h, string $src, array $extension): string {
    $id = Validators::newUuidV4();
    $time = $h->timestamp();
    $json = json_encode($extension, JSON_UNESCAPED_SLASHES);
    $canonical = "PHPVPN-AUTH-FULL\nPOST\ninfo\n$src\n$time\n$id\n" . hash('sha256', $json);
    return json_encode(array_merge(['type'=>'auth-full','src'=>$src,'timestamp'=>$time,'requestId'=>$id,
        'tag'=>Base64Url::encode(hash_hmac('sha256', $canonical, $h->fullKey($src), true))], $extension), JSON_UNESCAPED_SLASHES);
}
function extendedInfoPlaintext(Harness $h, string $body): array {
    $v = json_decode($body, true);
    $key = KeyDerivation::infoKey($h->config->serverSecret, Base64Url::decode($v['salt']), $v['requestId']);
    return json_decode((new AesGcm($key))->open(Base64Url::decode($v['iv']), Base64Url::decode($v['ciphertext']),
        Base64Url::decode($v['tag']), Authenticator::infoAad($v['requestId'], $v['salt'], $v['iv'])), true);
}
$runner->group('InfoExtensions');
$runner->run('Cross-language fixed canonical vectors', function(Runner $t): void {
    $t->same('0c7a3dd333644ac165ce31c512e2e473c32909310f79c8cb7c492d84db14e459',
        hash('sha256','10.99.99.2_10.99.99.10_'.str_repeat('01',32).'_'),'empty configured secret mask');
    $hash=hash('sha256','{"includeMaskedCommunicationContracts":true}');
    $t->same('225ecf0db7d2c44e191004a5afbbd9969fa7875097d1081d958f2d7d29f7ecf3',$hash,'extension canonical hash');
    $canonical=Authenticator::infoFullCanonical('10.99.99.2','2026-09-13T12:00:00.000Z','8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30',$hash);
    $t->same('NrLbf7RH9URds5iv6S382huu-Msw1zZpLMvjox8gcmQ',Base64Url::encode(hash_hmac('sha256',$canonical,str_repeat("\0",32),true)),'info-domain tag');
});
$runner->run('Stored snapshot validation and bounded temp cleanup', function(Runner $t): void {
    $h = new Harness();
    try {
        $storage=$h->storage();
        $t->ok($storage->publishMaskedContracts(Harness::CLIENT_A,[], $h->nowMs),'initial publication');
        $directory=$storage->clientDirectory(Harness::CLIENT_A);
        $path=$directory.'/info.json';
        foreach (['', '{"maskedCommunicationContracts":{}}', '{"maskedCommunicationContracts":[],"extra":true}',
                  '{"maskedCommunicationContracts":[],"maskedCommunicationContracts":[]}', str_repeat(' ',65537)] as $bad) {
            file_put_contents($path,$bad);touch($path,intdiv($h->nowMs,1000));
            $snapshot=$storage->readClientInfoSnapshot(Harness::CLIENT_A);
            $t->same([],$snapshot['contracts'],'invalid whole snapshot becomes empty');
            $t->same(intdiv($h->nowMs,1000)*1000,$snapshot['lastSeen'],'mtime retained');
        }
        $stale=$directory.'/.info-'.Validators::newUuidV4().'.tmp';
        $fresh=$directory.'/.info-'.Validators::newUuidV4().'.tmp';
        $unrelated=$directory.'/.info-not-a-uuid.tmp';
        foreach ([$stale,$fresh,$unrelated] as $file) file_put_contents($file,'test');
        touch($stale,intdiv($h->nowMs,1000)-301);
        touch($fresh,intdiv($h->nowMs,1000));
        touch($unrelated,intdiv($h->nowMs,1000)-301);
        $storage->sweep($h->nowMs,1000);
        $t->ok(!file_exists($stale),'expired owned temp removed');
        $t->ok(file_exists($fresh) && file_exists($unrelated) && file_exists($path),'fresh and unrelated files preserved');
    } finally {$h->cleanup();}
});
$runner->run('Authenticated publication, privacy and compatible touch', function(Runner $t): void {
    $h = new Harness();
    try {
        $contracts = [['peer'=>Harness::CLIENT_A,'hash'=>str_repeat('a',64)], ['peer'=>'10.99.99.4','hash'=>str_repeat('b',64)]];
        $body = extendedInfoBody($h, Harness::CLIENT_B, ['maskedCommunicationContracts'=>$contracts]);
        $r = $h->call('info','POST','application/json',$body);
        $t->same(200,$r->status,'publication');
        $plain = extendedInfoPlaintext($h,$r->body);
        $t->same(['address','lastSeen'],array_keys($plain['clients'][0]),'publish-only old shape');
        $path = $h->storage()->clientDirectory(Harness::CLIENT_B).'/info.json';
        $before = file_get_contents($path);
        $h->storage()->touchLastSeenBestEffort(Harness::CLIENT_B,$h->nowMs+40000);
        $t->same($before,file_get_contents($path),'touch preserves JSON');
        $r = $h->call('info','POST','application/json',extendedInfoBody($h,Harness::CLIENT_A,['includeMaskedCommunicationContracts'=>true]));
        $t->same(200,$r->status,'include');
        $plain = extendedInfoPlaintext($h,$r->body);
        $t->same([], $plain['clients'][0]['maskedCommunicationContracts'],'self private');
        $t->same([$contracts[0]],$plain['clients'][1]['maskedCommunicationContracts'],'only requesting peer');
        $t->ok(!str_contains(json_encode($plain),str_repeat('b',64)),'hidden hash absent');
        $r=$h->call('info','POST','application/json',extendedInfoBody($h,Harness::CLIENT_B,['maskedCommunicationContracts'=>[],'includeMaskedCommunicationContracts'=>false]));
        $t->same(200,$r->status,'immediate clear');
        $t->same('{"maskedCommunicationContracts":[]}',file_get_contents($path),'clear replaces');
        file_put_contents($path,'{"maskedCommunicationContracts": [broken');
        $t->same([],$h->storage()->readClientInfoSnapshot(Harness::CLIENT_B)['contracts'],'corruption not rescued');
    } finally { $h->cleanup(); }
});
$runner->run('Validation, signed presence and size boundaries', function(Runner $t): void {
    $h = new Harness();
    try {
        $body = extendedInfoBody($h,Harness::CLIENT_A,['includeMaskedCommunicationContracts'=>true]);
        foreach ([str_replace(':true',':false',$body), str_replace(Harness::CLIENT_A,Harness::CLIENT_B,$body)] as $bad)
            $t->same(401,$h->call('info','POST','application/json',$bad)->status,'tampering');
        foreach (['{"includeMaskedCommunicationContracts":true,"includeMaskedCommunicationContracts":false}',
            '{"includeMaskedCommunicationContracts":true,"includeMaskedCommunicationContra\\u0063ts":false}'] as $bad)
            $t->same(null,JsonWire::parseInfoObject($bad),'decoded duplicate');
        foreach ([null, new \stdClass(), [['peer'=>Harness::CLIENT_A,'hash'=>str_repeat('a',64)]],
            [['peer'=>Harness::CLIENT_B,'hash'=>str_repeat('A',64)]],
            [['peer'=>Harness::CLIENT_B,'hash'=>str_repeat('a',64)],['peer'=>Harness::CLIENT_B,'hash'=>str_repeat('a',64)]]] as $bad)
            $t->same(400,$h->call('info','POST','application/json',extendedInfoBody($h,Harness::CLIENT_A,['maskedCommunicationContracts'=>$bad]))->status,'bad contracts');
        $list=[];
        for($i=3;$i<=254;$i++) $list[]=['peer'=>'10.99.99.'.$i,'hash'=>str_repeat('a',64)];
        $max=extendedInfoBody($h,Harness::CLIENT_A,['maskedCommunicationContracts'=>$list]);
        $t->same(200,$h->call('info','POST','application/json',str_pad($max,65536))->status,'65536 and 252');
        $t->same(413,$h->call('info','POST','application/json',str_pad($max,65537))->status,'65537');
        $list[]=$list[0];
        $t->same(413,$h->call('info','POST','application/json',extendedInfoBody($h,Harness::CLIENT_A,['maskedCommunicationContracts'=>$list]))->status,'253');
        $semi=$h->authSemiBody();
        $t->same(400,$h->call('info','POST','application/json',str_pad($semi,4096))->status,'legacy 4096');
        $t->same(413,$h->call('info','POST','application/json',str_pad($semi,4097))->status,'legacy 4097');
    } finally {$h->cleanup();}
});
