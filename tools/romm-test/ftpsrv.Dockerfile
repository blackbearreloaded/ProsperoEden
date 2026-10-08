# ProsperoEden - ftpsrv (https://github.com/ps5-payload-dev/ftpsrv), the FTP server the console runs
# to write the downloads, built for Linux (its Makefile.linux) for tools/check-romm.py.
FROM debian:12-slim AS build
ARG FTPSRV_COMMIT=23737c4738d15b9b9b036665bbf008d8fd6d82d2
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates gcc git libc6-dev make \
    && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/ps5-payload-dev/ftpsrv.git /ftpsrv && cd /ftpsrv \
    && git checkout --quiet "$FTPSRV_COMMIT" && make -f Makefile.linux

FROM debian:12-slim
COPY --from=build /ftpsrv/ftpsrv-posix.elf /usr/local/bin/ftpsrv
ENTRYPOINT ["/usr/local/bin/ftpsrv"]
