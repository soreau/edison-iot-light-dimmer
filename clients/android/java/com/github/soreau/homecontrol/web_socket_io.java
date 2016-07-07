/*
 * Copyright (c) 2016 Scott Moreau
 */

package com.github.soreau.homecontrol;


import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.CheckBox;
import android.widget.RelativeLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.github.nkzawa.emitter.Emitter;
import com.github.nkzawa.socketio.client.IO;
import com.github.nkzawa.socketio.client.Socket;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.List;

public class web_socket_io {

    private static class Component {
        public String type;
        public String name;
        public int min;
        public int max;
        public int current;
    }

    private static class Device {
        public String uuid;
        public String name;
        public List<Component> components;
    }

    private static class UI_Object {
        TextView tv;
        CheckBox cb;
        SeekBar sb;
    }

    private static Handler mainHandler;
    private static ThreadData td;
    private static UI_Object ui_object;
    private static Socket socket;
    private static List<Device> devices;
    private static boolean tracking_touch;

    private static void add_ui_object(final Device d) {
        Log.d("socket_io", "add_ui_object");
        Log.d("socket_io", "uuid: " + d.uuid);
        Log.d("socket_io", "name: " + d.name);

        Runnable myRunnable = new Runnable() {
            @Override
            public void run() {
                if (Looper.myLooper() == Looper.getMainLooper())
                    Log.d("socket_io", "Running on UI Thread 3");
                else
                    Log.d("socket_io", "not running on UI Thread 3");

                RelativeLayout device2_layout = new RelativeLayout(td.context);
                RelativeLayout.LayoutParams device2_params = new RelativeLayout.LayoutParams(
                        RelativeLayout.LayoutParams.MATCH_PARENT,
                        RelativeLayout.LayoutParams.WRAP_CONTENT);
                device2_params.setMargins(10, 10, 10, 10);
                device2_layout.setBackgroundColor(Color.parseColor("#f8f8f8"));
                device2_layout.setLayoutParams(device2_params);
                RelativeLayout.LayoutParams text_params = new RelativeLayout.LayoutParams(
                        RelativeLayout.LayoutParams.MATCH_PARENT,
                        RelativeLayout.LayoutParams.WRAP_CONTENT);
                RelativeLayout.LayoutParams sparams = new RelativeLayout.LayoutParams(
                        RelativeLayout.LayoutParams.MATCH_PARENT,
                        RelativeLayout.LayoutParams.WRAP_CONTENT);
                RelativeLayout.LayoutParams cbparams = new RelativeLayout.LayoutParams(
                        RelativeLayout.LayoutParams.MATCH_PARENT,
                        RelativeLayout.LayoutParams.WRAP_CONTENT);
                ui_object.tv = new TextView(td.context);
                ui_object.tv.setText(d.name);
                ui_object.tv.setTextSize(18);
                ui_object.tv.setGravity(Gravity.CENTER);
                ui_object.tv.setLayoutParams(text_params);
                ui_object.tv.setPadding(0, 40, 0, 40);
                device2_layout.addView(ui_object.tv);
                for (final Component c : d.components) {
                    Log.d("socket_io", "type: " + c.type);
                    Log.d("socket_io", "name: " + c.name);
                    Log.d("socket_io", "min: " + c.min);
                    Log.d("socket_io", "max: " + c.max);
                    Log.d("socket_io", "current: " + c.current);
                    if (c.type.equals("toggle")) {
                        ui_object.cb = new CheckBox(td.context);
                        cbparams.setMargins(20, 20, 0, 0);
                        ui_object.cb.setLayoutParams(cbparams);
                        ui_object.cb.setPadding(50, 50, 50, 100);
                        ui_object.cb.setChecked(c.current == 1);
                        ui_object.cb.setOnClickListener(new View.OnClickListener() {
                            @Override
                            public void onClick(View v) {
                                try {
                                    JSONObject json = new JSONObject();
                                    json.put("id", d.uuid);
                                    json.put("type", c.type);
                                    json.put("component", c.name);
                                    json.put("value", ((CheckBox) v).isChecked() ? 1 : 0);
                                    socket.emit("value_update", json);
                                } catch (JSONException e) {
                                }
                                if (((CheckBox) v).isChecked()) {
                                    Log.d("socket_io", "checked");
                                } else {
                                    Log.d("socket_io", "unchecked");
                                }
                            }
                        });
                        device2_layout.addView(ui_object.cb);
                    } else if (c.type.equals("range")) {
                        ui_object.sb = new SeekBar(td.context);
                        sparams.setMargins(100, 50, 25, 0);
                        ui_object.sb.setLayoutParams(sparams);
                        ui_object.sb.setPadding(25, 25, 25, 25);
                        ui_object.sb.setMax(c.max);
                        ui_object.sb.setProgress(c.current);
                        ui_object.sb.setSecondaryProgress(0);
                        ui_object.sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                            @Override
                            public void onProgressChanged(SeekBar seekBar, int progress, boolean arg2) {
                                if (tracking_touch) {
                                    Log.d("socket_io", "seekbar progress: " + progress);
                                    try {
                                        JSONObject json = new JSONObject();
                                        json.put("id", d.uuid);
                                        json.put("type", c.type);
                                        json.put("component", c.name);
                                        json.put("value", progress);
                                        socket.emit("value_update", json);
                                    } catch (JSONException e) {
                                    }
                                }
                            }

                            @Override
                            public void onStartTrackingTouch(SeekBar seekBar) {
                                tracking_touch = true;
                            }

                            @Override
                            public void onStopTrackingTouch(SeekBar seekBar) {
                                tracking_touch = false;
                            }
                        });
                        device2_layout.addView(ui_object.sb);
                    }
                }
                td.content_layout.addView(device2_layout);
            }
        };
        mainHandler.post(myRunnable);
    }

    private static Device device_from_json(JSONObject data) {
        Device device = new Device();
        try {
            device.uuid = data.getString("id");
            device.name = data.getString("name");
            device.components = new ArrayList<Component>();
            JSONArray components = data.getJSONArray("components");
            for (int i = 0; i < components.length(); i++) {
                Component c = new Component();
                JSONObject o = components.getJSONObject(i);
                c.type = o.getString("type");
                c.name = o.getString("name");
                c.min = o.getInt("min");
                c.max = o.getInt("max");
                c.current = o.getInt("current");
                device.components.add(c);
            }
        } catch (JSONException e) {
            Log.d("socket_io", "Problem parsing json data");
        }
        return device;
    }

    private static void update_ui_objects(JSONObject data) {
        final String type;
        final String value;
        try {
            type = data.getString("type");
            value = data.getString("value");
        }
        catch (JSONException e) {
            Log.d("socket_io", "Problem parsing json data");
            return;
        }

        Runnable myRunnable = new Runnable() {
            @Override
            public void run() {
                int v = Integer.parseInt(value);
                if (type.equals("toggle")) {
                    ui_object.cb.setChecked(v == 1);
                } else if (type.equals("range")) {
                    ui_object.sb.setProgress(v);
                }
            }
        };
        mainHandler.post(myRunnable);
    }

    private static Emitter.Listener disconnect = new Emitter.Listener() {
        @Override
        public void call(final Object... args) {
            Log.d("socket_io", "disconnected");
        }
    };

    private static Emitter.Listener value_update = new Emitter.Listener() {
        @Override
        public void call(final Object... args) {
            Log.d("socket_io", "Recieved value_update");
            JSONObject data = (JSONObject) args[0];
            update_ui_objects(data);
        }
    };

    private static Emitter.Listener on_descriptor_response = new Emitter.Listener() {
        @Override
        public void call(final Object... args) {
            Log.d("socket_io", "Recieved descriptor_response");
            JSONObject data = (JSONObject) args[0];
            Device device = device_from_json(data);
            devices.add(device);
            add_ui_object(device);
        }
    };

    public static void begin_device_comms(ThreadData data, String address) {
        try {
            Log.d("socket_io", "Beginning socket.io connection to " + address);
            socket = IO.socket("http://" + address + ":25001");
            socket.connect();
            Log.d("socket_io", "Sucessfully connected to " + address);
            socket.on("disconnect", disconnect);
            socket.on("value_update", value_update);
            socket.on("descriptor_response", on_descriptor_response);
            Log.d("socket_io", "Sending descriptor_request");
            socket.emit("descriptor_request");
        }
        catch (URISyntaxException e) {
            Log.d("socket_io", "There was an exception when attempting connection to " + address);
            Log.d("socket_io", e.getReason());
        }
        if (Looper.myLooper() == Looper.getMainLooper())
            Log.d("socket_io", "Running on UI Thread 2");
        else
            Log.d("socket_io", "not running on UI Thread 2");
        mainHandler = new Handler(data.context.getMainLooper());
        devices = new ArrayList<Device>();
        ui_object = new UI_Object();
        tracking_touch = false;
        td = data;
    }
}