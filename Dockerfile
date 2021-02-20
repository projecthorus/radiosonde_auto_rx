# -------------------
# The build container
# -------------------
FROM python:3.7-buster AS build

# Upgrade base packages.
RUN apt-get update && \
  apt-get upgrade -y && \
  rm -rf /var/lib/apt/lists/*

# Copy in requirements.txt.
COPY auto_rx/requirements.txt \
  /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Install Python packages.
RUN pip3 --no-cache-dir install --user --no-warn-script-location \
  --extra-index-url https://www.piwheels.org/simple \
  -r /root/radiosonde_auto_rx/auto_rx/requirements.txt

# Copy in radiosonde_auto_rx.
COPY . /root/radiosonde_auto_rx

# Build the binaries.
WORKDIR /root/radiosonde_auto_rx/auto_rx
RUN /bin/sh build.sh

# -------------------------
# The application container
# -------------------------
FROM python:3.7-slim-buster
EXPOSE 5000/tcp

# Upgrade base packages and install application dependencies.
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y \
  rng-tools \
  rtl-sdr \
  sox \
  tini \
  usbutils && \
  rm -rf /var/lib/apt/lists/*

# Copy any additional Python packages from the build container.
COPY --from=build /root/.local /root/.local

# Copy auto_rx from the build container to /opt.
COPY --from=build /root/radiosonde_auto_rx/LICENSE /opt/auto_rx/
COPY --from=build /root/radiosonde_auto_rx/auto_rx/ /opt/auto_rx/

# Set the working directory.
WORKDIR /opt/auto_rx

# Ensure scripts from Python packages are in PATH.
ENV PATH=/root/.local/bin:$PATH

# Use tini as init.
ENTRYPOINT ["/usr/bin/tini", "--"]

# Run auto_rx.py.
CMD ["python3", "/opt/auto_rx/auto_rx.py"]
