/*
 * Copyright (c) 2016 Scott Moreau
 */

var express = require("express");
var app = express();
var http = require("http").Server(app);
var io = require("socket.io")(http);
var os = require('os');
var dgram = require('dgram');

var device_addresses = [];
var device_sockets = [];
var device_socket = require('socket.io-client');

var wport = 8080;
var sport = 8081;
var dport = 25001;
var uport = 25002;

var connect_timeout = 2000;
var connecting = false;
var client_sockets = [];

var major_version = process.versions.node.split(".")[0];


/* Serve index.html */
app.use(express.static(__dirname));

/* Listen on 8080 for http and 8081 for socket.io */
http.listen(sport, "127.0.0.1", function() {
	console.log("Socket.io listening on *:" + sport);

	app.listen(wport, "127.0.0.1", function() {
		console.log("Http listening on *:" + wport);
	});
});


/* Connect to device server */
function begin_device_comms(address, port) {
	var socket_addr = "http://" + address + ":" + port;
	console.log("Attemption connection to " + socket_addr);

	var socket = device_socket.connect(socket_addr, {reconnect: true});

	socket.on("connect", function() {
		console.log("Connected to device server " + socket.io.opts.hostname + ":" + socket.io.opts.port);
		socket.emit("descriptor_request");
		console.log("Sent descriptor_request");
		client_sockets.forEach(function(client) {
			client.emit("connect");
		});
	});
	socket.on("disconnect", function() {
		console.log("Device server disconnected");
		device_sockets.forEach(function(d) {
			if (d.socket == socket) {
				var i = device_sockets.indexOf(d);
				if (i > -1)
					device_sockets.splice(i, 1);
				client_sockets.forEach(function(client) {
					client.emit("device-disconnect", {id: d.data.id});
				});
			}
		});
		socket.close();
	});
	socket.on("descriptor_response", function(data) {
		console.log("Received descriptor_response");
		client_sockets.forEach(function(client) {
			client.emit("descriptor", data);
		});
		var exists = false;
		device_sockets.forEach(function(d) {
			if (d.data.id == data.id) {
				exists = true;
				d.data.components.forEach(function(c) {
					if (c.type == data.type) {
						c.current = data.value;
					}
				});
			}
		});
		if (!exists)
			device_sockets.push({data: data, socket: socket});
		connecting = false;
	});
	socket.on("value_update", function(data) {
		client_sockets.forEach(function(client) {
			client.emit("value_update", data);
		});
		device_sockets.forEach(function(d) {
			d.data.components.forEach(function(c) {
				if (c.name == data.component)
					c.current = data.value;
			});
		});
	});
}


/* Device discovery functions */

function close_dgram_socket(socket) {
	socket.close();
	console.log("dgram socket closed");

	if (!device_addresses.length) {
		console.log("No devices found");
		if (connecting)
			begin_dgram_discovery();
	}
}

function get_ipv4_addresses() {
	var addresses = [];
	var interfaces = os.networkInterfaces();

	for (var i in interfaces) {
		for (var iface in interfaces[i]) {
			var address = interfaces[i][iface];
			if (address.family === 'IPv4' && !address.internal)
				addresses.push(address.address);
		}
	}
	return addresses;
}

/* Send datagrams on a few broadcast addresses and
 * listen for replies to discover the device addresses */

function begin_dgram_discovery() {
	var timeout = null;
	var ds;
	if (major_version < 4)
		ds = dgram.createSocket("udp4");
	else
		ds = dgram.createSocket({type: 'udp4', reuseAddr: true});

	var bcast_addr = '255.255.255.255';
	var address;

	ds.on('listening', function() {
		address = ds.address();
		console.log("listening on :" + address.address + ":" + address.port);
		ds.setBroadcast(true);
		const message = new Buffer("IOT_DISCOVER_DEVICE_REQUEST");
		console.log("Sending " + message + " to " + bcast_addr + ":" + uport);
		ds.send(message, 0, message.length, uport, bcast_addr, function(err, bytes) {
		if (err)
			console.log(err);
		});

		var addresses = get_ipv4_addresses();
		for (i in addresses) {
			var a = addresses[i];
			var last_octect = a.split('.').pop();
			var subnet = a.slice(0, a.length - last_octect.length);
			bcast_addr = subnet + "255";
			console.log("Sending " + message + " to " + bcast_addr + ":" + uport);
			ds.send(message, 0, message.length, uport, bcast_addr, function(err, bytes) {
				if (err)
					console.log(err);
			});
			console.log("Sending " + message + " to " + a + ":" + uport);
			ds.send(message, 0, message.length, uport, a, function(err, bytes) {
				if (err)
					console.log(err);
			});
		}
	});

	ds.on('message', function (msg, data) {
		if (msg.toString() != "IOT_DISCOVER_DEVICE_RESPONSE")
			return;

		/* We might get duplicate messages from the same device
		 * so check we haven't already added it to the list */
		if (device_addresses.indexOf(data.address) == -1) {
			device_addresses.push(data.address);
			begin_device_comms(data.address, dport);
		}
		console.log("Received " + msg.toString() + " from " + data.address + ":" + address.port);
		if (timeout) {
			clearTimeout(timeout);
			timeout = setTimeout(close_dgram_socket, connect_timeout, ds);
		}
	});

	if (timeout)
		clearTimeout(timeout);

	timeout = setTimeout(close_dgram_socket, connect_timeout, ds);

	ds.bind();
}

function device_discovery_listen() {
	var socket;
	if (major_version < 4)
		socket = dgram.createSocket("udp4");
	else
		socket = dgram.createSocket({type: 'udp4', reuseAddr: true});
	socket.on("message", function (msg, data) {
		if (msg.toString() != "IOT_CLIENT_CONNECT_REQUEST")
			return;
		if (!client_sockets.length)
			return;

		console.log("Received " + msg.toString() + " from " + data.address + ":" + data.port);
		begin_device_comms(data.address, dport);
	});

	socket.on("listening", function() {
	    var address = socket.address();
	    console.log("listening on :" + address.address + ":" + address.port);
	});

	socket.bind(uport);
}


/* Initial connection
 * Socket.io connection fires when localhost:8080 is requested from browser */

io.on("connection", function(socket) {
	console.log("Client connected.");

	client_sockets.push(socket);

	socket.on("disconnect", function() {
		var idx = client_sockets.indexOf(socket);
		if (idx !== -1)
			client_sockets.splice(idx, 1);
		if (!client_sockets.length) {
			console.log("Closing device sockets");
			device_sockets.forEach( function(d) {
				d.socket.close();
			});
			device_sockets = [];
			connecting = false;
		}
		device_addresses = [];
		console.log("Client disconnected.");
	});

	if (!connecting && !device_sockets.length) {
		connecting = true;
		begin_dgram_discovery();
	} else {
		device_sockets.forEach(function(d) {
			socket.emit("descriptor", d.data);
		});
	}

	socket.on("value_update", function(data) {
		device_sockets.forEach(function(d) {
			if (d.data.id == data.id) {
				d.socket.emit("value_update", {type: data.type, component: data.component, value: data.value});
				d.data.components.forEach(function(c) {
					if (c.type == data.type) {
						c.current = data.value;
					}
				});
			}
		});
		client_sockets.forEach(function(s) {
			if (s != socket)
				s.emit("value_update", data);
		});
	});
});

device_discovery_listen();
