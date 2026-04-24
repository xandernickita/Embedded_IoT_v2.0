package com.example.hc_05_application.bluetooth

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.annotation.RequiresPermission

class ClassicDiscovery(
    private val context: Context,
    private val adapter: BluetoothAdapter,
    private val onFound: (BluetoothDevice) -> Unit,
    private val onFinished: () -> Unit
) {
    private val receiver = object : BroadcastReceiver() {
        @SuppressLint("MissingPermission") // enforce permissions before starting discovery
        override fun onReceive(ctx: Context, intent: Intent) {
            when (intent.action) {
                BluetoothDevice.ACTION_FOUND -> {
                    val device: BluetoothDevice? =
                        intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                    if (device != null) onFound(device)
                }
                BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> onFinished()
            }
        }
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_SCAN)
    fun start() {
        val f = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
        }
        context.registerReceiver(receiver, f)
        adapter.startDiscovery()
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_SCAN)
    fun stop() {
        try { context.unregisterReceiver(receiver) } catch (_: Exception) {}
        adapter.cancelDiscovery()
    }
}