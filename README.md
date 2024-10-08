# esp-idf-mqtt-client
GUI MQTT Client for esp-idf.   
I used [this](https://github.com/Molorius/esp32-websocket) component.   
This component can communicate directly with the browser.   
There is an example of using the component [here](https://github.com/Molorius/ESP32-Examples).
It's a great job.   

I use [this](https://github.com/emqx/MQTT-Client-Examples/tree/master/mqtt-client-Electron) index.html.   
I use [this](https://bulma.io/) open source framework.   

![mqtt-client](https://user-images.githubusercontent.com/6020549/139746798-d83496dd-3f3f-471d-bc9e-b9baaf9bdc01.jpg)

# Software requirements
ESP-IDF V5.0 or later.   
ESP-IDF V4.4 release branch reached EOL in July 2024.   
ESP-IDF V5.1 is required when using ESP32-C6.   

# Installation
```
git clone https://github.com/nopnop2002/esp-idf-mqtt-client
cd esp-idf-mqtt-client
git clone https://github.com/Molorius/esp32-websocket components/websocket
idf.py menuconfig
idf.py flash monitor
```

# Application Setting
![config-main](https://user-images.githubusercontent.com/6020549/139746838-6fb6ddd2-3abb-4f15-9aa0-1af51759579d.jpg)
![config-app-1](https://user-images.githubusercontent.com/6020549/139746864-50e7e765-9733-4dc0-b5fb-46a585dc62fe.jpg)

You can use the MDNS hostname instead of the IP address.   
![config-app-2](https://user-images.githubusercontent.com/6020549/139746873-09ab4d54-c6f1-41a3-bb2e-6a0ea65d3be7.jpg)
![mqtt-client-2](https://user-images.githubusercontent.com/6020549/139753130-ce044b46-daac-4540-836a-192d8d528809.jpg)

# How to use
- Open browser.   
- Enter the esp32 address in your browser's address bar.   
 You can use the mDNS hostname instead of the IP address.   
 Default mDNS name is esp32-server.local.   
- Enter Host & port.   
 You can specify the mDNS host name like mqtt-broker.local for the host name.   
 You can use broker.emqx.io as public broker.   
 Press Connect button.   
- Press Subscribe button.   
- Enter the payload and press the Send button.   

You can publish new topic using mqtt_pub.sh.   
- Open terminal.   
- Start mqtt_pub.sh.   

# Reference
https://github.com/nopnop2002/esp-idf-mqtt-image-client

# Task Structure Diagram
![Task_structure_diagram](https://user-images.githubusercontent.com/6020549/139747430-1257fc80-7519-4d6e-80d7-740adc2e0e17.JPG)
