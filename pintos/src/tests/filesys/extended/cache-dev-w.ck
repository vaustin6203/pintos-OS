# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(cache-dev-w) begin
(cache-dev-w) open "giant.txt"
(cache-dev-w) write "giant.txt"
(cache-dev-w) read "giant.txt"
(cache-dev-w) end
cache-dev-w: exit(0)
EOF
pass;
