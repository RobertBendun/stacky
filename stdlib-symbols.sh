#!/bin/sh

echo "#define Stdlib_Functions \\"
grep -oE "_stacky_[a-z_0-9]+" stdlib.cc | sed 's/.*/\t"extern \0\\n" \\/'
echo ""
