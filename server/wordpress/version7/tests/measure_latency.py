"""Latency of empty data polls against one or more relay deployments.

Each argument is a deployment description JSON as used by tests-integration (baseUrl,
serverSecret, accessKeys, bufferDirectory). The script runs warm-up polls first, then reports
median, mean, 90th percentile and max of the measured polls, all over one keep-alive session.

    uv run --no-project python server/wordpress/version7/tests/measure_latency.py \
        --requests 200 standalone=work/test-server/server.json wordpress=work/wp-plugin-test/server.json
"""
import argparse
import pathlib
import statistics
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tests-integration'))

import requests  # noqa: E402
from phpvpn_client import Configuration, PhpVpnClient  # noqa: E402


def measure(path: pathlib.Path, count: int, warmup: int) -> list[float]:
    configuration = Configuration.load(path)
    with requests.Session() as session:
        client = PhpVpnClient(configuration, session=session)
        client.synchronize_clock()
        address = configuration.addresses[0]
        samples = []
        for index in range(warmup + count):
            started = time.perf_counter()
            response, _ = client.send_data(address)
            elapsed = (time.perf_counter() - started) * 1000.0
            if response.status_code != 200:
                raise SystemExit(f'{path}: data poll returned HTTP {response.status_code}: {response.text[:200]}')
            if index >= warmup:
                samples.append(elapsed)
        return samples


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    arguments.add_argument('--requests', type=int, default=200)
    arguments.add_argument('--warmup', type=int, default=20)
    arguments.add_argument('deployments', nargs='+', help='label=path/to/server.json')
    options = arguments.parse_args()

    print(f'{"deployment":<24}{"median":>10}{"mean":>10}{"p90":>10}{"max":>10}   ms, {options.requests} polls')
    for item in options.deployments:
        label, _, path = item.partition('=')
        samples = sorted(measure(pathlib.Path(path), options.requests, options.warmup))
        p90 = samples[int(len(samples) * 0.9) - 1]
        print(f'{label:<24}{statistics.median(samples):>10.1f}{statistics.fmean(samples):>10.1f}{p90:>10.1f}{samples[-1]:>10.1f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
