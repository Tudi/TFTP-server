TFTP server

############################################################
# Description
############################################################

Simple TFTP server with extension support. Cross platform compilable

############################################################
# Building
############################################################

# Below steps were used using a remote ssh console. I used nano to not type the contents of the files but to "paste" them from clipboard
# You can simply upload the files and build them directly
mkdir tftp
cd tftp
mkdir src
cd src
nano TFTP.cpp
nano TFTP.h
nano config.cpp
nano config.h
nano main.cpp
nano Logger.h
nano Logger.cpp
nano pthread_win.h
nano pthread_win.cpp
cd ..
nano CMakeLists.txt
nano init.cfg
cmake -Daws-sdk-cpp_DIR=../sdk_build
make

############################################################
# Usage
############################################################

#Syntax : ./TFTPSQS [PortNumber]
./TFTPSQS 25 