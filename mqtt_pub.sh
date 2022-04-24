#!/bin/bash
#set -x
count=1
while :
do
    #payload=$(date +"%T")
    payload="text${count}"
    mosquitto_pub -h broker.emqx.io -p 1883 -t "testtopic/electron" -m $payload
    echo mosquitto_pub -h broker.emqx.io -p 1883 -t "testtopic/electron" -m $payload
    count=$((++count))
    sleep 5
done
