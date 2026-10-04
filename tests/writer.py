"""Independent writer for an in-host stale-decision regression; public APIs only."""

import argparse
from pathlib import Path

from postproject import Production, SequenceNaming, file_locator

parser = argparse.ArgumentParser()
parser.add_argument("production", type=Path)
parser.add_argument("binding")
parser.add_argument("directory", type=Path)
args = parser.parse_args()
with Production.open(args.production) as production:
    binding = production.host_bindings.parse(args.binding)
    representation = production.representation(binding.object)
    with production.transaction() as transaction:
        transaction.confirm_locator(
            representation.resources[0].id,
            file_locator(args.directory),
            sequence_naming=SequenceNaming("plateA.", ".png", 4),
        )
