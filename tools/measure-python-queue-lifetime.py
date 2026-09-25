#!/usr/bin/env python3
"""Measure server RSS with three unread responses and concurrent queue refill."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time

import psutil
import requests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests-integration'))
from phpvpn_client import Configuration, PhpVpnClient, make_record, parse_data_response  # noqa: E402

spec = importlib.util.spec_from_file_location('server_fixture', ROOT / 'tools/test-python-server.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def measure(profile):
    with runner.fixture(profile) as (config, url, process, temporary):
        cfg = Configuration.load(config)
        client = PhpVpnClient(cfg)
        client.synchronize_clock()
        monitored = psutil.Process(process.pid)
        stop = threading.Event()
        samples = []

        def sample():
            while not stop.is_set():
                samples.append(monitored.memory_info().rss)
                stop.wait(0.01)

        sampler = threading.Thread(target=sample, daemon=True)
        sampler.start()
        pending = []
        result = {'profile': profile, 'rssStartBytes': monitored.memory_info().rss}
        src, *destinations = cfg.addresses[:4]
        expected = {}
        queue_bytes = 0
        try:
            for dst in destinations:
                records = [make_record(src, dst, b'x' * 32768) for _ in range(16)]
                expected[dst] = [record.message_id for record in records] * 14
                queue_bytes += sum(len(record.line) for record in records) * 14
                for _ in range(14):
                    assert client.poll(src, records).accepted_message_ids == expected[dst][:16]
                body, request_id = client.build_data_body(dst)
                response = requests.post(url + '?action=data', data=body,
                                         headers={'Content-Type': 'application/x-ndjson'},
                                         stream=True, timeout=30)
                assert response.status_code == 200
                pending.append((dst, request_id, response))
                # The first response owns its claimed batches while this refill
                # creates a new, independent queue for the same destination.
                for _ in range(14):
                    assert client.poll(src, records).accepted_message_ids == expected[dst][:16]
            time.sleep(0.3)
            result['rssHeldAndRefilledBytes'] = monitored.memory_info().rss
            for dst, request_id, response in pending:
                received = parse_data_response(response.content)
                assert received.verify_mac(client.full_auth_key(dst), request_id)
                assert [record.message_id for record in received.records] == expected[dst]
                response.close()
                refilled = client.poll(dst)
                assert [record.message_id for record in refilled.records] == expected[dst]
                assert not client.poll(dst).records
            time.sleep(0.3)
            result['rssEndBytes'] = monitored.memory_info().rss
            result['wireBytesPerGeneration'] = queue_bytes
            result['unreadResponses'] = len(pending)
            if profile == 'python-memory':
                assert not (temporary / 'buffer').exists()
        finally:
            for _, _, response in pending:
                response.close()
            client.session.close()
            stop.set()
            sampler.join()
        result.update(rssPeakBytes=max(samples), samples=len(samples), sampleIntervalMs=10)
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = [measure(profile) for profile in ('python-disk', 'python-memory')]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(results, indent=2))
