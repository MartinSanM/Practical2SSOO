
#Crear los grupos
sudo groupadd ufvauditor
sudo groupadd ufvauditores


# Crear los usuarios para las sucursales y añadirlos al grupo ufvauditor
sudo useradd -m -G ufvauditor userSU001
sudo useradd -m -G ufvauditor userSU002
sudo useradd -m -G ufvauditor userSU003
sudo useradd -m -G ufvauditor userSU004

# Crear los usuarios userfp y usermonitor y añadirlos al grupo ufvauditores
sudo useradd -m -G ufvauditores userfp
sudo useradd -m -G ufvauditores usermonitor


# Crear directorios para cada sucursal
sudo mkdir -p ./Files/SU001
sudo mkdir -p ./Files/SU002
sudo mkdir -p ./Files/SU003
sudo mkdir -p ./Files/SU004

sudo chgrp ufvauditores ./*
sudo chown userfp ./*
sudo chmod 600 ./*
sudo chgrp ufvauditor ./Files
sudo chmod 760 ./Files
sudo chmod 770 ./*.sh

sudo chown userSU001:ufvauditor ./Files/SU001
sudo chown userSU002:ufvauditor ./Files/SU002
sudo chown userSU003:ufvauditor ./Files/SU003
sudo chown userSU004:ufvauditor ./Files/SU004

sudo chmod 621 ./Files/SU001
sudo chmod 621 ./Files/SU002
sudo chmod 621 ./Files/SU003
sudo chmod 621 ./Files/SU004


sudo chown userfp:ufvauditores ./testFinal
sudo chown usermonitor:ufvauditores ./testMon
sudo chown root:ufvauditores ./fp.conf

sudo chmod 100 ./testFinal
sudo chmod 110 ./testMon

# Archivo de configuración
sudo chmod 660 ./fp.conf
sudo setfacl -m u:userfp:rw ./fp.conf
sudo setfacl -m u:usermonitor:rw ./fp.conf
