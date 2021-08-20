# -------------------
# The build container
# -------------------
FROM alpine:3.14 AS build

# Upgrade base packages.
RUN apk upgrade --no-cache

# Install build dependencies.
RUN apk add --no-cache \
  build-base \
  python3 \
  python3-dev \
  py3-pip \
  py3-wheel

# Copy in requirements.txt.
COPY auto_rx/requirements.txt \
  /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Install additional Python packages.
RUN --mount=type=cache,target=/root/.cache/pip pip3 install \
  --user --no-warn-script-location --ignore-installed --no-binary numpy \
  -r /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Copy in radiosonde_auto_rx.
COPY . /root/radiosonde_auto_rx

# Build the binaries.
WORKDIR /root/radiosonde_auto_rx/auto_rx
RUN /bin/sh build.sh

# -------------------------
# The application container
# -------------------------
FROM alpine:3.14

EXPOSE 5000/tcp

# Upgrade base packages.
RUN apk upgrade --no-cache

# Install application dependencies.
RUN apk add --no-cache \
  coreutils \
  python3 \
  rtl-sdr \
  sox \
  tini \
  usbutils

# Copy any additional Python packages from the build container.
COPY --from=build /root/.local /root/.local

# Copy auto_rx from the build container to /opt.
COPY --from=build /root/radiosonde_auto_rx/LICENSE /opt/auto_rx/
COPY --from=build /root/radiosonde_auto_rx/auto_rx/ /opt/auto_rx/

# Set the working directory.
WORKDIR /opt/auto_rx

# Use tini as init.
ENTRYPOINT ["/sbin/tini", "--"]

# Run auto_rx.py.
CMD ["python3", "/opt/auto_rx/auto_rx.py"]
