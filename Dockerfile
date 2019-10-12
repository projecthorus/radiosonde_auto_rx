# -------------------
# The build container
# -------------------
FROM alpine AS build
WORKDIR /root

# Update system packages and install build dependencies.
RUN apk upgrade --no-cache && \
  apk add --no-cache \
  build-base \
  cmake \
  libusb-dev

# Build RTL-SDR.
ADD https://github.com/steve-m/librtlsdr/archive/master.zip /root/librtlsdr-master.zip
RUN unzip librtlsdr-master.zip && \
  rm librtlsdr-master.zip && \
  cd librtlsdr-master && \
  mkdir build && \
  cd build && \
  cmake -DCMAKE_INSTALL_PREFIX=/root/target/usr/local ../ && \
  make && \
  make install

# Build the radiosonde_auto_rx decoders.
COPY . /root/radiosonde_auto_rx
RUN cd radiosonde_auto_rx/auto_rx && \
  sh build.sh

# -------------------------
# The application container
# -------------------------
FROM alpine
EXPOSE 5000/tcp

# Update system packages and install application dependencies.
RUN apk upgrade --no-cache && \
  apk add --no-cache \
  coreutils \
  libusb \
  py3-crcmod \
  py3-dateutil \
  py3-flask \
  py3-numpy \
  py3-requests \
  python3 \
  rng-tools \
  sox \
  usbutils

RUN pip3 --no-cache-dir install \
  flask-socketio

# Copy required artefacts from the build container.
COPY --from=build /root/target /
COPY --from=build /root/radiosonde_auto_rx/auto_rx /opt/auto_rx/

# Run auto_rx.py
WORKDIR /opt/auto_rx
CMD ["python3", "/opt/auto_rx/auto_rx.py"]
