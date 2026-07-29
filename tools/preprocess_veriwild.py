#!/usr/bin/env python3
"""Build deterministic PSI input sets from VERI-Wild annotations.

The expected default annotation layout is one record per line with at least:

    image_path vehicle_id camera_id

Columns can be changed with --vehicle-id-column and --camera-id-column.
The generated .bin files are raw concatenations of 16-byte SHA-256 prefixes.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
from pathlib import Path


DEFAULT_DOMAIN = "TVIS-v1|vehicle|"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert VERI-Wild vehicle identities into PSI block files."
    )
    parser.add_argument(
        "--annotations",
        required=True,
        type=Path,
        action="append",
        help="Annotation file; repeat to combine train/test identity lists.",
    )
    parser.add_argument("--server-out", type=Path, default=Path("data/processed/veriwild_server.bin"))
    parser.add_argument("--client-out", type=Path, default=Path("data/processed/veriwild_client.bin"))
    parser.add_argument("--metadata-out", type=Path, default=Path("data/processed/veriwild_metadata.json"))
    parser.add_argument("--server-size", type=int, default=32768)
    parser.add_argument("--client-size", type=int, default=256)
    parser.add_argument("--intersection", type=int, default=64)
    parser.add_argument("--seed", type=int, default=20260727)
    parser.add_argument("--domain", default=DEFAULT_DOMAIN)
    parser.add_argument(
        "--vehicle-id-column",
        type=int,
        default=1,
        help="Zero-based vehicle ID column (default: 1).",
    )
    parser.add_argument(
        "--vehicle-id-path-prefix",
        action="store_true",
        help="Use the first path component of the vehicle ID column.",
    )
    parser.add_argument(
        "--camera-id-column",
        type=int,
        default=2,
        help="Zero-based camera ID column; use -1 to ignore cameras (default: 2).",
    )
    parser.add_argument(
        "--client-camera",
        action="append",
        default=[],
        help="Restrict client vehicles to this camera ID; repeat for multiple cameras.",
    )
    parser.add_argument(
        "--delimiter",
        choices=("auto", "comma", "whitespace"),
        default="auto",
    )
    return parser.parse_args()


def split_record(line: str, delimiter: str) -> list[str]:
    if delimiter == "comma" or (delimiter == "auto" and "," in line):
        return next(csv.reader([line]))
    return line.split()


def load_vehicle_ids(
    path: Path,
    vehicle_column: int,
    camera_column: int,
    client_cameras: set[str],
    delimiter: str,
    vehicle_id_path_prefix: bool,
) -> tuple[set[str], set[str], int]:
    all_ids: set[str] = set()
    client_ids: set[str] = set()
    rows = 0
    max_column = max(vehicle_column, camera_column)

    with path.open("r", encoding="utf-8-sig") as source:
        for line_number, raw_line in enumerate(source, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            fields = [field.strip() for field in split_record(line, delimiter)]
            if len(fields) <= max_column:
                raise ValueError(
                    f"{path}:{line_number}: expected column {max_column}, got {len(fields)} fields"
                )

            vehicle_id = fields[vehicle_column]
            if vehicle_id_path_prefix:
                vehicle_id = vehicle_id.replace("\\", "/").split("/", 1)[0]
            if not vehicle_id:
                raise ValueError(f"{path}:{line_number}: empty vehicle ID")

            rows += 1
            all_ids.add(vehicle_id)

            if not client_cameras or camera_column < 0:
                client_ids.add(vehicle_id)
            elif fields[camera_column] in client_cameras:
                client_ids.add(vehicle_id)

    return all_ids, client_ids, rows


def encode_vehicle(vehicle_id: str, domain: str) -> bytes:
    return hashlib.sha256((domain + vehicle_id).encode("utf-8")).digest()[:16]


def write_blocks(path: Path, vehicle_ids: list[str], domain: str) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    blocks = [encode_vehicle(vehicle_id, domain) for vehicle_id in vehicle_ids]
    if len(set(blocks)) != len(blocks):
        raise RuntimeError("a 128-bit hash collision occurred in the generated set")
    payload = b"".join(blocks)
    path.write_bytes(payload)
    return hashlib.sha256(payload).hexdigest()


def main() -> None:
    args = parse_args()
    if args.server_size <= 0 or args.client_size <= 0:
        raise ValueError("server and client sizes must be positive")
    if not 0 <= args.intersection <= min(args.server_size, args.client_size):
        raise ValueError("intersection must be between 0 and both set sizes")
    if args.vehicle_id_column < 0:
        raise ValueError("vehicle ID column must be non-negative")

    all_ids: set[str] = set()
    client_pool: set[str] = set()
    row_count = 0
    for annotation_path in args.annotations:
        file_ids, file_client_ids, file_rows = load_vehicle_ids(
            annotation_path,
            args.vehicle_id_column,
            args.camera_id_column,
            set(args.client_camera),
            args.delimiter,
            args.vehicle_id_path_prefix,
        )
        all_ids.update(file_ids)
        client_pool.update(file_client_ids)
        row_count += file_rows

    required_unique = args.server_size + args.client_size - args.intersection
    if len(all_ids) < required_unique:
        raise ValueError(
            f"need at least {required_unique} unique vehicle IDs, found {len(all_ids)}"
        )
    if len(client_pool) < args.client_size:
        raise ValueError(
            f"need {args.client_size} client vehicle IDs, found {len(client_pool)}"
        )

    rng = random.Random(args.seed)
    client_ids = rng.sample(sorted(client_pool), args.client_size)
    intersection_ids = rng.sample(client_ids, args.intersection)
    client_set = set(client_ids)
    server_only_pool = sorted(all_ids - client_set)
    server_only_size = args.server_size - args.intersection
    server_ids = intersection_ids + rng.sample(server_only_pool, server_only_size)
    rng.shuffle(server_ids)
    rng.shuffle(client_ids)

    actual_intersection = len(set(server_ids) & set(client_ids))
    if actual_intersection != args.intersection:
        raise RuntimeError(
            f"constructed intersection {actual_intersection}, expected {args.intersection}"
        )

    server_sha256 = write_blocks(args.server_out, server_ids, args.domain)
    client_sha256 = write_blocks(args.client_out, client_ids, args.domain)

    metadata = {
        "dataset": "VERI-Wild",
        "annotations": [str(path) for path in args.annotations],
        "annotation_rows": row_count,
        "unique_vehicle_ids": len(all_ids),
        "client_candidate_ids": len(client_pool),
        "client_cameras": args.client_camera,
        "server_size": args.server_size,
        "client_size": args.client_size,
        "expected_intersection": args.intersection,
        "element_size_bytes": 16,
        "encoding": "Trunc128(SHA256(domain || vehicle_id))",
        "vehicle_id_path_prefix": args.vehicle_id_path_prefix,
        "domain": args.domain,
        "seed": args.seed,
        "server_file": str(args.server_out),
        "client_file": str(args.client_out),
        "server_file_sha256": server_sha256,
        "client_file_sha256": client_sha256,
    }
    args.metadata_out.parent.mkdir(parents=True, exist_ok=True)
    args.metadata_out.write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    print(f"dataset.name={metadata['dataset']}")
    print(f"dataset.unique_vehicle_ids={metadata['unique_vehicle_ids']}")
    print(f"dataset.server_size={args.server_size}")
    print(f"dataset.client_size={args.client_size}")
    print(f"dataset.expected_intersection={args.intersection}")
    print(f"output.server={args.server_out}")
    print(f"output.client={args.client_out}")
    print(f"output.metadata={args.metadata_out}")


if __name__ == "__main__":
    main()
