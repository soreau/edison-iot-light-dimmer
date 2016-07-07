/*
 * Copyright (c) 2016 Scott Moreau
 */

package com.github.soreau.homecontrol;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.widget.RelativeLayout;
import android.widget.SeekBar;
import android.widget.TextView;


class ThreadData {
    public Context context;
    public RelativeLayout content_layout;
}

public class MainActivity extends Activity {
    private SeekBar seekBar;
    private TextView textView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {

        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ThreadData td = new ThreadData();
        td.context = this;
        td.content_layout = (RelativeLayout) findViewById(R.id.content);
        new udp_discovery().execute(td);
    }
}