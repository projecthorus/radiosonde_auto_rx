# -------------------
# The build container
# -------------------
FROM debian:bullseye-slim AS build

# Upgrade base packages.
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libatlas-base-dev \
    libsamplerate0-dev \
    libusb-1.0-0-dev \
    pkg-config \
    python3 \
    python3-dev \
    python3-pip \
    python3-setuptools \
    python3-wheel && \
  rm -rf /var/lib/apt/lists/*

# Copy in requirements.txt.
COPY auto_rx/requirements.txt \
  /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Install Python packages.
RUN --mount=type=cache,target=/root/.cache/pip pip3 install \
  --user --no-warn-script-location --ignore-installed --no-binary numpy \
  -r /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Compile rtl-sdr from source.
RUN git clone https://github.com/steve-m/librtlsdr.git /root/librtlsdr && \
  mkdir -p /root/librtlsdr/build && \
  cd /root/librtlsdr/build && \
  cmake -DCMAKE_INSTALL_PREFIX=/root/target/usr/local -Wno-dev ../ && \
  make && \
  make install && \
  rm -rf /root/librtlsdr

# Compile spyserver_client from source.
RUN git clone https://github.com/miweber67/spyserver_client.git /root/spyserver_client && \
  cd /root/spyserver_client && \
  make

# Copy in radiosonde_auto_rx.
COPY . /root/radiosonde_auto_rx

# Build the radiosonde_auto_rx binaries.
WORKDIR /root/radiosonde_auto_rx/auto_rx
RUN /bin/sh build.sh

# -------------------------
# The application container
# -------------------------
FROM debian:bullseye-slim

EXPOSE 5000/tcp

# Upgrade base packages and install application dependencies.
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y --no-install-recommends \
  libatlas3-base \
  libatomic1 \
  libsamplerate0 \
  python3 \
  rng-tools \
  sox \
  tini \
  usbutils && \
  rm -rf /var/lib/apt/lists/*

# Copy rtl-sdr from the build container.
COPY --from=build /root/target /
RUN ldconfig

# Copy any additional Python packages from the build container.
COPY --from=build /root/.local /root/.local

# Copy auto_rx from the build container to /opt.
COPY --from=build /root/radiosonde_auto_rx/LICENSE /opt/auto_rx/
COPY --from=build /root/radiosonde_auto_rx/auto_rx/ /opt/auto_rx/

# Copy ss_client from the build container and create links
COPY --from=build /root/spyserver_client/ss_client /opt/auto_rx/
RUN ln -s ss_client /opt/auto_rx/ss_iq && \
  ln -s ss_client /opt/auto_rx/ss_power

# Set the working directory.
WORKDIR /opt/auto_rx

# Ensure scripts from Python packages are in PATH.
ENV PATH=/root/.local/bin:$PATH

# Use tini as init.
ENTRYPOINT ["/usr/bin/tini", "--"]

# Run auto_rx.py.
CMD ["python3", "/opt/auto_rx/auto_rx.py"]
