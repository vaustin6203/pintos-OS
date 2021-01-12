# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(my-test-2-tell) begin
(my-test-2-tell) before read current position in file 0
(my-test-2-tell) after 1 char read current position in file 1
(my-test-2-tell) end
my-test-2-tell: exit(0)
EOF
pass;
