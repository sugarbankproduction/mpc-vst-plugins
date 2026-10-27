# Linux workbench for running the repo's bash tools on Windows (see win/run.sh).
# gcc/g++ for tools/test_port.sh's x86 ASan build, python3 + Pillow for gen_vst.py, and a Docker CLI
# new enough for Docker Desktop 29 (Debian's docker.io speaks an API version the daemon now refuses).
FROM gcc:12
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 python3-pil \
 && rm -rf /var/lib/apt/lists/*
COPY --from=docker:29-cli /usr/local/bin/docker /usr/local/bin/docker
