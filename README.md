TFTP server

############################################################
# Description
############################################################

Simple TFTP server with extension support

############################################################
# Prerequisits for building it
############################################################

#! Note that to build Amazon SDK you need at least 4 GB of RAM. Free tier servers will freeze when trying to build the SDK
# Below steps are for a naked amazon EC2 instance. Based on your PC, you might not need all these steps

sudo apt-get install libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
sudo apt-get update
sudo apt-get install cmake
sudo apt install g++
sudo apt-get install libexplain-dev
git clone https://github.com/aws/aws-sdk-cpp.git
sudo mkdir sdk_build
cd sdk_build
sudo cmake ../aws-sdk-cpp/ -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DBUILD_SHARED_LIBS=ON -DSTATIC_LINKING=1 -DCMAKE_C_COMPILER="/usr/bin/gcc" -DCMAKE_CXX_COMPILER="/usr/bin/g++" -DBUILD_ONLY="sqs"
sudo make -j 4
sudo make install

# You will need to set amazon credentials in order for the SQS service to function. Set amazon acces key and secret
sudo apt install awscli
aws configure

############################################################
# Building
############################################################

# Below steps were used using a remote ssh console. I used nano to not type the contents of the files but to "paste" them from clipboard
# You can simply upload the files and build them directly
# If you wish to enable log messages, you should build it with definition -D_DEBUG
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
nano AWSSQS.h
nano AWSSQS.cpp
cd ..
nano CMakeLists.txt
nano init.cfg
cmake -Daws-sdk-cpp_DIR=../sdk_build
make VERBOSE=1

############################################################
# Configure
############################################################

# You need to enable priviledged port usage by runnin this command. Of course you need to update the path to the program ( last argument )
sudo setcap 'cap_net_bind_service=+ep' /home/ubuntu/tftp/TFTPSQS

# Edit the config file to allow uploading / downloading of files

############################################################
# Usage
############################################################

#Syntax : ./TFTPSQS [PortNumber] [ServerId]
./TFTPSQS 69 EC2SomeSrv123 