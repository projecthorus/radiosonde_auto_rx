# Using virtual Python environment (venv)

## Creation of a new virtual Python environment

	[?]$ cd /home/pi/radiosonde_auto_rx
	[radiosonde_auto_rx]$ python3 -m venv venv
	
## Activate the new virtual Python environment

	[radiosonde_auto_rx]$ source ./venv/bin/activate
	(venv) [radiosonde_auto_rx]$

## Python packages installation in virtual Python environment

	(venv) [radiosonde_auto_rx]$ python3 -m pip list

	(venv) [radiosonde_auto_rx]$ python3 -m pip install some_python_package

	(venv) [radiosonde_auto_rx]$ python3 -m pip list

## requirements.txt file generation

	(venv) [radiosonde_auto_rx]$ python3 -m pip freeze > requirements.txt

## Installation des paquets à partir d'un fichier "requirements.txt"

	(venv) [radiosonde_auto_rx]$ python3 -m pip install -r ./requirements.txt

## Deactivation of virtual Python environment (exiting)

 	(venv) [radiosonde_auto_rx]$ deactivate
