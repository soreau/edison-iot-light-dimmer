/*
 * Copyright (c) 2016 Scott Moreau
 */

var net = require('net');
var dgram = require('dgram');
var http = require('http');
var io = require('socket.io');
var dgram = require('dgram');
var fs = require('fs');
var exec = require('child_process').exec;

var major_version = process.versions.node.split(".")[0];

var enabled = 0;
var max_brightness = 0x7F;
var current_brightness = 0;

var sport = 25001;
var uport = 25002;

var uuid_file = "device_uuid";
var config_file = "config_data";

var device_uuid = null;
var local_socket = null;
var local_socket_timeout = null;

var clients = [];


function validate_local_socket() {
	if (!local_socket || local_socket.destroyed)
		local_socket = connect_local_socket();
}

function reconnect_local_socket() {
	validate_local_socket();
	local_socket_timeout = setTimeout(validate_local_socket, 3000);
}

function send_data(data) {
	var out = data.split(":");
	if (out.length != 3)
		return;
	clients.forEach(function(client) {
		client.emit("value_update", {id: device_uuid, type: out[0], component: out[1], value: out[2]});
	});
	if (out[0] == "toggle")
		enabled = out[2];
	else if (out[0] == "range")
		current_brightness = out[2];

	write_config_file();
}

function write_config_file() {
	var data = {enabled: enabled, brightness: current_brightness};
	fs.writeFile(config_file, JSON.stringify(data));
}

fs.readFile(config_file, 'utf8', function (err, json) {
	if (err) {
		write_config_file();
	} else {
		try {
			data = JSON.parse(json);
			enabled = data.enabled;
			current_brightness = data.brightness;
		} catch(e) {
			write_config_file();
		}
	}

	validate_local_socket();

	local_socket.write("t0:" + enabled + "\n");
	local_socket.write("r0:" + current_brightness + "\n");
});

function generate_uuid() {
	var d = new Date().getTime();

	var id = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
		var r = (d + Math.random()*16)%16 | 0;
		d = Math.floor(d/16);
		return (c=='x' ? r : (r&0x3|0x8)).toString(16);
	});
	console.log("New uuid generated: " + id);
	return id;
}

function send_descriptor(remote_client, uuid) {
	remote_client.emit("descriptor_response", {
						id: uuid,
						name: "Edison Dimmer Switch",
						components: [{
							type: "toggle",
							name: "t0",
							min: 0,
							max: 1,
							current: enabled }, {

							type: "range",
							name: "r0",
							min: 1,
							max: max_brightness,
							current: current_brightness}]
	});
}

function send_descriptor_response(remote_client) {
	var uuid;

	fs.readFile(uuid_file, 'utf8', function (err, data) {
		if (device_uuid)
			uuid = device_uuid;
		else if (err) {
			uuid = generate_uuid();
			fs.writeFile(uuid_file, uuid, function (err) {
				if (err)
					throw err;
			});
		} else
			uuid = data;
		device_uuid = uuid;
		send_descriptor(remote_client, uuid);

		validate_local_socket();
	});
}

function connect_local_socket() {
	var domain_socket = net.createConnection("/tmp/socket");

	domain_socket.on("error", function() {
		console.log("Failed to connect to /tmp/socket");
	});

	domain_socket.on("connect", function() {
		console.log("Connected to socket");
		if (local_socket_timeout)
			clearTimeout(local_socket_timeout);
		local_socket_timeout = null;
		//domain_socket.write("Hello\n");
	});

	domain_socket.on("close", function() {
		console.log("Closed socket");
		local_socket_timeout = setTimeout(reconnect_local_socket, 1000);
	});

	domain_socket.on("data", function(data) {
		var i = 0;
		var decoded = [];
		decoded[i] = "";
		for (c = 0; c < data.length; c++) {
			if (data[c] == 0x0a) {
				i++;
				decoded[i] = "";
			} else
				decoded[i] += String.fromCharCode(data[c]);
		}
		decoded.forEach(function(str) {
			if (str.length)
				send_data(str);
		});
	});

	return domain_socket;
}


/* Make device discovable by listening and responding to datagrams */

var socket = dgram.createSocket("udp4");

socket.on("message", function (msg, data) {
    if (msg.toString() != "IOT_DISCOVER_DEVICE_REQUEST")
	return;

    console.log("Received " + msg.toString() + " from " + data.address + ":" + uport);

    const message = new Buffer("IOT_DISCOVER_DEVICE_RESPONSE");

    console.log("Sending " + message + " to " + data.address + ":" + data.port);
    socket.send(message, 0, message.length, data.port, data.address, function (err, bytes) {
        if (err) {
            console.log(err);
            throw err;
        }
    });
});

socket.on("listening", function() {
    var address = socket.address();
    console.log("listening on :" + address.address + ":" + address.port);
});

socket.bind(uport);

function request_client_connect() {
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
		const message = new Buffer("IOT_CLIENT_CONNECT_REQUEST");
		console.log("Sending " + message + " to " + bcast_addr + ":" + uport);
		ds.send(message, 0, message.length, uport, bcast_addr, function(err, bytes) {
		if (err)
			console.log(err);
		});
	});

	ds.bind();
}


/* Socket.io server - connection to remote client */

var server = http.createServer();

server.listen(sport);
io = io.listen(server);

io.on("connection", function(c) {
	console.log("Client connected.");
	clients.push(c);

	validate_local_socket();

	/* Disconnect listener */
	c.on("disconnect", function() {
		console.log("Client disconnected.");
		var idx = clients.indexOf(c);
		if (idx !== -1)
			clients.splice(idx, 1);
	});

	c.on("descriptor_request", function() {
		console.log("Received descriptor_request");
		send_descriptor_response(c);
		console.log("Sent descriptor_response");
	});

	c.on("value_update", function(data) {
		validate_local_socket();

		local_socket.write(data.component + ":" + data.value + "\n");

		if (data.type == "toggle")
			enabled = data.value;
		else if (data.type == "range")
			current_brightness = data.value;

		write_config_file();

		clients.forEach(function(client) {
			if (c != client)
				client.emit("value_update", {id: device_uuid, type: data.type, component: data.component, value: data.value});
		});
	});
});

request_client_connect();
