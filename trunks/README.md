# Trunks

The Trunks TPM Library (TTL) is a set of types and functions used to interface
with a Trusted Platform Module.  It is designed to be light, and does not
comply with the TSS specification.  It is usable in firmware as well as in
user-level code.

## TPM Specification

See http://www.trustedcomputinggroup.org.  This version of trunks is based on
TPM 2.0 rev 00.99.

### Structures

`generator/raw_structures.txt`

`generator/raw_structures_fixed.txt`

This file is a direct PDF scrape (*) of 'Part 2 - Structures'.  The `_fixed`
version includes some manual fixes to make processing easier.

### Commands

`generator/raw_commands.txt`

`generator/raw_commands_fixed.txt`

This file is a direct PDF scrape (*) of 'Part 3 - Commands'.  The `_fixed`
version includes some manual fixes to make processing easier.

(*) Scraping for this version of trunks used Poppler's `pdftotext` utility
    v0.18.4.

## Code Generation

### `generator/extract_structures.sh`

Extracts structured information about types, constants, structures, and unions
from `generator/raw_structures_fixed.txt`.  The output of this script is
intended to be parsed by `generator.py`.

### `generator/extract_commands.sh`

Extracts structured information about commands from
`generator/raw_commands_fixed.txt`.  The output of this script is intended to be
parsed by `generator.py`.

### `generator/generator.py`

Generates C++ serialization and parsing code for TPM commands.  Inputs must be
formatted as by the `extract_*` scripts.
