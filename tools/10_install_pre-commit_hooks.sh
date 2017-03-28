#!/bin/bash

script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hook=${script_path}/../.git/hooks/pre-commit

rm -f ${hook}
ln -s ${script_path}/pre-commit ${hook}
chmod +x ${hook}
