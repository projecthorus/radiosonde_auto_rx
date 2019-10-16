FROM debian:buster-slim
EXPOSE 5000/tcp

# Update system packages and install build and application dependencies.
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y \
  build-essential \
  python3 \
  python3-crcmod \
  python3-dateutil \
  python3-flask \
  python3-numpy \
  python3-pip \
  python3-requests \
  python3-setuptools \
  rng-tools \
  rtl-sdr \
  sox \
  usbutils && \
  rm -rf /var/lib/apt/lists/*

RUN pip3 --no-cache-dir install \
  flask-socketio

# Build the radiosonde_auto_rx binaries and copy auto_rx to /opt.
COPY . /tmp/radiosonde_auto_rx
RUN cd /tmp/radiosonde_auto_rx/auto_rx && \
  sh build.sh && \
  cd ../ && \
  mv /tmp/radiosonde_auto_rx/auto_rx /opt/ && \
  rm -rf /tmp/radiosonde_auto_rx

# Remove packages that were only needed for building.
RUN apt-get remove -y \
  build-essential \
  python3-pip \
  python3-setuptools && \
  apt-get autoremove -y && \
  rm -rf /var/lib/apt/lists/*

# Run auto_rx.py.
WORKDIR /opt/auto_rx
CMD ["python3", "/opt/auto_rx/auto_rx.py"]
