/*
 * Copyright (c) 2016 Scott Moreau
 */

package com.github.soreau.homecontrol;

import android.os.AsyncTask;
import android.util.Log;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Enumeration;
import java.lang.Thread;
import java.util.List;

// Find the servers using UDP broadcast
public class udp_discovery extends AsyncTask<ThreadData, String, Void> {
    private final static String					LOG_TAG				= "UDP";
    ThreadData td;

    protected Void doInBackground(ThreadData... params) {
        Log.d(LOG_TAG, "doInBackground called");
        int port = 25002;
        boolean done = false;
        List<String> addresses = new ArrayList<>();
        td = params[0];

        try {
            //Open a random port to send the package
            DatagramSocket c = new DatagramSocket();
            c.setBroadcast(true);

            byte[] sendData = "IOT_DISCOVER_DEVICE_REQUEST".getBytes();

            //Try the 255.255.255.255 first
            try {
                DatagramPacket sendPacket = new DatagramPacket(sendData, sendData.length, InetAddress.getByName("255.255.255.255"), port);
                c.send(sendPacket);
                Log.d(LOG_TAG, getClass().getName() + ">>> Request packet sent to: 255.255.255.255 (DEFAULT)");
            } catch (Exception e) {
            }

            // Broadcast the message over all the network interfaces
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces.hasMoreElements()) {
                NetworkInterface networkInterface = interfaces.nextElement();

                if (networkInterface.isLoopback() || !networkInterface.isUp()) {
                    continue; // Don't want to broadcast to the loopback interface
                }

                for (InterfaceAddress interfaceAddress : networkInterface.getInterfaceAddresses()) {
                    InetAddress broadcast = interfaceAddress.getBroadcast();
                    if (broadcast == null) {
                        continue;
                    }

                    // Send the broadcast package!
                    try {
                        DatagramPacket sendPacket = new DatagramPacket(sendData, sendData.length, broadcast, port);
                        c.send(sendPacket);
                    } catch (Exception e) {
                    }

                    Log.d(LOG_TAG, getClass().getName() + ">>> Request packet sent to: " + broadcast.getHostAddress() + "; Interface: " + networkInterface.getDisplayName());
                }
            }

            Log.d(LOG_TAG, getClass().getName() + ">>> Done looping over all network interfaces. Now waiting for replies!");

            c.setSoTimeout(3000);
            String message = "";

            //Wait for a response
            while(!done) {
                try {
                    message = "";
                    byte[] recvBuf = new byte[1500];
                    DatagramPacket receivePacket = new DatagramPacket(recvBuf, recvBuf.length);
                    c.receive(receivePacket);

                    //We have a response
                    Log.d(LOG_TAG, getClass().getName() + ">>> Broadcast response from server: " + receivePacket.getAddress().getHostAddress());

                    //Check if the message is correct
                    message = new String(receivePacket.getData()).trim();
                    if (message.equals("IOT_DISCOVER_DEVICE_RESPONSE")) {
                        String address = receivePacket.getAddress().getHostAddress();
                        Log.d(LOG_TAG, ">>> Found device ip: " + address + ":" + receivePacket.getPort() + " " + message);
                        boolean found = false;
                        for (String a : addresses) {
                            if(a.equals(address)) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            web_socket_io.begin_device_comms(td, address);
                            addresses.add(address);
                        }
                    }
                    try {
                        Thread.sleep(250);
                    } catch (Exception e) {
                        Log.d(LOG_TAG, "Failed to sleep ...");
                    }
                }
                catch (SocketTimeoutException ste) {
                    if (message == null || message.length() == 0) {
                        done = true;
                    } else
                        Log.d(LOG_TAG, "SocketTimeoutException: " + message);
                }
            }

            // Close the port
            c.close();
            //  ..or wait for more devices?
        } catch (IOException ex) {
            Log.d(LOG_TAG, "Error: UDP discovery failed");
        }
        return null;
    }

    protected void onPostExecute (Void result) {
        Log.d(LOG_TAG, "Info: UDP finished");
    }
}
