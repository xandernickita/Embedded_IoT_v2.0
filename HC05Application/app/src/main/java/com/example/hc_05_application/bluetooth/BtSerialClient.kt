package com.example.hc_05_application.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.IOException
import java.util.UUID

private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

class BtSerialClient(
    private val adapter: BluetoothAdapter
) {
    private var socket: BluetoothSocket? = null

    val isConnected: Boolean
        get() = socket?.isConnected == true

    @SuppressLint("MissingPermission") // you must enforce BLUETOOTH_CONNECT at runtime before calling
    suspend fun connect(device: BluetoothDevice) = withContext(Dispatchers.IO) {
        disconnect()

        // Create RFCOMM socket
        val s = device.createRfcommSocketToServiceRecord(SPP_UUID)

        // Discovery slows connections - cancel first
        adapter.cancelDiscovery()

        try {
            s.connect()
            socket = s
        } catch (e: IOException) {
            try { s.close() } catch (_: IOException) {}
            throw e
        }
    }

    suspend fun sendLine(line: String) = withContext(Dispatchers.IO) {
        val s = socket ?: throw IllegalStateException("Not connected")
        val bytes = (line + "\r\n").toByteArray(Charsets.US_ASCII)
        s.outputStream.write(bytes)
        s.outputStream.flush()
    }

    suspend fun disconnect() = withContext(Dispatchers.IO) {
        try { socket?.close() } catch (_: IOException) {}
        socket = null
    }
}