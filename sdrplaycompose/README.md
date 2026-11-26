Build docker image (one dir back) with `docker build -f Dockerfile.sdrplay -t radiod .`
Edit example radiod config provided in `radiod_config`
Edit example autorx config provided in `radiosonde_auto_rx`
Place `SDRplay_RSP_API-Linux-3.15.2.run` from https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run in `sdrplay`
`docker compose up`