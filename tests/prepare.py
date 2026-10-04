"""Make genuine frames and producer facts through the maintained public binding."""

import argparse
import json
import struct
import zlib
from pathlib import Path

from postproject import (
    ImageSequenceSource,
    MetadataProperty,
    MetadataString,
    Production,
    SequenceNaming,
    file_locator,
)

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--library", required=True)
args = parser.parse_args()
root = args.root.resolve()
directory = root / "seed"
directory.mkdir(parents=True, exist_ok=True)


def png(color):
    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data))
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress((b"\0" + bytes(color) * 2) * 2))
        + chunk(b"IEND", b"")
    )


with Production.create(
    root / "shared.pproj", "Sequence handoff", library_path=args.library
) as production:
    with production.transaction() as transaction:
        transaction.add_media_root("plates")
        for prefix, blue in (("plateA.", 20), ("plateB.", 180)):
            for frame in range(1001, 1004):
                (directory / f"{prefix}{frame:04d}.png").write_bytes(
                    png((frame % 256, 50, blue))
                )
            asset = transaction.import_media(
                ImageSequenceSource(
                    directory,
                    SequenceNaming(prefix, ".png", 4),
                    1001,
                    1003,
                    1,
                    24,
                    1,
                    (),
                ),
                prefix,
            )
            transaction.add_metadata(
                asset,
                MetadataProperty("example.org/unrecognized", "note"),
                MetadataString("keep exactly: α/unknown"),
            )
    destination = root / "plates"
    directory.rename(destination)
    directory = destination
    bindings = {}
    for asset in production.assets:
        representation = production.representations[asset.id][0]
        bindings[asset.display_name] = production.host_bindings[representation.id]
        with production.transaction() as transaction:
            transaction.confirm_locator(
                representation.resources[0].id,
                file_locator(directory, library_path=args.library),
                media_root="plates",
                sequence_naming=SequenceNaming(asset.display_name, ".png", 4),
            )
    (root / "bindings.json").write_text(json.dumps(bindings))
print(root)
