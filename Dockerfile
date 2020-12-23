# -------------------
# The build container
# -------------------
FROM debian:buster-slim AS build

# Update system packages and install build dependencies.
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y \
  build-essential \
  python3 \
  python3-crcmod \
  python3-dateutil \
  python3-flask \
  python3-numpy \
  python3-pip  \
  python3-requests && \
  rm -rf /var/lib/apt/lists/*

# Copy in radiosonde_auto_rx
COPY . /root/radiosonde_auto_rx

# Install additional Python packages that aren't available through apt-get.
RUN pip3 --no-cache-dir install -r /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Build the binaries.
RUN cd /root/radiosonde_auto_rx/auto_rx && \
  sh build.sh

# -------------------------
# The application container
# -------------------------
FROM debian:buster-slim
EXPOSE 5000/tcp

# Update system packages and install application dependencies.
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y \
  python3 \
  python3-crcmod \
  python3-dateutil \
  python3-flask \
  python3-numpy \
  python3-requests \
  rng-tools \
  rtl-sdr \
  sox \
  usbutils && \
  rm -rf /var/lib/apt/lists/*

# Copy any additional Python packages from the build container.
COPY --from=build /usr/local/lib/python3.7/dist-packages /usr/local/lib/python3.7/dist-packages

# Copy auto_rx from the build container to /opt.
COPY --from=build /root/radiosonde_auto_rx/auto_rx /opt/auto_rx

# Run auto_rx.py.
WORKDIR /opt/auto_rx
CMD ["python3", "/opt/auto_rx/auto_rx.py"]
