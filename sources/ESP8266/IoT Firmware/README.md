- [ABOUT Olimex ESP8266-EVB IoT Firmware](#)
	- [Security](#security)
	- [Operating modes](#operating-modes)
	- [QUICK START](#quick-start)
	- [OPERATION](#operation)
		- [Basic Response Message Format](#basic-response-message-format)
		- [Events Message Format](#events-message-format)
		- [Requests](#requests)
			- [Devices & Entry Points](#devices--entry-points)
			- [REST Service](#rest-service)
			- [WebSockets](#websockets)
	- [CONFIGURATION](#configuration)
		- [General Configuration](#general-configuration)
		- [Access Point Configuration](#access-point-configuration)
		- [Station Configuration](#station-configuration)
		- [IoT Server Configuration](#iot-server-configuration)
		- [SSL Configuration](#ssl-configuration)
		- [Firmware Update](#firmware-update)
	- [DEVICES REFERENCE](#devices-reference)
		- [Native](#native)
		- [I2C](#i2c)
		- [SPI](#spi)
		- [UART](#uart)
	- [Sample HTML / JavaScript application](#sample-html--javascript-application)

# ABOUT Olimex ESP8266-EVB IoT Firmware

ESP8266-EVB IoT Firmware implements JSON Based Web Server and Client.

The firmware is intended to be used by people with sufficient knowledge in web application 
development, without the need of any knowledge in embedded technologies. The IoT firmware
allows such developers to use UEXT modules manufactured by Olimex with ease.

## Security

SSL implementation is quite restricted due to small RAM amount. 
Successfully tested with 1024 bit keys. Unfortunately because of large number of supported modules there is not enough free RAM. If you want to enable SSL you should compile firmware with reduced modules support

For example:
	
	make clean
	make DEVICE=EVB_ONLY SSL=1 APP=1

You can use [Sample HTML / JavaScript application](#sample-html--javascript-application) to upload your SSL keys.

## Operating modes

Firmware operates in two different modes:
* HTTP REST Service
* WebSockets

Support Events:
* to connected clients
* to IoT Server

ESP8266-EVB It can works as Access Point, WiFi Station or both at the same time. 

Reset to defaults: hold button pressed for 5 seconds.
 
Default Access Point
	
	SSID     : ESP_OLIMEX
	Password : olimex-ap
	Mode     : WPA2 PSK
	IP       : 192.168.4.1
	
Optional Authentication - enabled by default
	
* Basic HTTP Authentication
* WebSockets - Authentication message

Default credentials
	
	User     : olimex
	Password : olimex

Supported Devices
	
* Native
	* Button
	* Relay
	* ADC
* I2C
	* MOD-RGB
	* MOD-TC-MK2-31855
	* MOD-IO2
	* MOD-IRDA+
* SPI
	* MOD-LED8x8RGB
* UART
	* MOD-RFID125-BOX
	* Fingerprint - work in progress

## QUICK START

**How to load firmware for the first time**

You will need a seral connection established. This connection requires seral cable with level shifter. We will use esptool. Enter bootloader mode - hold the button pressed down and power the board, then release the button. Use the following command issued from root folder:
	
	esptool/esptool.py --baud 576000 write_flash 0x00000 bin/boot_v1.5.bin 0x01000 bin/upgrade/user1.2048.new.3.bin 0x81000 bin/upgrade/user2.2048.new.3.bin 0x100000 bin/blank.bin 0x1FC000 bin/esp_init_data_default.bin 0x1FE000 bin/blank.bin --flash_size 16m

**How to compile firmware**

If you do not want to use precompiled binaries from bin/upgrade folder then you can compile firmware yourself using following commands sequence:

*Compiling bin/upgrade/user1.2048.new.3.bin*
	
	make clean
	make APP=1

*Compiling bin/upgrade/user2.2048.new.3.bin*
	
	make clean
	make APP=2


**JavaScript code example**

```JavaScript
// Connect to ESP8266-EVB events entry point
var socket = new WebSocket('ws://192.168.4.1/events');

socket.onopen = function () {
	// Send authentication message
	this.send(
		JSON.stringify(
			{
				User: 'olimex',
				Password: 'olimex'
			}
		)
	);
	
	// Send request for Devices and URL Entry Points
	setTimeout(
		function () {
			socket.send(
				JSON.stringify(
					{
						URL: '/',
						Method: 'GET'
					}
				)
			);
		},
		500
	);
}

socket.onmessage = function (event) {
	try {
		// Print received messages in current document
		document.write('<pre>'+JSON.stringify(JSON.parse(event.data), null, 4)+'</pre>');
	} catch (e) {
		// Print errors in console
		console.log(e.message);
	}
};

socket.onerror = function (event) {
	console.log("WebSocket ERROR");
};

socket.onclose = function (event) {
	console.log(event.code+': '+(event.reason ? event.reason : 'WebSocket error'));
};
```


## OPERATION

Two modes of operation 
* HTTP REST Service (Long poll for events handling)
* WebSockets - RECOMMENDED
	
### Basic Response Message Format
	
	{
		"Device" : "ESP8266",
		
		"Status" : "OK", 
		<or>
		"Error"  : "Device not found",
		
		"Data"   : {
			<device dependent>
		}
	}
		
### Events Message Format
	
	{
		EventURL   : "/button",
		EventsData : {
			<Basic Response Message>
		}
	}
	
### Requests
	
### Devices & Entry Points

To get information about installed device drivers and URL for entry points you have to issue GET request to the root.
	
	GET / HTTP/1.1
	Host: 192.168.4.1
	Authorization: Basic b2xpbWV4Om9saW1leA==
	
### REST Service

To read relay status using REST Service - execute following GET request
	
	GET /relay HTTP/1.1
	Host: 192.168.4.1
	Authorization: Basic b2xpbWV4Om9saW1leA==
	
Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data" : {
			"Relay" : 0
		}
	}

To set relay status via REST Service - execute following POST
	
	POST /relay HTTP/1.1
	Host: 192.168.4.1
	Content-Length: 11
	Authorization: Basic b2xpbWV4Om9saW1leA==
	Content-Type: text/plain;charset=UTF-8
	
	{"Relay":1}

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data" : {
			"Relay" : 1
		}
	}

### WebSockets

Main WebSockets entry point URL is /events. On this entry point you will receive all events. If you want to receive only particular device events you can use its URL for example if you want to receive only MOD-TC-MK2 (temperature changes) events then you can connect to /mod-tc-mk2.

```JavaScript
// Connect to ESP8266-EVB events entry point
var socket = new WebSocket('ws://192.168.4.1/events');
```

Read relay status via WebSockets -  send following message
	
```JavaScript	
socket.send(
	JSON.stringify(
		{
			URL: '/relay',
			Method: 'GET'
		}
	)
);
```

Sample Response: 
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data" : {
			"Relay" : 0
		}
	}
			
Set relay status via WebSockets - send following message

```JavaScript	
socket.send(
	JSON.stringify(
		{
			URL: '/relay',
			Method: 'POST',
			Data: {
				Relay: 1
			}
		}
	)
);
```

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data" : {
			"Relay" : 1
		}
	}

## CONFIGURATION

### General Configuration
	
	URL : /config

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data"   : {
			"Config" : {
				"SDKVersion" : "1.5.1(e67da894)", 
				"ResetInfo" : "4:0:00000000", 
				"PHYMode" : "802.11g", 
				"AccessPointMAC" : "1A:FE:34:9C:61:DE", 
				"StationMAC"     : "18:FE:34:9C:61:DE", 
				"User"           : "olimex", 
				"Password"       : "olimex", 
				"Mode"           : "Access Point and Station", 
				"Authentication" : 1, 
				"SSL"            : 0
			}
		}
	}
		
Data fields description:
	
	SDKVersion     : <read-only>, 
	ResetInfo      : <read-only>, 
	PHYMode        : <read-only>, 
	AccessPointMAC : <read-only>, 
	StationMAC     : <read-only>, 
	
	User           : <user name>, 
	Password       : <password>, 
	
	Mode           : <Access Point | Station | Access Point and Station>, 
		WiFi operational mode. 
		Default - Access Point and Station
		Recommended - Station
	
	Authentication : 1, 
		Use or not authentication
	
	SSL            : 0
		If set to 1 redirects to HTTPS if non SSL request is received
		
Sample POST Data:
	
	{
		"Config" : {
			"User"           : "john", 
			"Password"       : "doe", 
			"Mode"           : "Station", 
			"Authentication" : 1, 
			"SSL"            : 0
		}
	}
	
### Access Point Configuration
	
	URL : /config/access-point
	
Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data"   : {
			"AccessPoint" : {
				"SSID" : "ESP_OLIMEX", 
				"Password" : "olimex-ap", 
				"Mode" : "WPA2 PSK", 
				"Hidden" : 0, 
				"MaxConnections" : 2, 
				"BeaconInterval" : 100, 
				"DHCP" : 1, 
				"IP" : {
					"Address" : "192.168.4.1", 
					"NetMask" : "255.255.255.0", 
					"Gateway" : "192.168.4.1"
				}
			}
		}
	}
		
Data fields description:
	
	SSID     : <desired WiFi SSID>, 
	Password : <password>, 
	Mode     : <Open | WEP | WPA PSK | WPA2 PSK | WPA WPA2 PSK>, 
		WiFi Encryption Mode
		Default: WPA2 PSK
		
	Hidden   : 0, 
		Do not broadcast SSID
		
	MaxConnections   : 2,
		Maximum allowed Access Point connections
			Default: 2
			Maximum: 4
		
	BeaconInterval   : 100, 
		
	DHCP     : 1, 
		Enable DHCP Server
		
	IP : {
		Address : <Access Point IP address>, 
			Default: 192.168.4.1
			
		NetMask : <Network Mask>, 
			Default: 255.255.255.0
			
		Gateway : <Gateway>
			Default: 192.168.4.1
	}
		
### Station Configuration
	
	URL : /config/station

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data" : {
			"Station" : {
				"SSID"        : "SampleWiFiNetwork", 
				"Password"    : "HardToBreak", 
				"AutoConnect" : 1, 
				"DHCP"        : 1, 
				"IP" : {
					"Address" : "192.168.0.125", 
					"NetMask" : "255.255.255.0", 
					"Gateway" : "192.168.0.1"
				}
			}
		}
	}
		
Data fields description:
	
	SSID        : <SSID of the network to connect>, 
	Password    : <password>, 
	
	AutoConnect : 1, 
		Auto connect to the network
		Default: 1
		
	DHCP        : 1, 
		Use DHCP to get IP address
		Default: 1
		
	IP : {
		Address : <read-only if DHCP is 1>, 
			Default: 192.168.10.2
			
		NetMask : <read-only if DHCP is 1>, 
			Default: 255.255.255.0
			
		Gateway : <read-only if DHCP is 1> 
			Default: 192.168.10.1
	}
		
### IoT Server Configuration
	
	URL : /config/iot

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data" : {
			"IoT" : {
				"WebSocket" : 1, 
				"SSL" : 0, 
				"Server" : "iot.olimex.com", 
				"User" : "", 
				"Password" : "", 
				"Path" : "/", 
				"Name" : "Termometer", 
				"Token" : "123456789ABC"
			}
		}
	}
			
Data fields description:
	
	WebSocket : 1, 
		use WebSocket to connect to IoT Server
		
	SSL       : 0, 
		use secure connection - work in progress
		
	Server    : <host name or IP address>, 
		
	User      : <user name>, 
	Password  : <password>, 
		Credentials to authenticate to server.
		If WebSocket is used they are send as first message.
		Otherwise Basic HTTP Authentication headers are used.
	
	Path      : "/", 
		IoT Server Entry point
		
	Name      : "Termometer", 
		Human friendly device name
		
	Token     : "123456789ABC"
		Device token
		
### SSL Configuration
	
	URL : /config/ssl

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data"   : {}
	}
		
Upload SSL Private key and Certificate.

Sample POST data:
	
	{
		"SSL" : {
			"PrivateKey"  : "FILE-DATA:493",
			"Certificate" : "FILE-DATA:449"
		}
	}
	<0>PrivateKey:493:<493 bytes data>Certificate:449:<449 bytes data>
			
Sample WebSocket data (binary message)
	
	{
		"URL" : "/config/ssl",
		"Method" : "POST",
		"Data" : {
			"SSL" : {
				"PrivateKey"  : "FILE-DATA:493",
				"Certificate" : "FILE-DATA:449"
			}
		}
	}
	<0>PrivateKey:493:<493 bytes data>Certificate:449:<449 bytes data>
			
### Firmware Update

	URL : /config/firmware

Sample Response:
	
	{
		"Device" : "ESP8266", 
		"Status" : "OK", 
		"Data"   : {
			"Firmware" : {
				"Current" : "user2.bin", 
				"Boot"    : "v2 normal 0x00FFFFFF"
			}
		}
	}
		
If Current is user1.bin you MUST upload user2.bin and vice versa.

Sample POST data
	
	{
		"Firmware" : {
			"boot.bin"  : "FILE-DATA:0",
			"user1.bin" : "FILE-DATA:278880",
			"user2.bin" : "FILE-DATA:0",
		}
	}
	<0>user1.bin:278880:<278880 bytes data>

## DEVICES REFERENCE

### Native
* Button - Long Poll, timeout 30 seconds
		
		URL : /button
	
	Sample Response:
		
		{
			"Device" : "ESP8266", 
			"Status" : "OK", 
			"Data"   : {
				"Button" : "Short Press"
			}
		}

* Relay
	 	
		URL : /relay
	
	**JavaScript Example**

	```JavaScript
	// Read relay status
	socket.send(
		JSON.stringify(
	    		{
				URL: '/relay',
				Method: 'GET'
			}
		)
	);
	```

	Sample Response:
		
		{
			"Device" : "ESP8266", 
			"Status" : "OK", 
			"Data" : {
				"Relay" : 0
			}
		}

	Sample POST data
		
		{
			"Relay" : 1
		}
	
	If [value] is 0 relay is switched off.
	
	If [value] is 1 relay is switched on.
	
	If [value] is greater than 1 relay is switched on for [value]ms and then switched off. During this interval all other commands are ignored.

	**JavaScript Example to switch relay on**

	```JavaScript
	socket.send(
		JSON.stringify(
	    		{
				URL: '/relay',
				Method: 'POST',
				Data: {
					Relay: 1
				}
			}
		)
	);
	```

* ADC - Voltage on CON3 pin 16 max 1V step 1/1024V.
		
		URL : /adc
	
	**JavaScript examle to read ADC**

	```JavaScript
	socket.send(
		JSON.stringify(
	    		{
				URL: '/adc',
				Method: 'GET'
			}
		)
	);
	```
	
	Sample Response:
		
		{
			"Device" : "ESP8266", 
			"Status" : "OK", 
			"Data" : {
				"ADC" : {
					"Value" : 197 , 
					"Poll" : {
						"Refresh"   : 10, 
						"Each"      : 3, 
						"Threshold" : 5
					}
				}
			}
		}
	
	Data fields description:
		
		Value : <read-only>, 
			Min: 0
			Max: 1024
			
		Poll : {
			Refresh   : <refresh interval>,
				check for change each <refresh interval> seconds
				
			Each      : <n>, 
				raise event on each <n> interval even if the change is smaller than <threshold>
				
			Threshold : <threshold>
				raise event if the change is greater than <threshold>
		}
	
	Sample POST data:
		
		{
			"ADC" : {
				"Poll" : {
					"Refresh"   : 15, 
					"Each"      : 4, 
					"Threshold" : 10
				}
			}
		}

### I2C
	
For all I2C devices the last part of the URL is address of the device in HEX format.
If address is not present then the default I2C address is used

* MOD-RGB
		 	
		URL : 
			/mod-rgb
			/mod-rgb/<address>
			
		Default address: 0x20

	Sample Response:
		
		{
			"Device" : "MOD-RGB", 
			"Status" : "OK", 
			"Data"   : {
				"R" : 0, 
				"G" : 0, 
				"B" : 0
			}, 
			"I2C_Address" : "0x20"
		}

	Data fields description
		
		R : <red>, 
			Min: 0
			Max: 255
			
		G : <green>, 
			Min: 0
			Max: 255
			
		B : <blue>
			Min: 0
			Max: 255
			
		
	Sample POST data
		
		{
			"R" : 20, 
			"G" : 0, 
			"B" : 0
		}
	
	**JavaScript example to light green on 10%**

	```JavaScript
	socket.send(
		JSON.stringify(
		    	{
		    		URL: "/mod-rgb",
		    		Method: "POST",
		    		Data: {
		    			R: 0,
		    			G: 25,
		    			B: 0
		    		}
		    	}
		)
	);
	```

* MOD-TC-MK2-31855
		
		URL : 
			/mod-tc-mk2
			/mod-tc-mk2/<address>
			
		Default address: 0x23
	
	```JavaScript
	// Read MOD-TC-MK2-31855 temperature sensor
	socket.send(
		JSON.stringify(
		    	{
		    		URL: "/mod-tc-mk2",
		    		Method: "GET"
		    	}
		)
	);
	```
	
	Sample Response:
		
		{
			"Device" : "MOD-TC-MK2", 
			"Status" : "OK", 
			"Data" : {
				"Temperature" : 24.75 , 
				"Poll" : {
					"Refresh"   : 10, 
					"Each"      : 3, 
					"Threshold" : 2
				}
			}, 
			"I2C_Address" : "0x23"
		}

	Data fields description
		
		Temperature : 24.75
			temperature in degrees Celsius - step 0.25 degrees
		
		Poll : {
			Refresh   : <refresh interval>,
				check for change each <refresh interval> seconds
				
			Each      : <n>, 
				raise event on each <n>-th interval even if the change is smaller than <threshold>
				
			Threshold : <threshold>
				1 per each 0.25 degrees
				raise event if the change is greater than <threshold>
		}
	
	Sample POST data
		
		{
			"Poll" : {
				"Refresh"   : 30, 
				"Each"      : 2, 
				"Threshold" : 2
			}
		}

* MOD-IO2
		
		URL : 
			/mod-io2
			/mod-io2/<address>
			
		Default address: 0x21
		
	Sample Response:
		
		{
			"Device" : "MOD-IO2", 
			"Status" : "OK", 
			"Data" : {
				"Relay1" : 0, 
				"Relay2" : 0, 
				"GPIO0" : 243, 
				"GPIO1" : 95, 
				"GPIO2" : 0, 
				"GPIO3" : 0, 
				"GPIO4" : 1, 
				"GPIO5" : 0, 
				"GPIO6" : 0
			}, 
			"I2C_Address" : "0x21"
		}
	
	Data fields description
		
		Relay1 : <relay state>, 
		Relay2 : <relay state>, 
		
		GPIO0 : <read-only>, 
			ADC Input
			Min: 0
			Max: 1024
			
		GPIO1 : <read-only>, 
			ADC Input
			Min: 0
			Max: 1024
		
		GPIO2 : <0 | 1>, 
			Digital Output
			
		GPIO3 : <read-only>, 
			Digital Input
			
		GPIO4 : <read-only>, 
			Digital Input
		
		GPIO5 : 0, 
			DAC Output
			Min: 0
			Max: 255
			
		GPIO6 : 0, 
			DAC Output
			Min: 0
			Max: 255
	
	Sample POST data
		
		{
			"Relay1" : 0,
			"Relay2" : 0,
			"GPIO2"  : 1,
			"GPIO5"  : 0,
			"GPIO6"  : 100
		}

* MOD-IRDA+
		
		URL : 
			/mod-irda
			/mod-irda/<address>
			
		Default address: 0x24

	Sample Response:
		
		{
			"Device" : "MOD-IRDA", 
			"Status" : "OK", 
			"Data"   : {
				"Mode" : "RC5", 
				"Device" : 0, 
				"Command" : 0 
			}, 
			"I2C_Address" : "0x24"
		}
	
	Data fields description
		
		Mode : <RC5 | SIRC>
			
		Device : 0, 
			Read MOD-IRDA Documentation or see Sample HTML Application
		
		Command : 0 
			Read MOD-IRDA Documentation or see Sample HTML Application

	Sample POST data
		
		{
			"Mode" : "SIRC", 
			"Device" : 0, 
			"Command" : 16 
		}
	
### SPI
* MOD-LED8x8RGB
		
		URL : /mod-led-8x8-rgb
	
	Panels have to be connected from left to right and up from bottom.
	Maximum number of panels - 48
	
	Sample Response:
		
		{
			"Device" : "MOD-LED8x8RGB", 
			"Status" : "OK", 
			"Data"   : {
				"cols"  : 1, 
				"rows"  : 1, 
				"Speed" : 85, 
				"R" : 1, 
				"G" : 1, 
				"B" : 1, 
				"Text" : ""
			}
		}
		
	Data fields description
		
		cols  : <columns>
		rows  : <rows>
		Speed : <speed>
			Min: 0
			Max: 100
		R : <red>
		G : <green>
		B : <blue>
		
		Text : <text>
			Maximum length - 255 chars

### UART
* MOD-RFID125-BOX
		
		URL : /mod-rfid
	
	Event is generated on every read tag
	
	Sample Response:
		
		{
			"Device" : "MOD-RFID", 
			"Status" : "OK", 
			"Data"   : {
				"Frequency" : 0, 
				"Led"       : 1, 
				"Tag"       : ""
			}
		}
	
	Data fields description
		
		Frequency : 0, 
			scan frequency 
				0   - continuous
				1-9 - times per second
				
		Led : 1, 
			Enable / Disable LEDs
			
		Tag : <tag>
			last read tag
	
### EVENTS
	
	URL : /events

Long poll GET request or WebSockets (Recommended)

System status change
	
	{
		"Device" : "ESP8266", 
		"Status" : <status>, 
	}

Status values	
	
	Connected
	Reconnect station
	Rebooting
	Restored defaults
	Authorization success
	
Common Events Message Format
	
	{
		EventURL   : <URL>,
		EventsData : <Basic Response Message>
	}
	
Sample Event Message

	{
		EventURL   : "/button",
		EventsData : {
			"Device" : "ESP8266", 
			"Status" : "OK", 
			"Data" : {
				"Button" : "Short Press"
			}
		}
	}
	
	
## Sample HTML / JavaScript application

See *app* folder - index.html			
	
![Sample Application Screenshot](ESP8266-Sample-HTML-JS-Application.jpg "Sample Application Screenshot")
	
