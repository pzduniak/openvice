cd mtasa
autoreconf -fiv
./configure
make
make -C MTA10_Server install
make -C Shared/XML install