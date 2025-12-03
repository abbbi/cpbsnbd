FROM debian:trixie AS build

COPY . /build
WORKDIR /build
RUN apt-get update && apt-get install -y wget
RUN echo "deb http://download.proxmox.com/debian/pve trixie pve-no-subscription trixie" > /etc/apt/sources.list.d/pxmx.list
RUN wget https://enterprise.proxmox.com/debian/proxmox-release-trixie.gpg -O /etc/apt/trusted.gpg.d/proxmox-release-trixie.gpg
RUN apt-get update && apt-get install -y nbdkit-plugin-dev \
    libproxmox-backup-qemu0 \
    libproxmox-backup-qemu0-dev \
    build-essential

RUN make

FROM scratch AS export-stage
COPY --from=build /build/nbdkit-pbs-plugin.so .
