#!/bin/bash

# Crear el grupo ufvauditor
groupadd ufvauditor

# Crear usuarios para cada sucursal y asignar al grupo ufvauditor
for i in {1..4}
do
    username="userSU00$i"
    useradd -m -g ufvauditor $username
    # Crear un directorio para cada usuario con los permisos específicos
    mkdir /home/$username/SU00$i
    chown $username:ufvauditor /home/$username/SU00$i
    chmod 770 /home/$username/SU00$i

    # Establecer permisos para que solo puedan leer en otros directorios de sucursales
    for j in {1..4}
    do
        if [ $i -ne $j ]; then
            mkdir -p /home/userSU00$j/SU00$j
            setfacl -m u:$username:r /home/userSU00$j/SU00$j
        fi
    done
done

# Crear el grupo ufvauditores
groupadd ufvauditores

# Crear usuarios userfp y usermonitor, y asignar al grupo ufvauditores
useradd -m -g ufvauditores userfp
useradd -m -g ufvauditores usermonitor

# Crear directorios especiales para FileProcessor y Monitor
mkdir /opt/FileProcessor
mkdir /opt/Monitor

# Asignar permisos para userfp
chmod 770 /opt/FileProcessor
chmod 770 /opt/Monitor
setfacl -m u:userfp:rwx /opt/FileProcessor
setfacl -m u:userfp:rwx /opt/Monitor
setfacl -m u:userfp:rwx /home

# Asignar permisos para usermonitor
chmod 700 /opt/Monitor
setfacl -m u:usermonitor:--x /opt/Monitor

# Asegurarse de que usermonitor no tiene permisos sobre otros directorios
setfacl -m u:usermonitor:--- /home

echo "Configuración completada."
