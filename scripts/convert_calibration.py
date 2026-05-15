#!/usr/bin/env python3
"""Convert legacy arrays_test_*.npy calibration files to OpenCV YAML format."""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np


def write_yaml(output_path: Path, matrices: np.ndarray, map_image: str, mask_image: str) -> None:
    if matrices.shape != (3, 3, 3):
        raise ValueError(f"Expected calibration array shape (3,3,3), got {matrices.shape}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fs = cv2.FileStorage(str(output_path), cv2.FILE_STORAGE_WRITE)
    if not fs.isOpened():
        raise RuntimeError(f"Failed to open output yaml: {output_path}")

    fs.write("M_ground", matrices[0].astype(np.float32))
    fs.write("M_height_r", matrices[1].astype(np.float32))
    fs.write("M_height_g", matrices[2].astype(np.float32))
    fs.write("map_image", map_image)
    fs.write("mask_image", mask_image)
    fs.release()


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert .npy calibration arrays to OpenCV YAML.")
    parser.add_argument("--npy", required=True, type=Path, help="Input .npy file, shape=(3,3,3)")
    parser.add_argument("--out", required=True, type=Path, help="Output yaml path")
    parser.add_argument("--map", required=True, help="Map image path written to yaml")
    parser.add_argument("--mask", required=True, help="Mask image path written to yaml")
    args = parser.parse_args()

    matrices = np.load(args.npy)
    write_yaml(args.out, matrices, args.map, args.mask)
    print(f"[ok] wrote calibration yaml -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
