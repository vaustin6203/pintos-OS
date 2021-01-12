# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(cache-hit-rate) begin
(cache-hit-rate) open "design_doc.txt"
(cache-hit-rate) read "design_doc.txt"
(cache-hit-rate) open "design_doc.txt"
(cache-hit-rate) read "design_doc.txt"
(cache-hit-rate) end
cache-hit-rate: exit(0)
EOF
pass;
